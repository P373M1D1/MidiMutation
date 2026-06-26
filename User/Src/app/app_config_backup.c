#include "app/app_config_backup.h"

#include "app_event.h"
#include "app/app_requests.h"
#include "app/app_sd_card.h"
#include "presets.h"
#include "runtime_config.h"
#include "stm32f4xx_hal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define APP_CONFIG_BACKUP_PRIMARY_FILE "CONFIG.TXT"
#define APP_CONFIG_BACKUP_FALLBACK_FILE "CONFIG.BAK"
#define APP_CONFIG_BACKUP_LINE_BUFFER_SIZE 1024U
#define APP_CONFIG_BACKUP_IO_BUFFER_SIZE 1024U
#define APP_CONFIG_RESTORE_MIN_PARSED_VALUES 16U

typedef enum {
    APP_CONFIG_BACKUP_RESTORE_SECTION_TOP = 0,
    APP_CONFIG_BACKUP_RESTORE_SECTION_GLOBAL,
    APP_CONFIG_BACKUP_RESTORE_SECTION_METRONOME,
    APP_CONFIG_BACKUP_RESTORE_SECTION_DEVICE,
    APP_CONFIG_BACKUP_RESTORE_SECTION_BANK,
    APP_CONFIG_BACKUP_RESTORE_SECTION_PRESET,
    APP_CONFIG_BACKUP_RESTORE_SECTION_GLOBAL_BYPASS,
    APP_CONFIG_BACKUP_RESTORE_SECTION_GLOBAL_MUTE,
    APP_CONFIG_BACKUP_RESTORE_SECTION_USER_THEME,
} AppConfigBackupRestoreSection_t;

typedef struct {
    RuntimeConfig_t *config;
    Preset_t *presets;
    AppConfigBackupRestoreSection_t section;
    uint8_t index;
    uint8_t format_version_seen;
    uint32_t parsed_values;
} AppConfigBackupRestoreContext_t;

static uint8_t app_config_backup_sd_probed = 0U;
static RuntimeConfig_t app_config_backup_restore_config;
static Preset_t app_config_backup_restore_presets[PRESET_COUNT];
static AppSdCardFile_t *app_config_backup_write_file = NULL;
static uint8_t app_config_backup_write_buffer[APP_CONFIG_BACKUP_IO_BUFFER_SIZE];
static uint16_t app_config_backup_write_buffer_length = 0U;
static AppSdCardFile_t *app_config_backup_read_file = NULL;
static uint8_t app_config_backup_read_buffer[APP_CONFIG_BACKUP_IO_BUFFER_SIZE];
static uint16_t app_config_backup_read_buffer_position = 0U;
static uint16_t app_config_backup_read_buffer_length = 0U;

static uint32_t AppConfigBackup_ElapsedMs(uint32_t start_tick)
{
    return HAL_GetTick() - start_tick;
}

static void AppConfigBackup_LogStage(const char *operation,
                                     const char *stage,
                                     uint32_t start_tick)
{
    printf("SDBACKUP_MON op=%s stage=%s ms=%lu\r\n",
           operation ? operation : "?",
           stage ? stage : "?",
           (unsigned long)AppConfigBackup_ElapsedMs(start_tick));
}

static void AppConfigBackup_LogFileStage(const char *operation,
                                         const char *filename,
                                         const char *stage,
                                         uint32_t start_tick,
                                         uint8_t ok)
{
    printf("SDBACKUP_MON op=%s file=%s stage=%s ms=%lu ok=%u\r\n",
           operation ? operation : "?",
           filename ? filename : "?",
           stage ? stage : "?",
           (unsigned long)AppConfigBackup_ElapsedMs(start_tick),
           (unsigned)ok);
}

static void AppConfigBackup_LogSdMetrics(const char *operation,
                                         uint32_t start_tick,
                                         uint32_t result)
{
    AppSdCardMetrics_t metrics;

    AppSdCard_GetMetrics(&metrics);
    printf("SDBACKUP_MON op=%s stage=summary total_ms=%lu result=%lu bytes_rd=%lu bytes_wr=%lu transfers=%lu exclusive=%lu\r\n",
           operation ? operation : "?",
           (unsigned long)AppConfigBackup_ElapsedMs(start_tick),
           (unsigned long)result,
           (unsigned long)metrics.bytes_read,
           (unsigned long)metrics.bytes_written,
           (unsigned long)metrics.transfer_windows,
           (unsigned long)metrics.exclusive_windows);
    printf("SDBACKUP_MON op=%s stage=blocks rd_single=%lu rd_multi_cmd=%lu rd_multi_blocks=%lu wr_single=%lu rd_fail=%lu wr_fail=%lu\r\n",
           operation ? operation : "?",
           (unsigned long)metrics.single_block_reads,
           (unsigned long)metrics.multi_block_read_commands,
           (unsigned long)metrics.multi_block_read_blocks,
           (unsigned long)metrics.single_block_writes,
           (unsigned long)metrics.read_block_failures,
           (unsigned long)metrics.write_block_failures);
    printf("SDBACKUP_MON op=%s stage=fat fat_rd=%lu fat_wr=%lu root_rd=%lu root_wr=%lu alloc=%lu freed=%lu free_scan=%lu\r\n",
           operation ? operation : "?",
           (unsigned long)metrics.fat_entry_reads,
           (unsigned long)metrics.fat_entry_writes,
           (unsigned long)metrics.root_dir_block_reads,
           (unsigned long)metrics.root_dir_block_writes,
           (unsigned long)metrics.clusters_allocated,
           (unsigned long)metrics.clusters_freed,
           (unsigned long)metrics.free_cluster_scan_steps);
    printf("SDBACKUP_MON op=%s stage=waits read_poll_total=%lu read_poll_max=%lu read_timeout=%lu write_busy_total=%lu write_busy_max=%lu write_timeout=%lu\r\n",
           operation ? operation : "?",
           (unsigned long)metrics.read_token_polls_total,
           (unsigned long)metrics.read_token_polls_max,
           (unsigned long)metrics.read_token_timeouts,
           (unsigned long)metrics.write_busy_polls_total,
           (unsigned long)metrics.write_busy_polls_max,
           (unsigned long)metrics.write_busy_timeouts);
    printf("SDBACKUP_MON op=%s stage=latency rd_ms=%lu rd_max=%lu multi_rd_ms=%lu multi_rd_max=%lu wr_ms=%lu wr_max=%lu\r\n",
           operation ? operation : "?",
           (unsigned long)metrics.read_block_ticks_total,
           (unsigned long)metrics.read_block_ticks_max,
           (unsigned long)metrics.multi_read_ticks_total,
           (unsigned long)metrics.multi_read_ticks_max,
           (unsigned long)metrics.write_block_ticks_total,
           (unsigned long)metrics.write_block_ticks_max);
}

static uint8_t AppConfigBackup_ResultIsSuccess(AppConfigBackupResult_t result)
{
    return (result == APP_CONFIG_BACKUP_RESULT_OK
         || result == APP_CONFIG_BACKUP_RESULT_PARTIAL) ? 1U : 0U;
}

static uint8_t AppConfigBackup_RestoreResultIsSuccess(AppConfigRestoreResult_t result)
{
    return (result == APP_CONFIG_RESTORE_RESULT_OK) ? 1U : 0U;
}

static void AppConfigBackup_WriteBufferBegin(AppSdCardFile_t *file)
{
    app_config_backup_write_file = file;
    app_config_backup_write_buffer_length = 0U;
}

static uint8_t AppConfigBackup_WriteBufferFlush(void)
{
    uint16_t length = app_config_backup_write_buffer_length;

    if (!app_config_backup_write_file)
        return 0U;

    if (length == 0U)
        return 1U;

    app_config_backup_write_buffer_length = 0U;
    return (AppSdCard_WriteFile(app_config_backup_write_file,
                                app_config_backup_write_buffer,
                                length) == length) ? 1U : 0U;
}

static uint8_t AppConfigBackup_WriteBuffered(const uint8_t *data, uint32_t length)
{
    if (!data || !app_config_backup_write_file)
        return 0U;

    while (length > 0UL)
    {
        uint16_t space = (uint16_t)(APP_CONFIG_BACKUP_IO_BUFFER_SIZE - app_config_backup_write_buffer_length);
        uint16_t chunk;

        if (space == 0U)
        {
            if (!AppConfigBackup_WriteBufferFlush())
                return 0U;

            space = APP_CONFIG_BACKUP_IO_BUFFER_SIZE;
        }

        chunk = (length < space) ? (uint16_t)length : space;
        memcpy(&app_config_backup_write_buffer[app_config_backup_write_buffer_length], data, chunk);
        app_config_backup_write_buffer_length = (uint16_t)(app_config_backup_write_buffer_length + chunk);
        data += chunk;
        length -= chunk;
    }

    return 1U;
}

static void AppConfigBackup_ReadBufferBegin(AppSdCardFile_t *file)
{
    app_config_backup_read_file = file;
    app_config_backup_read_buffer_position = 0U;
    app_config_backup_read_buffer_length = 0U;
}

static uint8_t AppConfigBackup_ReadBufferHasMore(void)
{
    return (app_config_backup_read_file
         && (app_config_backup_read_buffer_position < app_config_backup_read_buffer_length
          || app_config_backup_read_file->position < app_config_backup_read_file->file_size)) ? 1U : 0U;
}

static uint8_t AppConfigBackup_ReadBufferedByte(char *ch)
{
    uint32_t remaining;
    uint32_t request;
    uint32_t bytes_read;

    if (!ch || !app_config_backup_read_file)
        return 0U;

    if (app_config_backup_read_buffer_position >= app_config_backup_read_buffer_length)
    {
        if (app_config_backup_read_file->position >= app_config_backup_read_file->file_size)
            return 0U;

        remaining = app_config_backup_read_file->file_size - app_config_backup_read_file->position;
        request = (remaining < APP_CONFIG_BACKUP_IO_BUFFER_SIZE)
            ? remaining
            : APP_CONFIG_BACKUP_IO_BUFFER_SIZE;
        bytes_read = AppSdCard_ReadFile(app_config_backup_read_file,
                                        app_config_backup_read_buffer,
                                        request);
        if (bytes_read == 0UL)
            return 0U;

        app_config_backup_read_buffer_position = 0U;
        app_config_backup_read_buffer_length = (uint16_t)bytes_read;
    }

    *ch = (char)app_config_backup_read_buffer[app_config_backup_read_buffer_position++];
    return 1U;
}

static uint8_t AppConfigBackup_WriteRaw(AppSdCardFile_t *file, const char *text)
{
    uint32_t length;

    (void)file;

    if (!text)
        return 0U;

    length = (uint32_t)strlen(text);
    return AppConfigBackup_WriteBuffered((const uint8_t *)text, length);
}

static uint8_t AppConfigBackup_WriteF(AppSdCardFile_t *file, const char *format, ...)
{
    char line[APP_CONFIG_BACKUP_LINE_BUFFER_SIZE];
    va_list args;
    int length;

    (void)file;

    if (!format)
        return 0U;

    va_start(args, format);
    length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    if (length < 0 || (uint32_t)length >= sizeof(line))
        return 0U;

    return AppConfigBackup_WriteBuffered((const uint8_t *)line, (uint32_t)length);
}

static uint8_t AppConfigBackup_WriteEscaped(AppSdCardFile_t *file, const char *text)
{
    const char *cursor = text ? text : "";

    (void)file;

    while (*cursor)
    {
        char escaped[2];
        char ch = *cursor++;

        if (ch == '"' || ch == '\\')
        {
            escaped[0] = '\\';
            escaped[1] = ch;
            if (!AppConfigBackup_WriteBuffered((const uint8_t *)escaped, sizeof(escaped)))
                return 0U;
            continue;
        }

        if ((unsigned char)ch < 0x20U)
            ch = '?';

        if (!AppConfigBackup_WriteBuffered((const uint8_t *)&ch, 1U))
            return 0U;
    }

    return 1U;
}

static uint8_t AppConfigBackup_WriteQuotedLine(AppSdCardFile_t *file,
                                               const char *key,
                                               const char *value)
{
    return AppConfigBackup_WriteF(file, "%s=\"", key)
        && AppConfigBackup_WriteEscaped(file, value)
        && AppConfigBackup_WriteRaw(file, "\"\r\n");
}

static uint8_t AppConfigBackup_WriteCcPair(AppSdCardFile_t *file,
                                           const char *prefix,
                                           const MidiCC_t *cc)
{
    return AppConfigBackup_WriteF(file,
                                  "%s_cc=%u\r\n%s_value=%u\r\n",
                                  prefix,
                                  cc ? (unsigned)cc->cc : (unsigned)PRESET_CC_NUMBER_UNUSED,
                                  prefix,
                                  cc ? (unsigned)cc->value : (unsigned)PRESET_CC_VALUE_UNUSED);
}

static uint8_t AppConfigBackup_WritePresetCcSlot(AppSdCardFile_t *file,
                                                 const char *prefix,
                                                 uint8_t index,
                                                 const PresetCCSlot_t *slot)
{
    return AppConfigBackup_WriteF(file,
                                  "%s_%u_channel=%u\r\n%s_%u_cc=%u\r\n%s_%u_value=%u\r\n",
                                  prefix,
                                  (unsigned)(index + 1U),
                                  slot ? (unsigned)slot->channel : (unsigned)PRESET_CC_CHANNEL_UNUSED,
                                  prefix,
                                  (unsigned)(index + 1U),
                                  slot ? (unsigned)slot->cc_number : (unsigned)PRESET_CC_NUMBER_UNUSED,
                                  prefix,
                                  (unsigned)(index + 1U),
                                  slot ? (unsigned)slot->value : (unsigned)PRESET_CC_VALUE_UNUSED);
}

static uint8_t AppConfigBackup_WriteProgramMessage(AppSdCardFile_t *file,
                                                   const char *prefix,
                                                   uint8_t index,
                                                   const RuntimeConfigProgramMessage_t *program)
{
    return AppConfigBackup_WriteF(file,
                                  "%s_%u_channel=%u\r\n%s_%u_program=%u\r\n",
                                  prefix,
                                  (unsigned)(index + 1U),
                                  program ? (unsigned)program->channel : (unsigned)PRESET_CC_CHANNEL_UNUSED,
                                  prefix,
                                  (unsigned)(index + 1U),
                                  program ? (unsigned)program->program : (unsigned)PRESET_PROGRAM_NONE);
}

static uint8_t AppConfigBackup_WriteFunctionButton(AppSdCardFile_t *file,
                                                   const RuntimeConfigFunctionButton_t *button)
{
    if (!button)
        return 0U;

    if (!AppConfigBackup_WriteQuotedLine(file, "function_button_name", button->name)
     || !AppConfigBackup_WriteQuotedLine(file, "function_button_active_label", button->active_label)
     || !AppConfigBackup_WriteQuotedLine(file, "function_button_inactive_label", button->inactive_label))
    {
        return 0U;
    }

    for (uint8_t index = 0U; index < RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT; ++index)
    {
        if (!AppConfigBackup_WriteProgramMessage(file,
                                                 "function_button_active_program",
                                                 index,
                                                 &button->active_programs[index]))
        {
            return 0U;
        }
    }

    for (uint8_t index = 0U; index < RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT; ++index)
    {
        if (!AppConfigBackup_WritePresetCcSlot(file,
                                               "function_button_active_cc",
                                               index,
                                               &button->active_cc[index]))
        {
            return 0U;
        }
    }

    for (uint8_t index = 0U; index < RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT; ++index)
    {
        if (!AppConfigBackup_WriteProgramMessage(file,
                                                 "function_button_inactive_program",
                                                 index,
                                                 &button->inactive_programs[index]))
        {
            return 0U;
        }
    }

    for (uint8_t index = 0U; index < RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT; ++index)
    {
        if (!AppConfigBackup_WritePresetCcSlot(file,
                                               "function_button_inactive_cc",
                                               index,
                                               &button->inactive_cc[index]))
        {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t AppConfigBackup_WriteGlobal(AppSdCardFile_t *file,
                                           const RuntimeConfig_t *config)
{
    const RuntimeConfigGlobal_t *global;
    const RuntimeConfigMetronome_t *metronome;

    if (!config)
        return 0U;

    global = &config->global;
    metronome = &config->metronome;

    if (!AppConfigBackup_WriteRaw(file, "\r\n[global]\r\n")
     || !AppConfigBackup_WriteF(file,
                                "startup_delay_seconds=%u\r\nsync_style=%u\r\ndisplay_mode=%u\r\nbacklight_brightness=%u\r\nfeedback_taper_enabled=%u\r\nfeedback_taper_threshold=%u\r\nfeedback_taper_reduce=%u\r\nclock_mode=%u\r\nlive_enc2_mode=%u\r\nexpression_pedal_mode=%u\r\nexpression_pedal_min_raw=%u\r\nexpression_pedal_max_raw=%u\r\nexpression_pedal_invert=%u\r\n",
                                (unsigned)global->startup_delay_seconds,
                                (unsigned)global->sync_style,
                                (unsigned)global->display_mode,
                                (unsigned)global->backlight_brightness,
                                (unsigned)global->feedback_taper_enabled,
                                (unsigned)global->feedback_taper_threshold,
                                (unsigned)global->feedback_taper_reduce,
                                (unsigned)global->clock_mode,
                                (unsigned)global->live_enc2_mode,
                                (unsigned)global->expression_pedal_mode,
                                (unsigned)global->expression_pedal_min_raw,
                                (unsigned)global->expression_pedal_max_raw,
                                (unsigned)global->expression_pedal_invert))
    {
        return 0U;
    }

    for (uint8_t index = 0U; index < RUNTIME_CONFIG_EXPRESSION_PEDAL_CC_SLOT_COUNT; ++index)
    {
        const RuntimeConfigExpressionPedalCcSlot_t *slot = &global->expression_pedal_cc_slots[index];

        if (!AppConfigBackup_WriteF(file,
                                    "expression_pedal_cc_%u=%u\r\nexpression_pedal_heel_%u=%u\r\nexpression_pedal_toe_%u=%u\r\n",
                                    (unsigned)(index + 1U),
                                    (unsigned)slot->cc,
                                    (unsigned)(index + 1U),
                                    (unsigned)slot->heel_value,
                                    (unsigned)(index + 1U),
                                    (unsigned)slot->toe_value))
        {
            return 0U;
        }
    }

    return AppConfigBackup_WriteRaw(file, "\r\n[metronome]\r\n")
        && AppConfigBackup_WriteF(file,
                                  "volume=%u\r\npitch=%u\r\nbeats_per_bar=%u\r\nrhythm=%u\r\n",
                                  (unsigned)metronome->volume,
                                  (unsigned)metronome->pitch,
                                  (unsigned)metronome->beats_per_bar,
                                  (unsigned)metronome->rhythm);
}

static uint8_t AppConfigBackup_WriteDevices(AppSdCardFile_t *file,
                                            const RuntimeConfig_t *config)
{
    if (!config)
        return 0U;

    for (uint8_t index = 0U; index < MIDI_DEVICE_COUNT; ++index)
    {
        const RuntimeConfigDevice_t *device = &config->devices[index];

        if (!AppConfigBackup_WriteF(file, "\r\n[device %u]\r\nindex=%u\r\n", (unsigned)(index + 1U), (unsigned)index)
         || !AppConfigBackup_WriteQuotedLine(file, "name", device->name)
         || !AppConfigBackup_WriteF(file,
                                    "channel=%u\r\nmax_preset=%u\r\n",
                                    (unsigned)device->channel,
                                    (unsigned)device->max_preset)
         || !AppConfigBackup_WriteCcPair(file, "active", &device->active))
        {
            return 0U;
        }

        for (uint8_t auto_index = 0U; auto_index < RUNTIME_CONFIG_DEVICE_AUTO_CC_COUNT; ++auto_index)
        {
            if (!AppConfigBackup_WritePresetCcSlot(file,
                                                   "active_auto_cc",
                                                   auto_index,
                                                   &device->active_auto_cc[auto_index]))
            {
                return 0U;
            }
        }

        if (!AppConfigBackup_WriteCcPair(file, "bypass", &device->bypass))
        {
            return 0U;
        }

        for (uint8_t auto_index = 0U; auto_index < RUNTIME_CONFIG_DEVICE_AUTO_CC_COUNT; ++auto_index)
        {
            if (!AppConfigBackup_WritePresetCcSlot(file,
                                                   "bypass_auto_cc",
                                                   auto_index,
                                                   &device->bypass_auto_cc[auto_index]))
            {
                return 0U;
            }
        }

        if (!AppConfigBackup_WriteCcPair(file, "tap_tempo", &device->tap_tempo)
         || !AppConfigBackup_WriteCcPair(file, "volume1", &device->volume1)
         || !AppConfigBackup_WriteCcPair(file, "volume2", &device->volume2)
         || !AppConfigBackup_WriteCcPair(file, "mix1", &device->mix1)
         || !AppConfigBackup_WriteCcPair(file, "mix2", &device->mix2)
         || !AppConfigBackup_WriteCcPair(file, "decay1", &device->decay1)
         || !AppConfigBackup_WriteCcPair(file, "decay2", &device->decay2))
        {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t AppConfigBackup_WriteBanks(AppSdCardFile_t *file,
                                          const RuntimeConfig_t *config)
{
    if (!config)
        return 0U;

    for (uint8_t index = 0U; index < PRESET_BANK_COUNT; ++index)
    {
        const RuntimeConfigBank_t *bank = &config->banks[index];

        if (!AppConfigBackup_WriteF(file, "\r\n[bank %u]\r\nindex=%u\r\n", (unsigned)(index + 1U), (unsigned)index)
         || !AppConfigBackup_WriteQuotedLine(file, "name", bank->name)
         || !AppConfigBackup_WriteF(file,
                                    "wet_dry_enabled=%u\r\nmidi_clock_bar_count=%u\r\n",
                                    (unsigned)bank->wet_dry_enabled,
                                    (unsigned)bank->midi_clock_bar_count)
         || !AppConfigBackup_WriteFunctionButton(file, &bank->function_button))
        {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t AppConfigBackup_WritePreset(AppSdCardFile_t *file,
                                           const char *section_name,
                                           const Preset_t *preset,
                                           uint8_t index,
                                           uint8_t bank,
                                           uint8_t slot)
{
    if (!preset || !section_name)
        return 0U;

    if (!AppConfigBackup_WriteF(file,
                                "\r\n[%s]\r\nindex=%u\r\nbank=%u\r\nslot=%u\r\n",
                                section_name,
                                (unsigned)index,
                                (unsigned)bank,
                                (unsigned)slot)
     || !AppConfigBackup_WriteQuotedLine(file, "name", preset->name))
    {
        return 0U;
    }

    for (uint8_t device = 0U; device < PRESET_DEVICE_SLOTS; ++device)
    {
        if (!AppConfigBackup_WriteF(file,
                                    "program_device_%u=%u\r\n",
                                    (unsigned)(device + 1U),
                                    (unsigned)preset->prg[device].program))
        {
            return 0U;
        }
    }

    for (uint8_t cc_index = 0U; cc_index < PRESET_CC_SLOT_COUNT; ++cc_index)
    {
        if (!AppConfigBackup_WritePresetCcSlot(file, "cc", cc_index, &preset->cc[cc_index]))
            return 0U;
    }

    for (uint8_t relay_index = 0U; relay_index < PRESET_RELAY_COUNT; ++relay_index)
    {
        if (!AppConfigBackup_WriteF(file,
                                    "relay_%u=%u\r\n",
                                    (unsigned)(relay_index + 1U),
                                    (unsigned)preset->relay[relay_index]))
        {
            return 0U;
        }
    }

    return AppConfigBackup_WriteFunctionButton(file, &preset->function_button);
}

static uint8_t AppConfigBackup_WritePresets(AppSdCardFile_t *file,
                                            const RuntimeConfig_t *config)
{
    if (!config)
        return 0U;

    for (uint8_t index = 0U; index < PRESET_COUNT; ++index)
    {
        char section[24];
        uint8_t bank = (uint8_t)(index / PRESETS_PER_BANK);
        uint8_t slot = (uint8_t)(index % PRESETS_PER_BANK);

        snprintf(section, sizeof(section), "preset %u.%u", (unsigned)(bank + 1U), (unsigned)(slot + 1U));
        if (!AppConfigBackup_WritePreset(file, section, Presets_Get(index), index, bank, slot))
            return 0U;
    }

    return AppConfigBackup_WritePreset(file,
                                       "global_preset bypass",
                                       &config->global_bypass_preset,
                                       PRESET_GLOBAL_BYPASS_INDEX,
                                       0U,
                                       0U)
        && AppConfigBackup_WritePreset(file,
                                       "global_preset mute",
                                       &config->global_mute_preset,
                                       PRESET_GLOBAL_MUTE_INDEX,
                                       0U,
                                       0U);
}

static uint8_t AppConfigBackup_WriteUserThemes(AppSdCardFile_t *file,
                                               const RuntimeConfig_t *config)
{
    if (!config)
        return 0U;

    for (uint8_t index = 0U; index < RUNTIME_CONFIG_USER_THEME_COUNT; ++index)
    {
        const RuntimeConfigUserTheme_t *theme = &config->user_themes[index];

        if (!AppConfigBackup_WriteF(file,
                                    "\r\n[user_theme %u]\r\ndisplay_bg_colour=0x%04X\r\nmain_footbar_color=0x%04X\r\nmain_footbar_text_colour=0x%04X\r\nmain_info_text_colour=0x%04X\r\nmain_info_edit_cursor_text_colour=0x%04X\r\nmain_info_edit_cursor_bg_colour=0x%04X\r\nmain_info_edit_cursor_shared_bg_colour=0x%04X\r\nmain_saving_popup_bg_colour=0x%04X\r\nmain_saving_popup_text_colour=0x%04X\r\nmain_saving_popup_border_colour=0x%04X\r\nmain_mode_header_colour=0x%04X\r\nmain_mode_header_edit_colour=0x%04X\r\nmain_mode_header_edit_bg_colour=0x%04X\r\nmain_preset_colour=0x%04X\r\nmain_bank_colour=0x%04X\r\nmain_bank_wet_dry_colour=0x%04X\r\nmain_special_function_button_active_colour=0x%04X\r\nmain_special_function_button_inactive_colour=0x%04X\r\nmain_special_function_button_active_bg=0x%04X\r\nmain_alert_badge_text_colour=0x%04X\r\nbpm_internal_colour=0x%04X\r\next_bpm_colour=0x%04X\r\n",
                                    (unsigned)(index + 1U),
                                    (unsigned)theme->display_bg_colour,
                                    (unsigned)theme->main_footbar_color,
                                    (unsigned)theme->main_footbar_text_colour,
                                    (unsigned)theme->main_info_text_colour,
                                    (unsigned)theme->main_info_edit_cursor_text_colour,
                                    (unsigned)theme->main_info_edit_cursor_bg_colour,
                                    (unsigned)theme->main_info_edit_cursor_shared_bg_colour,
                                    (unsigned)theme->main_saving_popup_bg_colour,
                                    (unsigned)theme->main_saving_popup_text_colour,
                                    (unsigned)theme->main_saving_popup_border_colour,
                                    (unsigned)theme->main_mode_header_colour,
                                    (unsigned)theme->main_mode_header_edit_colour,
                                    (unsigned)theme->main_mode_header_edit_bg_colour,
                                    (unsigned)theme->main_preset_colour,
                                    (unsigned)theme->main_bank_colour,
                                    (unsigned)theme->main_bank_wet_dry_colour,
                                    (unsigned)theme->main_special_function_button_active_colour,
                                    (unsigned)theme->main_special_function_button_inactive_colour,
                                    (unsigned)theme->main_special_function_button_active_bg,
                                    (unsigned)theme->main_alert_badge_text_colour,
                                    (unsigned)theme->bpm_internal_colour,
                                    (unsigned)theme->ext_bpm_colour))
        {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t AppConfigBackup_WriteSnapshotContents(AppSdCardFile_t *file,
                                                     const RuntimeConfig_t *config,
                                                     const char *reason)
{
    return AppConfigBackup_WriteRaw(file, "# ChatTest human-readable backup\r\n")
        && AppConfigBackup_WriteRaw(file, "# 255 means unused for MIDI channels, CC numbers, CC values, and programs.\r\n")
        && AppConfigBackup_WriteF(file,
                                  "format_version=1\r\nreason=\"%s\"\r\npreset_count=%u\r\npreset_bank_count=%u\r\npresets_per_bank=%u\r\nmidi_device_count=%u\r\n",
                                  reason ? reason : "manual",
                                  (unsigned)PRESET_COUNT,
                                  (unsigned)PRESET_BANK_COUNT,
                                  (unsigned)PRESETS_PER_BANK,
                                  (unsigned)MIDI_DEVICE_COUNT)
        && AppConfigBackup_WriteGlobal(file, config)
        && AppConfigBackup_WriteDevices(file, config)
        && AppConfigBackup_WriteBanks(file, config)
        && AppConfigBackup_WritePresets(file, config)
        && AppConfigBackup_WriteUserThemes(file, config);
}

static uint8_t AppConfigBackup_WriteSnapshotFile(const char *filename,
                                                 const RuntimeConfig_t *config,
                                                 const char *reason,
                                                 uint32_t *bytes_written)
{
    AppSdCardFile_t file;
    uint8_t ok;
    uint32_t file_start_tick = HAL_GetTick();
    uint32_t stage_tick;

    if (bytes_written)
        *bytes_written = 0UL;

    stage_tick = HAL_GetTick();
    if (!AppSdCard_OpenFileForWrite(&file, filename))
    {
        AppConfigBackup_LogFileStage("backup", filename, "open_write", stage_tick, 0U);
        AppConfigBackup_LogFileStage("backup", filename, "file_total", file_start_tick, 0U);
        return 0U;
    }
    AppConfigBackup_LogFileStage("backup", filename, "open_write", stage_tick, 1U);

    AppConfigBackup_WriteBufferBegin(&file);
    stage_tick = HAL_GetTick();
    ok = AppConfigBackup_WriteSnapshotContents(&file, config, reason);
    AppConfigBackup_LogFileStage("backup", filename, "write_contents", stage_tick, ok);

    stage_tick = HAL_GetTick();
    if (!AppConfigBackup_WriteBufferFlush())
        ok = 0U;
    AppConfigBackup_LogFileStage("backup", filename, "flush", stage_tick, ok);

    stage_tick = HAL_GetTick();
    if (!AppSdCard_CloseWrittenFile(&file))
        ok = 0U;
    AppConfigBackup_LogFileStage("backup", filename, "close", stage_tick, ok);

    app_config_backup_write_file = NULL;

    if (bytes_written)
        *bytes_written = file.file_size;

    printf("SDBACKUP_MON op=backup file=%s stage=file_total ms=%lu ok=%u bytes=%lu\r\n",
           filename ? filename : "?",
           (unsigned long)AppConfigBackup_ElapsedMs(file_start_tick),
           (unsigned)ok,
           (unsigned long)file.file_size);
    return ok;
}

static char *AppConfigBackup_SkipSpaces(char *text)
{
    while (text && (*text == ' ' || *text == '\t'))
        ++text;

    return text;
}

static void AppConfigBackup_TrimRight(char *text)
{
    size_t length;

    if (!text)
        return;

    length = strlen(text);
    while (length > 0U)
    {
        char ch = text[length - 1U];

        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
            break;

        text[--length] = '\0';
    }
}

static uint8_t AppConfigBackup_ParseUnsignedValue(const char *text, uint32_t *value_out)
{
    uint32_t value = 0UL;
    uint8_t base = 10U;
    uint8_t digits = 0U;

    if (!text || !value_out)
        return 0U;

    while (*text == ' ' || *text == '\t')
        ++text;

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        base = 16U;
        text += 2;
    }

    while (*text)
    {
        uint8_t digit;
        char ch = *text;

        if (ch >= '0' && ch <= '9')
            digit = (uint8_t)(ch - '0');
        else if (base == 16U && ch >= 'a' && ch <= 'f')
            digit = (uint8_t)(10U + (uint8_t)(ch - 'a'));
        else if (base == 16U && ch >= 'A' && ch <= 'F')
            digit = (uint8_t)(10U + (uint8_t)(ch - 'A'));
        else
            break;

        if (digit >= base)
            break;

        value = (value * (uint32_t)base) + digit;
        ++digits;
        ++text;
    }

    while (*text == ' ' || *text == '\t')
        ++text;

    if (digits == 0U || *text != '\0')
        return 0U;

    *value_out = value;
    return 1U;
}

static uint8_t AppConfigBackup_ParseQuotedValue(const char *text,
                                                char *destination,
                                                size_t destination_size)
{
    size_t write_index = 0U;

    if (!text || !destination || destination_size == 0U || *text != '"')
        return 0U;

    ++text;
    while (*text && *text != '"')
    {
        char ch = *text++;

        if (ch == '\\' && *text)
            ch = *text++;

        if (write_index + 1U < destination_size)
            destination[write_index++] = ch;
    }

    if (*text != '"')
        return 0U;

    ++text;
    while (*text == ' ' || *text == '\t')
        ++text;

    if (*text != '\0')
        return 0U;

    destination[write_index] = '\0';
    return 1U;
}

static uint8_t AppConfigBackup_ParseIndexedKey(const char *key,
                                               const char *prefix,
                                               const char *suffix,
                                               uint8_t max_count,
                                               uint8_t *index_out)
{
    size_t prefix_length;
    size_t suffix_length;
    uint32_t value = 0UL;
    uint8_t digits = 0U;

    if (!key || !prefix || !suffix || !index_out || max_count == 0U)
        return 0U;

    prefix_length = strlen(prefix);
    suffix_length = strlen(suffix);
    if (strncmp(key, prefix, prefix_length) != 0)
        return 0U;

    key += prefix_length;
    while (*key >= '0' && *key <= '9')
    {
        value = (value * 10UL) + (uint32_t)(*key - '0');
        ++digits;
        ++key;
    }

    if (digits == 0U || value == 0UL || value > max_count)
        return 0U;

    if (strncmp(key, suffix, suffix_length) != 0 || key[suffix_length] != '\0')
        return 0U;

    *index_out = (uint8_t)(value - 1UL);
    return 1U;
}

static uint8_t AppConfigBackup_ParseOneBasedSectionIndex(const char *section,
                                                         const char *prefix,
                                                         uint8_t max_count,
                                                         uint8_t *index_out)
{
    uint32_t value = 0UL;
    uint8_t digits = 0U;
    size_t prefix_length;

    if (!section || !prefix || !index_out || max_count == 0U)
        return 0U;

    prefix_length = strlen(prefix);
    if (strncmp(section, prefix, prefix_length) != 0)
        return 0U;

    section += prefix_length;
    while (*section >= '0' && *section <= '9')
    {
        value = (value * 10UL) + (uint32_t)(*section - '0');
        ++digits;
        ++section;
    }

    if (digits == 0U || *section != '\0' || value == 0UL || value > max_count)
        return 0U;

    *index_out = (uint8_t)(value - 1UL);
    return 1U;
}

static uint8_t AppConfigBackup_ParsePresetSectionIndex(const char *section,
                                                       uint8_t *index_out)
{
    uint32_t bank = 0UL;
    uint32_t slot = 0UL;
    uint8_t digits = 0U;

    if (!section || !index_out || strncmp(section, "preset ", 7U) != 0)
        return 0U;

    section += 7U;
    while (*section >= '0' && *section <= '9')
    {
        bank = (bank * 10UL) + (uint32_t)(*section - '0');
        ++digits;
        ++section;
    }

    if (digits == 0U || *section != '.')
        return 0U;

    ++section;
    digits = 0U;
    while (*section >= '0' && *section <= '9')
    {
        slot = (slot * 10UL) + (uint32_t)(*section - '0');
        ++digits;
        ++section;
    }

    if (digits == 0U
     || *section != '\0'
     || bank == 0UL
     || bank > PRESET_BANK_COUNT
     || slot == 0UL
     || slot > PRESETS_PER_BANK)
    {
        return 0U;
    }

    *index_out = (uint8_t)(((bank - 1UL) * PRESETS_PER_BANK) + (slot - 1UL));
    return 1U;
}

static uint8_t AppConfigBackup_ReadLine(AppSdCardFile_t *file,
                                        char *line,
                                        size_t line_size,
                                        uint8_t *line_complete)
{
    size_t write_index = 0U;

    if (!file || !line || line_size == 0U || !line_complete)
        return 0U;

    *line_complete = 1U;
    (void)file;

    while (AppConfigBackup_ReadBufferHasMore())
    {
        char ch;

        if (!AppConfigBackup_ReadBufferedByte(&ch))
            return 0U;

        if (ch == '\n')
            break;

        if (ch == '\r')
            continue;

        if (write_index + 1U < line_size)
            line[write_index++] = ch;
        else
            *line_complete = 0U;
    }

    line[write_index] = '\0';
    return 1U;
}

static void AppConfigBackup_RestoreContextInit(AppConfigBackupRestoreContext_t *context)
{
    const RuntimeConfig_t *current_config;

    if (!context)
        return;

    current_config = RuntimeConfig_Get();
    if (current_config)
        app_config_backup_restore_config = *current_config;
    else
        memset(&app_config_backup_restore_config, 0, sizeof(app_config_backup_restore_config));

    for (uint8_t index = 0U; index < PRESET_COUNT; ++index)
    {
        const Preset_t *preset = Presets_Get(index);
        if (preset)
            app_config_backup_restore_presets[index] = *preset;
        else
            memset(&app_config_backup_restore_presets[index], 0, sizeof(app_config_backup_restore_presets[index]));
    }

    memset(context, 0, sizeof(*context));
    context->config = &app_config_backup_restore_config;
    context->presets = app_config_backup_restore_presets;
}

static uint8_t AppConfigBackup_SetRestoreSection(AppConfigBackupRestoreContext_t *context,
                                                 char *line)
{
    char *section;
    size_t length;
    uint8_t index = 0U;

    if (!context || !line || line[0] != '[')
        return 0U;

    section = &line[1];
    length = strlen(section);
    if (length == 0U || section[length - 1U] != ']')
        return 0U;

    section[length - 1U] = '\0';
    context->section = APP_CONFIG_BACKUP_RESTORE_SECTION_TOP;
    context->index = 0U;

    if (strcmp(section, "global") == 0)
        context->section = APP_CONFIG_BACKUP_RESTORE_SECTION_GLOBAL;
    else if (strcmp(section, "metronome") == 0)
        context->section = APP_CONFIG_BACKUP_RESTORE_SECTION_METRONOME;
    else if (AppConfigBackup_ParseOneBasedSectionIndex(section, "device ", MIDI_DEVICE_COUNT, &index))
    {
        context->section = APP_CONFIG_BACKUP_RESTORE_SECTION_DEVICE;
        context->index = index;
    }
    else if (AppConfigBackup_ParseOneBasedSectionIndex(section, "bank ", PRESET_BANK_COUNT, &index))
    {
        context->section = APP_CONFIG_BACKUP_RESTORE_SECTION_BANK;
        context->index = index;
    }
    else if (AppConfigBackup_ParsePresetSectionIndex(section, &index))
    {
        context->section = APP_CONFIG_BACKUP_RESTORE_SECTION_PRESET;
        context->index = index;
    }
    else if (strcmp(section, "global_preset bypass") == 0)
        context->section = APP_CONFIG_BACKUP_RESTORE_SECTION_GLOBAL_BYPASS;
    else if (strcmp(section, "global_preset mute") == 0)
        context->section = APP_CONFIG_BACKUP_RESTORE_SECTION_GLOBAL_MUTE;
    else if (AppConfigBackup_ParseOneBasedSectionIndex(section,
                                                       "user_theme ",
                                                       RUNTIME_CONFIG_USER_THEME_COUNT,
                                                       &index))
    {
        context->section = APP_CONFIG_BACKUP_RESTORE_SECTION_USER_THEME;
        context->index = index;
    }

    return 1U;
}

static uint8_t AppConfigBackup_ClampU8(uint32_t value, uint8_t max_value)
{
    return (value > max_value) ? max_value : (uint8_t)value;
}

static uint16_t AppConfigBackup_ClampU16(uint32_t value, uint16_t max_value)
{
    return (value > max_value) ? max_value : (uint16_t)value;
}

static uint8_t AppConfigBackup_NormalizeBool(uint32_t value)
{
    return value ? 1U : 0U;
}

static uint8_t AppConfigBackup_NormalizeDeviceChannel(uint32_t value)
{
    return (value > 16UL) ? 16U : (uint8_t)value;
}

static uint8_t AppConfigBackup_NormalizeMessageChannel(uint32_t value)
{
    if (value == PRESET_CC_CHANNEL_UNUSED)
        return PRESET_CC_CHANNEL_UNUSED;

    if (value == 0UL)
        return 1U;

    return (value > 16UL) ? 16U : (uint8_t)value;
}

static uint8_t AppConfigBackup_NormalizeMidi7OrUnused(uint32_t value)
{
    if (value == PRESET_CC_NUMBER_UNUSED)
        return PRESET_CC_NUMBER_UNUSED;

    return (value > 127UL) ? 127U : (uint8_t)value;
}

static uint8_t AppConfigBackup_ParseAndMark(AppConfigBackupRestoreContext_t *context,
                                            const char *value_text,
                                            uint32_t *value_out)
{
    if (!AppConfigBackup_ParseUnsignedValue(value_text, value_out))
        return 0U;

    if (context)
        ++context->parsed_values;

    return 1U;
}

static uint8_t AppConfigBackup_ParseStringAndMark(AppConfigBackupRestoreContext_t *context,
                                                  const char *value_text,
                                                  char *destination,
                                                  size_t destination_size)
{
    if (!AppConfigBackup_ParseQuotedValue(value_text, destination, destination_size))
        return 0U;

    if (context)
        ++context->parsed_values;

    return 1U;
}

static uint8_t AppConfigBackup_ApplyMidiCcPairKey(AppConfigBackupRestoreContext_t *context,
                                                  MidiCC_t *cc,
                                                  const char *prefix,
                                                  const char *key,
                                                  const char *value_text,
                                                  uint8_t *handled)
{
    size_t prefix_length;
    uint32_t value;

    if (!cc || !prefix || !key || !value_text || !handled)
        return 0U;

    prefix_length = strlen(prefix);
    if (strncmp(key, prefix, prefix_length) != 0)
        return 1U;

    if (strcmp(&key[prefix_length], "_cc") == 0)
    {
        *handled = 1U;
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;

        cc->cc = AppConfigBackup_NormalizeMidi7OrUnused(value);
        return 1U;
    }

    if (strcmp(&key[prefix_length], "_value") == 0)
    {
        *handled = 1U;
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;

        cc->value = AppConfigBackup_NormalizeMidi7OrUnused(value);
        return 1U;
    }

    return 1U;
}

static uint8_t AppConfigBackup_ApplyPresetCcSlotKey(AppConfigBackupRestoreContext_t *context,
                                                    PresetCCSlot_t *slots,
                                                    uint8_t slot_count,
                                                    const char *prefix,
                                                    const char *key,
                                                    const char *value_text,
                                                    uint8_t *handled)
{
    uint8_t index;
    uint32_t value;

    if (!slots || !prefix || !key || !value_text || !handled)
        return 0U;

    if (AppConfigBackup_ParseIndexedKey(key, prefix, "_channel", slot_count, &index))
    {
        *handled = 1U;
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;

        slots[index].channel = AppConfigBackup_NormalizeMessageChannel(value);
        return 1U;
    }

    if (AppConfigBackup_ParseIndexedKey(key, prefix, "_cc", slot_count, &index))
    {
        *handled = 1U;
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;

        slots[index].cc_number = AppConfigBackup_NormalizeMidi7OrUnused(value);
        return 1U;
    }

    if (AppConfigBackup_ParseIndexedKey(key, prefix, "_value", slot_count, &index))
    {
        *handled = 1U;
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;

        slots[index].value = AppConfigBackup_NormalizeMidi7OrUnused(value);
        return 1U;
    }

    return 1U;
}

static uint8_t AppConfigBackup_ApplyProgramMessageKey(AppConfigBackupRestoreContext_t *context,
                                                      RuntimeConfigProgramMessage_t *programs,
                                                      uint8_t program_count,
                                                      const char *prefix,
                                                      const char *key,
                                                      const char *value_text,
                                                      uint8_t *handled)
{
    uint8_t index;
    uint32_t value;

    if (!programs || !prefix || !key || !value_text || !handled)
        return 0U;

    if (AppConfigBackup_ParseIndexedKey(key, prefix, "_channel", program_count, &index))
    {
        *handled = 1U;
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;

        programs[index].channel = AppConfigBackup_NormalizeMessageChannel(value);
        return 1U;
    }

    if (AppConfigBackup_ParseIndexedKey(key, prefix, "_program", program_count, &index))
    {
        *handled = 1U;
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;

        programs[index].program = AppConfigBackup_NormalizeMidi7OrUnused(value);
        return 1U;
    }

    return 1U;
}

static uint8_t AppConfigBackup_ApplyFunctionButtonKey(AppConfigBackupRestoreContext_t *context,
                                                      RuntimeConfigFunctionButton_t *button,
                                                      const char *key,
                                                      const char *value_text,
                                                      uint8_t *handled)
{
    if (!button || !key || !value_text || !handled)
        return 0U;

    if (strcmp(key, "function_button_name") == 0)
    {
        *handled = 1U;
        return AppConfigBackup_ParseStringAndMark(context,
                                                  value_text,
                                                  button->name,
                                                  sizeof(button->name));
    }

    if (strcmp(key, "function_button_active_label") == 0)
    {
        *handled = 1U;
        return AppConfigBackup_ParseStringAndMark(context,
                                                  value_text,
                                                  button->active_label,
                                                  sizeof(button->active_label));
    }

    if (strcmp(key, "function_button_inactive_label") == 0)
    {
        *handled = 1U;
        return AppConfigBackup_ParseStringAndMark(context,
                                                  value_text,
                                                  button->inactive_label,
                                                  sizeof(button->inactive_label));
    }

    if (!AppConfigBackup_ApplyProgramMessageKey(context,
                                                button->active_programs,
                                                RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT,
                                                "function_button_active_program_",
                                                key,
                                                value_text,
                                                handled))
    {
        return 0U;
    }

    if (*handled)
        return 1U;

    if (!AppConfigBackup_ApplyPresetCcSlotKey(context,
                                              button->active_cc,
                                              RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT,
                                              "function_button_active_cc_",
                                              key,
                                              value_text,
                                              handled))
    {
        return 0U;
    }

    if (*handled)
        return 1U;

    if (!AppConfigBackup_ApplyProgramMessageKey(context,
                                                button->inactive_programs,
                                                RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT,
                                                "function_button_inactive_program_",
                                                key,
                                                value_text,
                                                handled))
    {
        return 0U;
    }

    if (*handled)
        return 1U;

    return AppConfigBackup_ApplyPresetCcSlotKey(context,
                                                button->inactive_cc,
                                                RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT,
                                                "function_button_inactive_cc_",
                                                key,
                                                value_text,
                                                handled);
}

static uint8_t AppConfigBackup_ApplyTopLevelKey(AppConfigBackupRestoreContext_t *context,
                                                const char *key,
                                                const char *value_text)
{
    uint32_t value;

    if (!context || !key || !value_text)
        return 0U;

    if (strcmp(key, "format_version") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;

        if (value != 1UL)
            return 0U;

        context->format_version_seen = 1U;
        return 1U;
    }

    if (strcmp(key, "preset_count") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;

        return (value == PRESET_COUNT) ? 1U : 0U;
    }

    if (strcmp(key, "preset_bank_count") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;

        return (value == PRESET_BANK_COUNT) ? 1U : 0U;
    }

    if (strcmp(key, "presets_per_bank") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;

        return (value == PRESETS_PER_BANK) ? 1U : 0U;
    }

    if (strcmp(key, "midi_device_count") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;

        return (value == MIDI_DEVICE_COUNT) ? 1U : 0U;
    }

    return 1U;
}

static uint8_t AppConfigBackup_ApplyGlobalKey(AppConfigBackupRestoreContext_t *context,
                                              const char *key,
                                              const char *value_text)
{
    RuntimeConfigGlobal_t *global;
    uint8_t index;
    uint32_t value;

    if (!context || !context->config || !key || !value_text)
        return 0U;

    global = &context->config->global;

    if (strcmp(key, "startup_delay_seconds") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        global->startup_delay_seconds = AppConfigBackup_ClampU8(value, RUNTIME_CONFIG_GLOBAL_STARTUP_DELAY_MAX);
        return 1U;
    }

    if (strcmp(key, "sync_style") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        global->sync_style = (value > RUNTIME_CONFIG_SYNC_STYLE_TAP_TEMPO_CC)
            ? RUNTIME_CONFIG_SYNC_STYLE_MIDI_CLOCK
            : (RuntimeConfigSyncStyle_t)value;
        return 1U;
    }

    if (strcmp(key, "display_mode") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        global->display_mode = RuntimeConfig_NormalizeDisplayMode((uint8_t)value);
        return 1U;
    }

    if (strcmp(key, "backlight_brightness") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        if (value < RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MIN)
            value = RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MIN;
        global->backlight_brightness = AppConfigBackup_ClampU16(value, RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MAX);
        return 1U;
    }

    if (strcmp(key, "feedback_taper_enabled") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        global->feedback_taper_enabled = AppConfigBackup_NormalizeBool(value);
        return 1U;
    }

    if (strcmp(key, "feedback_taper_threshold") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        global->feedback_taper_threshold = AppConfigBackup_ClampU8(value, RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_THRESHOLD_MAX);
        return 1U;
    }

    if (strcmp(key, "feedback_taper_reduce") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        global->feedback_taper_reduce = AppConfigBackup_ClampU8(value, RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_REDUCE_MAX);
        return 1U;
    }

    if (strcmp(key, "clock_mode") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        global->clock_mode = (value > RUNTIME_CONFIG_CLOCK_MODE_MASTER)
            ? RUNTIME_CONFIG_CLOCK_MODE_MONITOR
            : (RuntimeConfigClockMode_t)value;
        return 1U;
    }

    if (strcmp(key, "live_enc2_mode") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        global->live_enc2_mode = (value > RUNTIME_CONFIG_LIVE_ENC2_MODE_TIMEBEND)
            ? RUNTIME_CONFIG_LIVE_ENC2_MODE_PRESET_BANK_SCROLL
            : (RuntimeConfigLiveEnc2Mode_t)value;
        return 1U;
    }

    if (strcmp(key, "expression_pedal_mode") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        global->expression_pedal_mode = (value > RUNTIME_CONFIG_EXPRESSION_PEDAL_MODE_TIMEBEND)
            ? RUNTIME_CONFIG_EXPRESSION_PEDAL_MODE_DISABLED
            : (RuntimeConfigExpressionPedalMode_t)value;
        return 1U;
    }

    if (strcmp(key, "expression_pedal_min_raw") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        global->expression_pedal_min_raw = AppConfigBackup_ClampU16(value, RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MAX);
        return 1U;
    }

    if (strcmp(key, "expression_pedal_max_raw") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        global->expression_pedal_max_raw = AppConfigBackup_ClampU16(value, RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MAX);
        return 1U;
    }

    if (strcmp(key, "expression_pedal_invert") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        global->expression_pedal_invert = AppConfigBackup_NormalizeBool(value);
        return 1U;
    }

    if (AppConfigBackup_ParseIndexedKey(key,
                                        "expression_pedal_cc_",
                                        "",
                                        RUNTIME_CONFIG_EXPRESSION_PEDAL_CC_SLOT_COUNT,
                                        &index))
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        global->expression_pedal_cc_slots[index].cc = AppConfigBackup_NormalizeMidi7OrUnused(value);
        return 1U;
    }

    if (AppConfigBackup_ParseIndexedKey(key,
                                        "expression_pedal_heel_",
                                        "",
                                        RUNTIME_CONFIG_EXPRESSION_PEDAL_CC_SLOT_COUNT,
                                        &index))
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        global->expression_pedal_cc_slots[index].heel_value = AppConfigBackup_NormalizeMidi7OrUnused(value);
        return 1U;
    }

    if (AppConfigBackup_ParseIndexedKey(key,
                                        "expression_pedal_toe_",
                                        "",
                                        RUNTIME_CONFIG_EXPRESSION_PEDAL_CC_SLOT_COUNT,
                                        &index))
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        global->expression_pedal_cc_slots[index].toe_value = AppConfigBackup_NormalizeMidi7OrUnused(value);
        return 1U;
    }

    return 1U;
}

static uint8_t AppConfigBackup_ApplyMetronomeKey(AppConfigBackupRestoreContext_t *context,
                                                 const char *key,
                                                 const char *value_text)
{
    RuntimeConfigMetronome_t *metronome;
    uint32_t value;

    if (!context || !context->config || !key || !value_text)
        return 0U;

    metronome = &context->config->metronome;

    if (strcmp(key, "volume") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        metronome->volume = AppConfigBackup_ClampU8(value, RUNTIME_CONFIG_METRONOME_VOLUME_MAX);
        return 1U;
    }

    if (strcmp(key, "pitch") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        metronome->pitch = (value > RUNTIME_CONFIG_METRONOME_PITCH_HIGH)
            ? RUNTIME_CONFIG_METRONOME_PITCH_MID
            : (RuntimeConfigMetronomePitch_t)value;
        return 1U;
    }

    if (strcmp(key, "beats_per_bar") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        if (value < RUNTIME_CONFIG_METRONOME_BEATS_PER_BAR_MIN)
            value = RUNTIME_CONFIG_METRONOME_BEATS_PER_BAR_MIN;
        metronome->beats_per_bar = AppConfigBackup_ClampU8(value, RUNTIME_CONFIG_METRONOME_BEATS_PER_BAR_MAX);
        return 1U;
    }

    if (strcmp(key, "rhythm") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        metronome->rhythm = (value > RUNTIME_CONFIG_METRONOME_RHYTHM_FOUR_EIGHT)
            ? RUNTIME_CONFIG_METRONOME_RHYTHM_QUARTER_NOTES
            : (RuntimeConfigMetronomeRhythm_t)value;
        return 1U;
    }

    return 1U;
}

static uint8_t AppConfigBackup_ApplyDeviceKey(AppConfigBackupRestoreContext_t *context,
                                              const char *key,
                                              const char *value_text)
{
    RuntimeConfigDevice_t *device;
    uint8_t handled = 0U;
    uint32_t value;

    if (!context || !context->config || context->index >= MIDI_DEVICE_COUNT || !key || !value_text)
        return 0U;

    device = &context->config->devices[context->index];

    if (strcmp(key, "index") == 0)
        return 1U;

    if (strcmp(key, "name") == 0)
        return AppConfigBackup_ParseStringAndMark(context, value_text, device->name, sizeof(device->name));

    if (strcmp(key, "channel") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        device->channel = AppConfigBackup_NormalizeDeviceChannel(value);
        return 1U;
    }

    if (strcmp(key, "max_preset") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        device->max_preset = AppConfigBackup_ClampU8(value, 127U);
        return 1U;
    }

    if (!AppConfigBackup_ApplyMidiCcPairKey(context, &device->active, "active", key, value_text, &handled))
        return 0U;
    if (handled)
        return 1U;

    if (!AppConfigBackup_ApplyPresetCcSlotKey(context,
                                              device->active_auto_cc,
                                              RUNTIME_CONFIG_DEVICE_AUTO_CC_COUNT,
                                              "active_auto_cc_",
                                              key,
                                              value_text,
                                              &handled))
    {
        return 0U;
    }

    if (handled)
        return 1U;

    if (!AppConfigBackup_ApplyMidiCcPairKey(context, &device->bypass, "bypass", key, value_text, &handled))
        return 0U;
    if (handled)
        return 1U;

    if (!AppConfigBackup_ApplyPresetCcSlotKey(context,
                                              device->bypass_auto_cc,
                                              RUNTIME_CONFIG_DEVICE_AUTO_CC_COUNT,
                                              "bypass_auto_cc_",
                                              key,
                                              value_text,
                                              &handled))
    {
        return 0U;
    }

    if (handled)
        return 1U;

    if (!AppConfigBackup_ApplyMidiCcPairKey(context, &device->tap_tempo, "tap_tempo", key, value_text, &handled))
        return 0U;
    if (handled)
        return 1U;

    if (!AppConfigBackup_ApplyMidiCcPairKey(context, &device->volume1, "volume1", key, value_text, &handled))
        return 0U;
    if (handled)
        return 1U;

    if (!AppConfigBackup_ApplyMidiCcPairKey(context, &device->volume2, "volume2", key, value_text, &handled))
        return 0U;
    if (handled)
        return 1U;

    if (!AppConfigBackup_ApplyMidiCcPairKey(context, &device->mix1, "mix1", key, value_text, &handled))
        return 0U;
    if (handled)
        return 1U;

    if (!AppConfigBackup_ApplyMidiCcPairKey(context, &device->mix2, "mix2", key, value_text, &handled))
        return 0U;
    if (handled)
        return 1U;

    if (!AppConfigBackup_ApplyMidiCcPairKey(context, &device->decay1, "decay1", key, value_text, &handled))
        return 0U;
    if (handled)
        return 1U;

    return AppConfigBackup_ApplyMidiCcPairKey(context, &device->decay2, "decay2", key, value_text, &handled);
}

static uint8_t AppConfigBackup_ApplyBankKey(AppConfigBackupRestoreContext_t *context,
                                            const char *key,
                                            const char *value_text)
{
    RuntimeConfigBank_t *bank;
    uint8_t handled = 0U;
    uint32_t value;

    if (!context || !context->config || context->index >= PRESET_BANK_COUNT || !key || !value_text)
        return 0U;

    bank = &context->config->banks[context->index];

    if (strcmp(key, "index") == 0)
        return 1U;

    if (strcmp(key, "name") == 0)
        return AppConfigBackup_ParseStringAndMark(context, value_text, bank->name, sizeof(bank->name));

    if (strcmp(key, "wet_dry_enabled") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        bank->wet_dry_enabled = AppConfigBackup_NormalizeBool(value);
        return 1U;
    }

    if (strcmp(key, "midi_clock_bar_count") == 0)
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        if (value < RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MIN)
            value = RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MIN;
        bank->midi_clock_bar_count = AppConfigBackup_ClampU8(value, RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MAX);
        return 1U;
    }

    return AppConfigBackup_ApplyFunctionButtonKey(context, &bank->function_button, key, value_text, &handled);
}

static Preset_t *AppConfigBackup_GetRestorePreset(AppConfigBackupRestoreContext_t *context)
{
    if (!context || !context->config)
        return NULL;

    if (context->section == APP_CONFIG_BACKUP_RESTORE_SECTION_GLOBAL_BYPASS)
        return &context->config->global_bypass_preset;

    if (context->section == APP_CONFIG_BACKUP_RESTORE_SECTION_GLOBAL_MUTE)
        return &context->config->global_mute_preset;

    if (context->section == APP_CONFIG_BACKUP_RESTORE_SECTION_PRESET
     && context->presets
     && context->index < PRESET_COUNT)
    {
        return &context->presets[context->index];
    }

    return NULL;
}

static uint8_t AppConfigBackup_ApplyPresetKey(AppConfigBackupRestoreContext_t *context,
                                              const char *key,
                                              const char *value_text)
{
    Preset_t *preset = AppConfigBackup_GetRestorePreset(context);
    uint8_t handled = 0U;
    uint8_t index;
    uint32_t value;

    if (!preset || !key || !value_text)
        return 0U;

    if (strcmp(key, "index") == 0 || strcmp(key, "bank") == 0 || strcmp(key, "slot") == 0)
        return 1U;

    if (strcmp(key, "name") == 0)
        return AppConfigBackup_ParseStringAndMark(context, value_text, preset->name, sizeof(preset->name));

    if (AppConfigBackup_ParseIndexedKey(key, "program_device_", "", PRESET_DEVICE_SLOTS, &index))
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        preset->prg[index].program = AppConfigBackup_NormalizeMidi7OrUnused(value);
        return 1U;
    }

    if (!AppConfigBackup_ApplyPresetCcSlotKey(context,
                                              preset->cc,
                                              PRESET_CC_SLOT_COUNT,
                                              "cc_",
                                              key,
                                              value_text,
                                              &handled))
    {
        return 0U;
    }

    if (handled)
        return 1U;

    if (AppConfigBackup_ParseIndexedKey(key, "relay_", "", PRESET_RELAY_COUNT, &index))
    {
        if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
            return 0U;
        preset->relay[index] = value ? PRESET_RELAY_CLOSED : PRESET_RELAY_OPEN;
        return 1U;
    }

    return AppConfigBackup_ApplyFunctionButtonKey(context, &preset->function_button, key, value_text, &handled);
}

static uint8_t AppConfigBackup_ApplyUserThemeColourKey(AppConfigBackupRestoreContext_t *context,
                                                       uint16_t *colour,
                                                       const char *key,
                                                       const char *expected_key,
                                                       const char *value_text,
                                                       uint8_t *handled)
{
    uint32_t value;

    if (!context || !colour || !key || !expected_key || !value_text || !handled)
        return 0U;

    if (strcmp(key, expected_key) != 0)
        return 1U;

    *handled = 1U;
    if (!AppConfigBackup_ParseAndMark(context, value_text, &value))
        return 0U;

    *colour = (uint16_t)value;
    return 1U;
}

static uint8_t AppConfigBackup_ApplyUserThemeKey(AppConfigBackupRestoreContext_t *context,
                                                 const char *key,
                                                 const char *value_text)
{
    RuntimeConfigUserTheme_t *theme;
    uint8_t handled = 0U;

    if (!context || !context->config || context->index >= RUNTIME_CONFIG_USER_THEME_COUNT || !key || !value_text)
        return 0U;

    theme = &context->config->user_themes[context->index];

    if (!AppConfigBackup_ApplyUserThemeColourKey(context, &theme->display_bg_colour, key, "display_bg_colour", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->main_footbar_color, key, "main_footbar_color", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->main_footbar_text_colour, key, "main_footbar_text_colour", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->main_info_text_colour, key, "main_info_text_colour", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->main_info_edit_cursor_text_colour, key, "main_info_edit_cursor_text_colour", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->main_info_edit_cursor_bg_colour, key, "main_info_edit_cursor_bg_colour", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->main_info_edit_cursor_shared_bg_colour, key, "main_info_edit_cursor_shared_bg_colour", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->main_saving_popup_bg_colour, key, "main_saving_popup_bg_colour", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->main_saving_popup_text_colour, key, "main_saving_popup_text_colour", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->main_saving_popup_border_colour, key, "main_saving_popup_border_colour", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->main_mode_header_colour, key, "main_mode_header_colour", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->main_mode_header_edit_colour, key, "main_mode_header_edit_colour", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->main_mode_header_edit_bg_colour, key, "main_mode_header_edit_bg_colour", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->main_preset_colour, key, "main_preset_colour", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->main_bank_colour, key, "main_bank_colour", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->main_bank_wet_dry_colour, key, "main_bank_wet_dry_colour", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->main_special_function_button_active_colour, key, "main_special_function_button_active_colour", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->main_special_function_button_inactive_colour, key, "main_special_function_button_inactive_colour", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->main_special_function_button_active_bg, key, "main_special_function_button_active_bg", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->main_alert_badge_text_colour, key, "main_alert_badge_text_colour", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->bpm_internal_colour, key, "bpm_internal_colour", value_text, &handled)
     || !AppConfigBackup_ApplyUserThemeColourKey(context, &theme->ext_bpm_colour, key, "ext_bpm_colour", value_text, &handled))
    {
        return 0U;
    }

    return 1U;
}

static void AppConfigBackup_NormalizeRestoredSnapshot(AppConfigBackupRestoreContext_t *context)
{
    RuntimeConfigGlobal_t *global;

    if (!context || !context->config)
        return;

    global = &context->config->global;
    global->legacy_idle_timeout_minutes = 0U;
    global->display_mode = RuntimeConfig_NormalizeDisplayMode((uint8_t)global->display_mode);

    if (global->expression_pedal_min_raw > RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MAX)
        global->expression_pedal_min_raw = RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MAX;

    if (global->expression_pedal_max_raw > RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MAX)
        global->expression_pedal_max_raw = RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MAX;

    if (global->expression_pedal_min_raw >= global->expression_pedal_max_raw)
    {
        if (global->expression_pedal_min_raw < RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MAX)
            global->expression_pedal_max_raw = (uint16_t)(global->expression_pedal_min_raw + 1U);
        else
            global->expression_pedal_min_raw = (uint16_t)(RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MAX - 1U);
    }
}

static uint8_t AppConfigBackup_ApplyRestoreKey(AppConfigBackupRestoreContext_t *context,
                                               const char *key,
                                               const char *value_text)
{
    if (!context || !key || !value_text)
        return 0U;

    switch (context->section)
    {
    case APP_CONFIG_BACKUP_RESTORE_SECTION_TOP:
        return AppConfigBackup_ApplyTopLevelKey(context, key, value_text);

    case APP_CONFIG_BACKUP_RESTORE_SECTION_GLOBAL:
        return AppConfigBackup_ApplyGlobalKey(context, key, value_text);

    case APP_CONFIG_BACKUP_RESTORE_SECTION_METRONOME:
        return AppConfigBackup_ApplyMetronomeKey(context, key, value_text);

    case APP_CONFIG_BACKUP_RESTORE_SECTION_DEVICE:
        return AppConfigBackup_ApplyDeviceKey(context, key, value_text);

    case APP_CONFIG_BACKUP_RESTORE_SECTION_BANK:
        return AppConfigBackup_ApplyBankKey(context, key, value_text);

    case APP_CONFIG_BACKUP_RESTORE_SECTION_PRESET:
    case APP_CONFIG_BACKUP_RESTORE_SECTION_GLOBAL_BYPASS:
    case APP_CONFIG_BACKUP_RESTORE_SECTION_GLOBAL_MUTE:
        return AppConfigBackup_ApplyPresetKey(context, key, value_text);

    case APP_CONFIG_BACKUP_RESTORE_SECTION_USER_THEME:
        return AppConfigBackup_ApplyUserThemeKey(context, key, value_text);

    default:
        return 1U;
    }
}

static uint8_t AppConfigBackup_ParseRestoreLine(AppConfigBackupRestoreContext_t *context,
                                                char *line)
{
    char *cursor;
    char *separator;
    char *key;
    char *value;

    if (!context || !line)
        return 0U;

    AppConfigBackup_TrimRight(line);
    cursor = AppConfigBackup_SkipSpaces(line);

    if (cursor[0] == '\0' || cursor[0] == '#')
        return 1U;

    if (cursor[0] == '[')
        return AppConfigBackup_SetRestoreSection(context, cursor);

    separator = strchr(cursor, '=');
    if (!separator)
        return 0U;

    *separator = '\0';
    key = AppConfigBackup_SkipSpaces(cursor);
    value = AppConfigBackup_SkipSpaces(separator + 1);
    AppConfigBackup_TrimRight(key);
    AppConfigBackup_TrimRight(value);

    if (key[0] == '\0')
        return 0U;

    return AppConfigBackup_ApplyRestoreKey(context, key, value);
}

static void AppConfigBackup_ApplyRestoredSnapshot(const AppConfigBackupRestoreContext_t *context)
{
    if (!context || !context->config || !context->presets)
        return;

    RuntimeConfig_ApplySnapshot(context->config);

    for (uint8_t index = 0U; index < PRESET_COUNT; ++index)
    {
        Preset_t *preset = Presets_GetMutable(index);

        if (preset)
            *preset = context->presets[index];
    }

    RuntimeConfig_ClearPersistentStoreSaveBlock();
    RuntimeConfig_MarkDirty();
    Presets_MarkDirty();
    App_QueueSaveRequestEvent(APP_EVENT_SAVE_KIND_RUNTIME_CONFIG);
}

static AppConfigRestoreResult_t AppConfigBackup_RestoreSnapshotFile(const char *filename,
                                                                    uint32_t *bytes_read,
                                                                    uint32_t *values_read)
{
    AppSdCardFile_t file;
    AppConfigBackupRestoreContext_t context;
    AppConfigRestoreResult_t result = APP_CONFIG_RESTORE_RESULT_FAILED;
    char line[APP_CONFIG_BACKUP_LINE_BUFFER_SIZE];
    uint32_t file_start_tick = HAL_GetTick();
    uint32_t stage_tick;
    uint32_t parsed_values = 0UL;
    uint8_t opened = 0U;

    if (bytes_read)
        *bytes_read = 0UL;
    if (values_read)
        *values_read = 0UL;

    stage_tick = HAL_GetTick();
    if (!filename || !AppSdCard_OpenFile(&file, filename))
    {
        app_config_backup_read_file = NULL;
        result = APP_CONFIG_RESTORE_RESULT_FILE_NOT_FOUND;
        AppConfigBackup_LogFileStage("restore", filename, "open_read", stage_tick, 0U);
        goto finish;
    }
    opened = 1U;
    AppConfigBackup_LogFileStage("restore", filename, "open_read", stage_tick, 1U);

    if (bytes_read)
        *bytes_read = file.file_size;

    AppConfigBackup_RestoreContextInit(&context);
    AppConfigBackup_ReadBufferBegin(&file);

    stage_tick = HAL_GetTick();
    while (AppConfigBackup_ReadBufferHasMore())
    {
        uint8_t line_complete = 1U;

        if (!AppConfigBackup_ReadLine(&file, line, sizeof(line), &line_complete))
        {
            result = APP_CONFIG_RESTORE_RESULT_FAILED;
            goto parse_done;
        }

        if (!line_complete)
        {
            result = APP_CONFIG_RESTORE_RESULT_BAD_FORMAT;
            goto parse_done;
        }

        if (!AppConfigBackup_ParseRestoreLine(&context, line))
        {
            result = APP_CONFIG_RESTORE_RESULT_BAD_FORMAT;
            goto parse_done;
        }
    }

    AppConfigBackup_NormalizeRestoredSnapshot(&context);

    if (!context.format_version_seen
     || context.parsed_values < APP_CONFIG_RESTORE_MIN_PARSED_VALUES)
    {
        result = APP_CONFIG_RESTORE_RESULT_BAD_FORMAT;
        goto parse_done;
    }

    parsed_values = context.parsed_values;
    if (values_read)
        *values_read = parsed_values;

    AppConfigBackup_LogFileStage("restore", filename, "read_parse", stage_tick, 1U);
    app_config_backup_read_file = NULL;

    stage_tick = HAL_GetTick();
    AppConfigBackup_ApplyRestoredSnapshot(&context);
    AppConfigBackup_LogFileStage("restore", filename, "apply", stage_tick, 1U);
    result = APP_CONFIG_RESTORE_RESULT_OK;
    goto finish;

parse_done:
    parsed_values = context.parsed_values;
    if (values_read)
        *values_read = parsed_values;
    AppConfigBackup_LogFileStage("restore", filename, "read_parse", stage_tick, 0U);

finish:
    app_config_backup_read_file = NULL;
    printf("SDBACKUP_MON op=restore file=%s stage=file_total ms=%lu result=%u bytes=%lu values=%lu opened=%u\r\n",
           filename ? filename : "?",
           (unsigned long)AppConfigBackup_ElapsedMs(file_start_tick),
           (unsigned)result,
           (unsigned long)(opened ? file.file_size : 0UL),
           (unsigned long)parsed_values,
           (unsigned)opened);
    return result;
}

static void AppConfigBackup_InitSdCard(void)
{
    if (app_config_backup_sd_probed)
        return;

    app_config_backup_sd_probed = 1U;
    AppSdCard_InitAndProbe();
}

static AppConfigBackupResult_t AppConfigBackup_WriteSdSnapshotInternal(const char *reason,
                                                                       uint8_t force_probe)
{
    RuntimeConfig_t snapshot;
    uint32_t fallback_bytes = 0UL;
    uint32_t primary_bytes = 0UL;
    uint8_t fallback_ok;
    uint8_t primary_ok = 0U;
    AppConfigBackupResult_t result;
    uint32_t total_start_tick = HAL_GetTick();
    uint32_t stage_tick;

    if (force_probe)
        app_config_backup_sd_probed = 0U;

    AppSdCard_ResetMetrics();
    stage_tick = HAL_GetTick();
    AppSdCard_BeginExclusiveAccess();
    AppConfigBackup_InitSdCard();
    AppConfigBackup_LogStage("backup", "sd_probe_mount", stage_tick);
    if (!AppSdCard_IsFilesystemReady())
    {
        printf("SDBACKUP write=skipped reason=sd_unavailable status=\"%s\"\r\n",
               AppSdCard_GetStatusText());
        result = AppSdCard_IsPresent()
            ? APP_CONFIG_BACKUP_RESULT_SD_UNAVAILABLE
            : APP_CONFIG_BACKUP_RESULT_NO_SD_CARD;
        goto finish;
    }

    if (RuntimeConfig_PersistentStoreSaveIsBlocked())
    {
        printf("SDBACKUP write=blocked reason=loaded_defaults\r\n");
        result = APP_CONFIG_BACKUP_RESULT_BLOCKED_DEFAULTS;
        goto finish;
    }

    stage_tick = HAL_GetTick();
    RuntimeConfig_CopyPersistentSaveSnapshot(&snapshot);
    AppConfigBackup_LogStage("backup", "snapshot_copy", stage_tick);
    if ((RuntimeConfig_PersistentSnapshotLooksFactoryDefault(&snapshot)
      || RuntimeConfig_PersistentSnapshotCoreLooksFactoryDefault(&snapshot))
     && Presets_RuntimeStoreLooksFactoryDefault())
    {
        printf("SDBACKUP write=blocked reason=factory_default_snapshot\r\n");
        result = APP_CONFIG_BACKUP_RESULT_FACTORY_DEFAULT;
        goto finish;
    }

    stage_tick = HAL_GetTick();
    fallback_ok = AppConfigBackup_WriteSnapshotFile(APP_CONFIG_BACKUP_FALLBACK_FILE,
                                                    &snapshot,
                                                    reason,
                                                    &fallback_bytes);
    AppConfigBackup_LogFileStage("backup", APP_CONFIG_BACKUP_FALLBACK_FILE, "file_call", stage_tick, fallback_ok);
    if (fallback_ok)
    {
        stage_tick = HAL_GetTick();
        primary_ok = AppConfigBackup_WriteSnapshotFile(APP_CONFIG_BACKUP_PRIMARY_FILE,
                                                       &snapshot,
                                                       reason,
                                                       &primary_bytes);
        AppConfigBackup_LogFileStage("backup", APP_CONFIG_BACKUP_PRIMARY_FILE, "file_call", stage_tick, primary_ok);
    }

    printf("SDBACKUP write=%s reason=\"%s\" file=%s bytes=%lu fallback=%s fallback_bytes=%lu\r\n",
           primary_ok ? "ok" : (fallback_ok ? "partial" : "failed"),
           reason ? reason : "manual",
           APP_CONFIG_BACKUP_PRIMARY_FILE,
           (unsigned long)primary_bytes,
           fallback_ok ? APP_CONFIG_BACKUP_FALLBACK_FILE : "none",
           (unsigned long)fallback_bytes);

    if (primary_ok)
    {
        result = APP_CONFIG_BACKUP_RESULT_OK;
        goto finish;
    }

    result = fallback_ok
        ? APP_CONFIG_BACKUP_RESULT_PARTIAL
        : APP_CONFIG_BACKUP_RESULT_FAILED;

finish:
    AppSdCard_EndExclusiveAccess();
    AppConfigBackup_LogSdMetrics("backup", total_start_tick, (uint32_t)result);
    return result;
}

uint8_t AppConfigBackup_WriteSdSnapshot(const char *reason)
{
    return AppConfigBackup_ResultIsSuccess(
        AppConfigBackup_WriteSdSnapshotInternal(reason, 0U));
}

AppConfigBackupResult_t AppConfigBackup_WriteSdSnapshotDetailed(const char *reason)
{
    return AppConfigBackup_WriteSdSnapshotInternal(reason, 1U);
}

static AppConfigRestoreResult_t AppConfigBackup_RestoreSdSnapshotInternal(uint8_t force_probe)
{
    AppConfigRestoreResult_t primary_result;
    AppConfigRestoreResult_t fallback_result;
    AppConfigRestoreResult_t result;
    uint32_t primary_bytes = 0UL;
    uint32_t fallback_bytes = 0UL;
    uint32_t values_read = 0UL;
    uint32_t total_start_tick = HAL_GetTick();
    uint32_t stage_tick;

    if (force_probe)
        app_config_backup_sd_probed = 0U;

    AppSdCard_ResetMetrics();
    stage_tick = HAL_GetTick();
    AppSdCard_BeginExclusiveAccess();
    AppConfigBackup_InitSdCard();
    AppConfigBackup_LogStage("restore", "sd_probe_mount", stage_tick);
    if (!AppSdCard_IsFilesystemReady())
    {
        printf("SDBACKUP restore=skipped reason=sd_unavailable status=\"%s\"\r\n",
               AppSdCard_GetStatusText());
        result = AppSdCard_IsPresent()
            ? APP_CONFIG_RESTORE_RESULT_SD_UNAVAILABLE
            : APP_CONFIG_RESTORE_RESULT_NO_SD_CARD;
        goto finish;
    }

    stage_tick = HAL_GetTick();
    primary_result = AppConfigBackup_RestoreSnapshotFile(APP_CONFIG_BACKUP_PRIMARY_FILE,
                                                         &primary_bytes,
                                                         &values_read);
    AppConfigBackup_LogFileStage("restore", APP_CONFIG_BACKUP_PRIMARY_FILE, "file_call", stage_tick, (primary_result == APP_CONFIG_RESTORE_RESULT_OK) ? 1U : 0U);
    if (primary_result == APP_CONFIG_RESTORE_RESULT_OK)
    {
        printf("SDBACKUP restore=ok file=%s bytes=%lu values=%lu\r\n",
               APP_CONFIG_BACKUP_PRIMARY_FILE,
               (unsigned long)primary_bytes,
               (unsigned long)values_read);
        result = APP_CONFIG_RESTORE_RESULT_OK;
        goto finish;
    }

    stage_tick = HAL_GetTick();
    fallback_result = AppConfigBackup_RestoreSnapshotFile(APP_CONFIG_BACKUP_FALLBACK_FILE,
                                                          &fallback_bytes,
                                                          &values_read);
    AppConfigBackup_LogFileStage("restore", APP_CONFIG_BACKUP_FALLBACK_FILE, "file_call", stage_tick, (fallback_result == APP_CONFIG_RESTORE_RESULT_OK) ? 1U : 0U);
    if (fallback_result == APP_CONFIG_RESTORE_RESULT_OK)
    {
        printf("SDBACKUP restore=ok file=%s bytes=%lu values=%lu primary_result=%u primary_bytes=%lu\r\n",
               APP_CONFIG_BACKUP_FALLBACK_FILE,
               (unsigned long)fallback_bytes,
               (unsigned long)values_read,
               (unsigned)primary_result,
               (unsigned long)primary_bytes);
        result = APP_CONFIG_RESTORE_RESULT_OK;
        goto finish;
    }

    printf("SDBACKUP restore=failed primary_result=%u primary_bytes=%lu fallback_result=%u fallback_bytes=%lu\r\n",
           (unsigned)primary_result,
           (unsigned long)primary_bytes,
           (unsigned)fallback_result,
           (unsigned long)fallback_bytes);

    if (primary_result == APP_CONFIG_RESTORE_RESULT_FILE_NOT_FOUND
     && fallback_result == APP_CONFIG_RESTORE_RESULT_FILE_NOT_FOUND)
    {
        result = APP_CONFIG_RESTORE_RESULT_FILE_NOT_FOUND;
        goto finish;
    }

    if (primary_result != APP_CONFIG_RESTORE_RESULT_FILE_NOT_FOUND)
    {
        result = primary_result;
        goto finish;
    }

    result = fallback_result;

finish:
    AppSdCard_EndExclusiveAccess();
    AppConfigBackup_LogSdMetrics("restore", total_start_tick, (uint32_t)result);
    return result;
}

uint8_t AppConfigBackup_RestoreSdSnapshot(void)
{
    return AppConfigBackup_RestoreResultIsSuccess(
        AppConfigBackup_RestoreSdSnapshotInternal(0U));
}

AppConfigRestoreResult_t AppConfigBackup_RestoreSdSnapshotDetailed(void)
{
    return AppConfigBackup_RestoreSdSnapshotInternal(1U);
}
