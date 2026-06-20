#include "runtime_config.h"
#include "persistent_store_layout.h"

#include "midi/clock_engine.h"
#include "midi_functions.h"

#include <stdio.h>
#include <string.h>
#include "st7796_rgb565_colors.h"

#define RUNTIME_CONFIG_INVALID_BANK_NAME                "(bank?)"
#define RUNTIME_CONFIG_GLOBAL_STARTUP_DELAY_DEFAULT     1U
#define RUNTIME_CONFIG_GLOBAL_LEGACY_IDLE_TIMEOUT_DEFAULT   0U
#define RUNTIME_CONFIG_GLOBAL_DISPLAY_MODE_DEFAULT       RUNTIME_CONFIG_DISPLAY_MODE_DARK
#define RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_DEFAULT        RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MAX
#define RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_ENABLED_DEFAULT   0U
#define RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_THRESHOLD_DEFAULT 96U
#define RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_REDUCE_DEFAULT    32U
#define RUNTIME_CONFIG_GLOBAL_CLOCK_MODE_DEFAULT       RUNTIME_CONFIG_CLOCK_MODE_MONITOR
#define RUNTIME_CONFIG_GLOBAL_LIVE_ENC2_MODE_DEFAULT    RUNTIME_CONFIG_LIVE_ENC2_MODE_PRESET_BANK_SCROLL
#define RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_MODE_DEFAULT RUNTIME_CONFIG_EXPRESSION_PEDAL_MODE_DISABLED
#define RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_MIN_RAW_DEFAULT RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MIN
#define RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_MAX_RAW_DEFAULT RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MAX
#define RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_INVERT_DEFAULT 0U
#define RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_CC_DEFAULT PRESET_CC_NUMBER_UNUSED
#define RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_HEEL_DEFAULT 0U
#define RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_TOE_DEFAULT 127U
#define RUNTIME_CONFIG_METRONOME_VOLUME_DEFAULT         50U
#define RUNTIME_CONFIG_METRONOME_PITCH_DEFAULT          RUNTIME_CONFIG_METRONOME_PITCH_MID
#define RUNTIME_CONFIG_METRONOME_BEATS_PER_BAR_DEFAULT  4U
#define RUNTIME_CONFIG_METRONOME_RHYTHM_DEFAULT         RUNTIME_CONFIG_METRONOME_RHYTHM_QUARTER_NOTES

#define RUNTIME_CONFIG_PROGRAM_MESSAGE_UNUSED \
    { .channel = PRESET_CC_CHANNEL_UNUSED, .program = PRESET_PROGRAM_NONE }

#define RUNTIME_CONFIG_CC_MESSAGE_UNUSED \
    { .channel = PRESET_CC_CHANNEL_UNUSED, .cc_number = PRESET_CC_NUMBER_UNUSED, .value = PRESET_CC_VALUE_UNUSED }

#define RUNTIME_CONFIG_PROGRAM_MESSAGE_LIST_EMPTY \
    { RUNTIME_CONFIG_PROGRAM_MESSAGE_UNUSED, RUNTIME_CONFIG_PROGRAM_MESSAGE_UNUSED, RUNTIME_CONFIG_PROGRAM_MESSAGE_UNUSED, RUNTIME_CONFIG_PROGRAM_MESSAGE_UNUSED }

#define RUNTIME_CONFIG_CC_MESSAGE_LIST_EMPTY \
    { RUNTIME_CONFIG_CC_MESSAGE_UNUSED, RUNTIME_CONFIG_CC_MESSAGE_UNUSED, RUNTIME_CONFIG_CC_MESSAGE_UNUSED, RUNTIME_CONFIG_CC_MESSAGE_UNUSED }

#define RUNTIME_CONFIG_PRESET_CC_LIST_EMPTY \
        { RUNTIME_CONFIG_CC_MESSAGE_UNUSED, RUNTIME_CONFIG_CC_MESSAGE_UNUSED, RUNTIME_CONFIG_CC_MESSAGE_UNUSED, RUNTIME_CONFIG_CC_MESSAGE_UNUSED, \
            RUNTIME_CONFIG_CC_MESSAGE_UNUSED, RUNTIME_CONFIG_CC_MESSAGE_UNUSED, RUNTIME_CONFIG_CC_MESSAGE_UNUSED, RUNTIME_CONFIG_CC_MESSAGE_UNUSED }

#define RUNTIME_CONFIG_PRESET_PROGRAM_LIST_EMPTY \
        { { PRESET_PROGRAM_NONE }, { PRESET_PROGRAM_NONE }, { PRESET_PROGRAM_NONE }, { PRESET_PROGRAM_NONE }, \
            { PRESET_PROGRAM_NONE }, { PRESET_PROGRAM_NONE }, { PRESET_PROGRAM_NONE }, { PRESET_PROGRAM_NONE } }

#define RUNTIME_CONFIG_DEVICE_CC_UNUSED \
    { .cc = PRESET_CC_NUMBER_UNUSED, .value = 0U }

#define RUNTIME_CONFIG_DEVICE_NAME_LENGTH_LEGACY_V2   4U
#define RUNTIME_CONFIG_DEVICE_NAME_LENGTH_LEGACY_V4   8U

typedef struct {
    char name[RUNTIME_CONFIG_BANK_NAME_LENGTH + 1U];
    uint8_t wet_dry_enabled;
    RuntimeConfigFunctionButton_t function_button;
} RuntimeConfigBankLegacyV2_t;

typedef struct {
    char name[RUNTIME_CONFIG_DEVICE_NAME_LENGTH_LEGACY_V2 + 1U];
    uint8_t channel;
    MidiCC_t active;
    MidiCC_t bypass;
    MidiCC_t tap_tempo;
    MidiCC_t level;
    uint8_t max_preset;
} RuntimeConfigDeviceLegacyV2_t;

typedef struct {
    char name[RUNTIME_CONFIG_DEVICE_NAME_LENGTH_LEGACY_V4 + 1U];
    uint8_t channel;
    MidiCC_t active;
    MidiCC_t bypass;
    MidiCC_t tap_tempo;
    MidiCC_t level;
    uint8_t max_preset;
} RuntimeConfigDeviceLegacyV4_t;

typedef struct {
    char name[RUNTIME_CONFIG_DEVICE_NAME_LENGTH + 1U];
    uint8_t channel;
    MidiCC_t active;
    MidiCC_t bypass;
    MidiCC_t tap_tempo;
    MidiCC_t level;
    uint8_t max_preset;
} RuntimeConfigDeviceLegacyV6_t;

typedef struct {
    char name[RUNTIME_CONFIG_DEVICE_NAME_LENGTH + 1U];
    uint8_t channel;
    MidiCC_t active;
    MidiCC_t bypass;
    MidiCC_t tap_tempo;
    MidiCC_t level;
    MidiCC_t mix;
    uint8_t max_preset;
} RuntimeConfigDeviceLegacyV7_t;

typedef struct {
    char name[RUNTIME_CONFIG_DEVICE_NAME_LENGTH + 1U];
    uint8_t channel;
    MidiCC_t active;
    MidiCC_t bypass;
    MidiCC_t tap_tempo;
    MidiCC_t level;
    MidiCC_t mix1;
    MidiCC_t mix2;
    MidiCC_t decay1;
    MidiCC_t decay2;
    uint8_t max_preset;
} RuntimeConfigDeviceLegacyV8_t;

typedef struct {
    uint8_t startup_delay_seconds;
    uint8_t legacy_idle_timeout_minutes;
    RuntimeConfigSyncStyle_t sync_style;
    RuntimeConfigDisplayMode_t display_mode;
    uint16_t backlight_brightness;
    uint8_t feedback_taper_enabled;
    uint8_t feedback_taper_threshold;
    uint8_t feedback_taper_reduce;
} RuntimeConfigGlobalLegacyV10_t;

typedef struct {
    uint8_t startup_delay_seconds;
    uint8_t legacy_idle_timeout_minutes;
    RuntimeConfigSyncStyle_t sync_style;
    RuntimeConfigDisplayMode_t display_mode;
    uint16_t backlight_brightness;
    uint8_t feedback_taper_enabled;
    uint8_t feedback_taper_threshold;
    uint8_t feedback_taper_reduce;
    RuntimeConfigLiveEnc2Mode_t live_enc2_mode;
} RuntimeConfigGlobalLegacyV11_t;

typedef struct {
    uint8_t startup_delay_seconds;
    uint8_t legacy_idle_timeout_minutes;
    RuntimeConfigSyncStyle_t sync_style;
    RuntimeConfigDisplayMode_t display_mode;
    uint16_t backlight_brightness;
    uint8_t feedback_taper_enabled;
    uint8_t feedback_taper_threshold;
    uint8_t feedback_taper_reduce;
    RuntimeConfigLiveEnc2Mode_t live_enc2_mode;
    RuntimeConfigExpressionPedalMode_t expression_pedal_mode;
} RuntimeConfigGlobalLegacyV12_t;

typedef struct {
    uint8_t startup_delay_seconds;
    uint8_t legacy_idle_timeout_minutes;
    RuntimeConfigSyncStyle_t sync_style;
    RuntimeConfigDisplayMode_t display_mode;
    uint16_t backlight_brightness;
    uint8_t feedback_taper_enabled;
    uint8_t feedback_taper_threshold;
    uint8_t feedback_taper_reduce;
    RuntimeConfigLiveEnc2Mode_t live_enc2_mode;
    RuntimeConfigExpressionPedalMode_t expression_pedal_mode;
    uint16_t expression_pedal_min_raw;
    uint16_t expression_pedal_max_raw;
    uint8_t expression_pedal_invert;
} RuntimeConfigGlobalLegacyV13_t;

typedef struct {
    uint8_t startup_delay_seconds;
    uint8_t legacy_idle_timeout_minutes;
    RuntimeConfigSyncStyle_t sync_style;
    RuntimeConfigDisplayMode_t display_mode;
    uint16_t backlight_brightness;
    uint8_t feedback_taper_enabled;
    uint8_t feedback_taper_threshold;
    uint8_t feedback_taper_reduce;
    RuntimeConfigLiveEnc2Mode_t live_enc2_mode;
    RuntimeConfigExpressionPedalMode_t expression_pedal_mode;
    uint16_t expression_pedal_min_raw;
    uint16_t expression_pedal_max_raw;
    uint8_t expression_pedal_invert;
    RuntimeConfigExpressionPedalCcSlot_t expression_pedal_cc_slots[RUNTIME_CONFIG_EXPRESSION_PEDAL_CC_SLOT_COUNT];
} RuntimeConfigGlobalLegacyV14_t;

typedef struct {
    uint8_t startup_delay_seconds;
    uint8_t legacy_idle_timeout_minutes;
    RuntimeConfigSyncStyle_t sync_style;
    RuntimeConfigDisplayMode_t display_mode;
    uint16_t backlight_brightness;
} RuntimeConfigGlobalLegacyV9_t;

typedef struct {
    uint8_t startup_delay_seconds;
    uint8_t legacy_idle_timeout_minutes;
    RuntimeConfigSyncStyle_t sync_style;
    uint16_t backlight_brightness;
} RuntimeConfigGlobalLegacyV4_t;

typedef struct {
    RuntimeConfigBankLegacyV2_t banks[PRESET_BANK_COUNT];
    RuntimeConfigDeviceLegacyV2_t devices[MIDI_DEVICE_COUNT];
    RuntimeConfigGlobalLegacyV4_t global;
} RuntimeConfigLegacyV2_t;

typedef struct {
    RuntimeConfigBank_t banks[PRESET_BANK_COUNT];
    RuntimeConfigDeviceLegacyV2_t devices[MIDI_DEVICE_COUNT];
    RuntimeConfigGlobalLegacyV4_t global;
} RuntimeConfigLegacyV3_t;

typedef struct {
    RuntimeConfigBank_t banks[PRESET_BANK_COUNT];
    RuntimeConfigDeviceLegacyV4_t devices[MIDI_DEVICE_COUNT];
    RuntimeConfigGlobalLegacyV4_t global;
} RuntimeConfigLegacyV4_t;

typedef struct {
    RuntimeConfigBank_t banks[PRESET_BANK_COUNT];
    RuntimeConfigDeviceLegacyV6_t devices[MIDI_DEVICE_COUNT];
    RuntimeConfigGlobalLegacyV9_t global;
} RuntimeConfigLegacyNoUserThemes_t;

typedef struct {
    RuntimeConfigBank_t banks[PRESET_BANK_COUNT];
    RuntimeConfigDeviceLegacyV6_t devices[MIDI_DEVICE_COUNT];
    RuntimeConfigGlobalLegacyV9_t global;
    RuntimeConfigUserTheme_t user_themes[RUNTIME_CONFIG_USER_THEME_COUNT];
} RuntimeConfigLegacyV5_t;

typedef struct {
    RuntimeConfigBank_t banks[PRESET_BANK_COUNT];
    RuntimeConfigDeviceLegacyV6_t devices[MIDI_DEVICE_COUNT];
    RuntimeConfigGlobalLegacyV9_t global;
    RuntimeConfigMetronome_t metronome;
    RuntimeConfigUserTheme_t user_themes[RUNTIME_CONFIG_USER_THEME_COUNT];
} RuntimeConfigLegacyV6_t;

typedef struct {
    RuntimeConfigBank_t banks[PRESET_BANK_COUNT];
    RuntimeConfigDeviceLegacyV7_t devices[MIDI_DEVICE_COUNT];
    RuntimeConfigGlobalLegacyV9_t global;
    RuntimeConfigMetronome_t metronome;
    RuntimeConfigUserTheme_t user_themes[RUNTIME_CONFIG_USER_THEME_COUNT];
} RuntimeConfigLegacyV7_t;

typedef struct {
    RuntimeConfigBank_t banks[PRESET_BANK_COUNT];
    RuntimeConfigDeviceLegacyV8_t devices[MIDI_DEVICE_COUNT];
    RuntimeConfigGlobalLegacyV9_t global;
    RuntimeConfigMetronome_t metronome;
    RuntimeConfigUserTheme_t user_themes[RUNTIME_CONFIG_USER_THEME_COUNT];
} RuntimeConfigLegacyV8_t;

typedef struct {
    RuntimeConfigBank_t banks[PRESET_BANK_COUNT];
    RuntimeConfigDevice_t devices[MIDI_DEVICE_COUNT];
    RuntimeConfigGlobalLegacyV9_t global;
    RuntimeConfigMetronome_t metronome;
    RuntimeConfigUserTheme_t user_themes[RUNTIME_CONFIG_USER_THEME_COUNT];
} RuntimeConfigLegacyV9_t;

typedef struct {
    RuntimeConfigBank_t banks[PRESET_BANK_COUNT];
    RuntimeConfigDevice_t devices[MIDI_DEVICE_COUNT];
    RuntimeConfigGlobalLegacyV12_t global;
    RuntimeConfigMetronome_t metronome;
    RuntimeConfigUserTheme_t user_themes[RUNTIME_CONFIG_USER_THEME_COUNT];
    Preset_t global_bypass_preset;
    Preset_t global_mute_preset;
} RuntimeConfigLegacyV12_t;

typedef struct {
    RuntimeConfigBank_t banks[PRESET_BANK_COUNT];
    RuntimeConfigDevice_t devices[MIDI_DEVICE_COUNT];
    RuntimeConfigGlobalLegacyV13_t global;
    RuntimeConfigMetronome_t metronome;
    RuntimeConfigUserTheme_t user_themes[RUNTIME_CONFIG_USER_THEME_COUNT];
    Preset_t global_bypass_preset;
    Preset_t global_mute_preset;
} RuntimeConfigLegacyV13_t;

typedef struct {
    RuntimeConfigBank_t banks[PRESET_BANK_COUNT];
    RuntimeConfigDevice_t devices[MIDI_DEVICE_COUNT];
    RuntimeConfigGlobalLegacyV14_t global;
    RuntimeConfigMetronome_t metronome;
    RuntimeConfigUserTheme_t user_themes[RUNTIME_CONFIG_USER_THEME_COUNT];
    Preset_t global_bypass_preset;
    Preset_t global_mute_preset;
} RuntimeConfigLegacyV14_t;

typedef struct {
    RuntimeConfigBank_t banks[PRESET_BANK_COUNT];
    RuntimeConfigDevice_t devices[MIDI_DEVICE_COUNT];
    RuntimeConfigGlobalLegacyV11_t global;
    RuntimeConfigMetronome_t metronome;
    RuntimeConfigUserTheme_t user_themes[RUNTIME_CONFIG_USER_THEME_COUNT];
    Preset_t global_bypass_preset;
    Preset_t global_mute_preset;
} RuntimeConfigLegacyV11_t;

typedef struct {
    RuntimeConfigBank_t banks[PRESET_BANK_COUNT];
    RuntimeConfigDevice_t devices[MIDI_DEVICE_COUNT];
    RuntimeConfigGlobalLegacyV10_t global;
    RuntimeConfigMetronome_t metronome;
    RuntimeConfigUserTheme_t user_themes[RUNTIME_CONFIG_USER_THEME_COUNT];
} RuntimeConfigLegacyV10_t;

/* Transitional branch schema: bank entries carried two extra footswitch-role
 * bytes that were later removed, while the rest of the persisted model stayed
 * aligned with the modern runtime config layout. */
typedef struct {
    char name[RUNTIME_CONFIG_BANK_NAME_LENGTH + 1U];
    uint8_t wet_dry_enabled;
    RuntimeConfigFunctionButton_t function_button;
    uint8_t midi_clock_bar_count;
    uint8_t footswitch9_role;
    uint8_t footswitch10_role;
} RuntimeConfigBankLegacyWithRoles_t;

typedef struct {
    RuntimeConfigBankLegacyWithRoles_t banks[PRESET_BANK_COUNT];
    RuntimeConfigDevice_t devices[MIDI_DEVICE_COUNT];
    RuntimeConfigGlobalLegacyV11_t global;
    RuntimeConfigMetronome_t metronome;
    RuntimeConfigUserTheme_t user_themes[RUNTIME_CONFIG_USER_THEME_COUNT];
} RuntimeConfigLegacyWithRolesNoGlobalPresets_t;

typedef struct {
    RuntimeConfigBankLegacyWithRoles_t banks[PRESET_BANK_COUNT];
    RuntimeConfigDevice_t devices[MIDI_DEVICE_COUNT];
    RuntimeConfigGlobalLegacyV11_t global;
    RuntimeConfigMetronome_t metronome;
    RuntimeConfigUserTheme_t user_themes[RUNTIME_CONFIG_USER_THEME_COUNT];
    Preset_t global_bypass_preset;
    Preset_t global_mute_preset;
} RuntimeConfigLegacyWithRoles_t;

#define RUNTIME_CONFIG_FUNCTION_BUTTON_DEFAULT \
    { \
        .name = "SpcBtn", \
        .active_label = "active", \
        .inactive_label = "bypass", \
        .active_programs = RUNTIME_CONFIG_PROGRAM_MESSAGE_LIST_EMPTY, \
        .active_cc = RUNTIME_CONFIG_CC_MESSAGE_LIST_EMPTY, \
        .inactive_programs = RUNTIME_CONFIG_PROGRAM_MESSAGE_LIST_EMPTY, \
        .inactive_cc = RUNTIME_CONFIG_CC_MESSAGE_LIST_EMPTY, \
    }

#define RUNTIME_CONFIG_BANK_ENTRY(name_literal) \
    { \
        .name = name_literal, \
        .wet_dry_enabled = 0U, \
        .function_button = RUNTIME_CONFIG_FUNCTION_BUTTON_DEFAULT, \
        .midi_clock_bar_count = RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_DEFAULT, \
    }

#define RUNTIME_CONFIG_DEVICE_ENTRY(name_literal, channel_value, active_cc_value, active_data_value, bypass_cc_value, bypass_data_value, tap_cc_value, tap_data_value, max_preset_value) \
    { \
        .name = name_literal, \
        .channel = channel_value, \
        .active = { .cc = active_cc_value, .value = active_data_value }, \
        .bypass = { .cc = bypass_cc_value, .value = bypass_data_value }, \
        .tap_tempo = { .cc = tap_cc_value, .value = tap_data_value }, \
        .volume1 = RUNTIME_CONFIG_DEVICE_CC_UNUSED, \
        .volume2 = RUNTIME_CONFIG_DEVICE_CC_UNUSED, \
        .mix1 = RUNTIME_CONFIG_DEVICE_CC_UNUSED, \
        .mix2 = RUNTIME_CONFIG_DEVICE_CC_UNUSED, \
        .decay1 = RUNTIME_CONFIG_DEVICE_CC_UNUSED, \
        .decay2 = RUNTIME_CONFIG_DEVICE_CC_UNUSED, \
        .max_preset = max_preset_value, \
    }

#define RUNTIME_CONFIG_USER_THEME_ENTRY(display_bg_value, footbar_value, footbar_text_value, info_text_value, cursor_text_value, cursor_bg_value, cursor_shared_bg_value, popup_bg_value, popup_text_value, popup_border_value, header_value, header_edit_value, header_edit_bg_value, preset_value, bank_value, wet_dry_value, function_active_value, function_inactive_value, function_active_bg_value, alert_text_value, bpm_internal_value, ext_bpm_value) \
    { \
        .display_bg_colour = display_bg_value, \
        .main_footbar_color = footbar_value, \
        .main_footbar_text_colour = footbar_text_value, \
        .main_info_text_colour = info_text_value, \
        .main_info_edit_cursor_text_colour = cursor_text_value, \
        .main_info_edit_cursor_bg_colour = cursor_bg_value, \
        .main_info_edit_cursor_shared_bg_colour = cursor_shared_bg_value, \
        .main_saving_popup_bg_colour = popup_bg_value, \
        .main_saving_popup_text_colour = popup_text_value, \
        .main_saving_popup_border_colour = popup_border_value, \
        .main_mode_header_colour = header_value, \
        .main_mode_header_edit_colour = header_edit_value, \
        .main_mode_header_edit_bg_colour = header_edit_bg_value, \
        .main_preset_colour = preset_value, \
        .main_bank_colour = bank_value, \
        .main_bank_wet_dry_colour = wet_dry_value, \
        .main_special_function_button_active_colour = function_active_value, \
        .main_special_function_button_inactive_colour = function_inactive_value, \
        .main_special_function_button_active_bg = function_active_bg_value, \
        .main_alert_badge_text_colour = alert_text_value, \
        .bpm_internal_colour = bpm_internal_value, \
        .ext_bpm_colour = ext_bpm_value, \
    }

#define RUNTIME_CONFIG_METRONOME_DEFAULT \
    { \
        .volume = RUNTIME_CONFIG_METRONOME_VOLUME_DEFAULT, \
        .pitch = RUNTIME_CONFIG_METRONOME_PITCH_DEFAULT, \
        .beats_per_bar = RUNTIME_CONFIG_METRONOME_BEATS_PER_BAR_DEFAULT, \
        .rhythm = RUNTIME_CONFIG_METRONOME_RHYTHM_DEFAULT, \
    }

static const RuntimeConfigBank_t runtime_config_blank_bank = {
    .name = RUNTIME_CONFIG_INVALID_BANK_NAME,
    .wet_dry_enabled = 0U,
    .function_button = RUNTIME_CONFIG_FUNCTION_BUTTON_DEFAULT,
    .midi_clock_bar_count = RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_DEFAULT,
};

static const RuntimeConfigDevice_t runtime_config_blank_device = {
    .name = "",
    .channel = 0U,
    .active = RUNTIME_CONFIG_DEVICE_CC_UNUSED,
    .bypass = RUNTIME_CONFIG_DEVICE_CC_UNUSED,
    .tap_tempo = RUNTIME_CONFIG_DEVICE_CC_UNUSED,
    .volume1 = RUNTIME_CONFIG_DEVICE_CC_UNUSED,
    .volume2 = RUNTIME_CONFIG_DEVICE_CC_UNUSED,
    .mix1 = RUNTIME_CONFIG_DEVICE_CC_UNUSED,
    .mix2 = RUNTIME_CONFIG_DEVICE_CC_UNUSED,
    .decay1 = RUNTIME_CONFIG_DEVICE_CC_UNUSED,
    .decay2 = RUNTIME_CONFIG_DEVICE_CC_UNUSED,
    .max_preset = 0U,
};

static const Preset_t runtime_config_global_bypass_preset_default = {
    .name = "Bypass",
    .prg = RUNTIME_CONFIG_PRESET_PROGRAM_LIST_EMPTY,
    .cc = RUNTIME_CONFIG_PRESET_CC_LIST_EMPTY,
    .relay = { PRESET_RELAY_OPEN, PRESET_RELAY_OPEN },
    .function_button = RUNTIME_CONFIG_FUNCTION_BUTTON_DEFAULT,
};

static const Preset_t runtime_config_global_mute_preset_default = {
    .name = "Mute",
    .prg = RUNTIME_CONFIG_PRESET_PROGRAM_LIST_EMPTY,
    .cc = RUNTIME_CONFIG_PRESET_CC_LIST_EMPTY,
    .relay = { PRESET_RELAY_OPEN, PRESET_RELAY_OPEN },
    .function_button = RUNTIME_CONFIG_FUNCTION_BUTTON_DEFAULT,
};

static const RuntimeConfig_t runtime_config_defaults = {
    .banks = {
        RUNTIME_CONFIG_BANK_ENTRY("Bank 1"),
        RUNTIME_CONFIG_BANK_ENTRY("Bank 2"),
        RUNTIME_CONFIG_BANK_ENTRY("Bank 3"),
        RUNTIME_CONFIG_BANK_ENTRY("Bank 4"),
        RUNTIME_CONFIG_BANK_ENTRY("Bank 5"),
        RUNTIME_CONFIG_BANK_ENTRY("Bank 6"),
        RUNTIME_CONFIG_BANK_ENTRY("Bank 7"),
        RUNTIME_CONFIG_BANK_ENTRY("Bank 8"),
    },
    .devices = {
        RUNTIME_CONFIG_DEVICE_ENTRY("Dev1", 1U, 60U, 127U, 60U, 0U, 35U, 64U, 35U),
        RUNTIME_CONFIG_DEVICE_ENTRY("Dev2", 2U, 60U, 127U, 60U, 0U, 35U, 64U, 35U),
        RUNTIME_CONFIG_DEVICE_ENTRY("Dev3", 3U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, 127U),
        RUNTIME_CONFIG_DEVICE_ENTRY("Dev4", 4U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, 127U),
        RUNTIME_CONFIG_DEVICE_ENTRY("Dev5", 5U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, 127U),
        RUNTIME_CONFIG_DEVICE_ENTRY("Dev6", 6U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, 127U),
        RUNTIME_CONFIG_DEVICE_ENTRY("Dev7", 7U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, 127U),
        RUNTIME_CONFIG_DEVICE_ENTRY("Dev8", 8U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, 127U),
    },
    .global = {
        .startup_delay_seconds = RUNTIME_CONFIG_GLOBAL_STARTUP_DELAY_DEFAULT,
        .legacy_idle_timeout_minutes = RUNTIME_CONFIG_GLOBAL_LEGACY_IDLE_TIMEOUT_DEFAULT,
        .sync_style = RUNTIME_CONFIG_SYNC_STYLE_MIDI_CLOCK,
        .display_mode = RUNTIME_CONFIG_GLOBAL_DISPLAY_MODE_DEFAULT,
        .backlight_brightness = RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_DEFAULT,
        .feedback_taper_enabled = RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_ENABLED_DEFAULT,
        .feedback_taper_threshold = RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_THRESHOLD_DEFAULT,
        .feedback_taper_reduce = RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_REDUCE_DEFAULT,
        .clock_mode = RUNTIME_CONFIG_GLOBAL_CLOCK_MODE_DEFAULT,
        .live_enc2_mode = RUNTIME_CONFIG_GLOBAL_LIVE_ENC2_MODE_DEFAULT,
        .expression_pedal_mode = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_MODE_DEFAULT,
        .expression_pedal_min_raw = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_MIN_RAW_DEFAULT,
        .expression_pedal_max_raw = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_MAX_RAW_DEFAULT,
        .expression_pedal_invert = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_INVERT_DEFAULT,
        .expression_pedal_cc_slots = {
            [0 ... (RUNTIME_CONFIG_EXPRESSION_PEDAL_CC_SLOT_COUNT - 1)] = {
                .cc = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_CC_DEFAULT,
                .heel_value = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_HEEL_DEFAULT,
                .toe_value = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_TOE_DEFAULT,
            },
        },
    },
    .metronome = RUNTIME_CONFIG_METRONOME_DEFAULT,
    .user_themes = {
        RUNTIME_CONFIG_USER_THEME_ENTRY(BLACK, BLUE_SAPPHIRE, BABY_POWDER, AQUAMARINE, BLACK, CANTALOUPE_MELON, BRINK_PINK, BABY_POWDER, BLUE_SAPPHIRE, CANTALOUPE_MELON, BABY_POWDER, BLACK, CANTALOUPE_MELON, BABY_POWDER, PALE_AQUA, CANTALOUPE_MELON, BABY_POWDER, PALE_AQUA, BLUE_SAPPHIRE, BLACK, AQUA, CANTALOUPE_MELON),
        RUNTIME_CONFIG_USER_THEME_ENTRY(BLACK, DARK_GOLDENROD, LIGHT_GOLDENROD_YELLOW, AMBER, BLACK, GOLDENROD, ORANGE, LIGHT_GOLDENROD_YELLOW, DARK_BROWN, GOLDEN_BROWN, LIGHT_GOLDENROD_YELLOW, BLACK, AMBER, LIGHT_GOLDENROD_YELLOW, GOLDENROD, AMBER, LIGHT_GOLDENROD_YELLOW, GOLD_FUSION, BROWN, BLACK, AMBER, GOLDENROD),
        RUNTIME_CONFIG_USER_THEME_ENTRY(BLACK, TYRIAN_PURPLE, BABY_POWDER, CAPRI, BLACK, CAPRI, HOT_PINK, BABY_POWDER, TYRIAN_PURPLE, CAPRI, BABY_POWDER, BLACK, CAPRI, BABY_POWDER, CAPRI, CAPRI, BABY_POWDER, VIOLET_CRAYOLA, TYRIAN_PURPLE, BLACK, CAPRI, HOT_PINK),
    },
    .global_bypass_preset = {
        .name = "Bypass",
        .prg = RUNTIME_CONFIG_PRESET_PROGRAM_LIST_EMPTY,
        .cc = RUNTIME_CONFIG_PRESET_CC_LIST_EMPTY,
        .relay = { PRESET_RELAY_OPEN, PRESET_RELAY_OPEN },
        .function_button = RUNTIME_CONFIG_FUNCTION_BUTTON_DEFAULT,
    },
    .global_mute_preset = {
        .name = "Mute",
        .prg = RUNTIME_CONFIG_PRESET_PROGRAM_LIST_EMPTY,
        .cc = RUNTIME_CONFIG_PRESET_CC_LIST_EMPTY,
        .relay = { PRESET_RELAY_OPEN, PRESET_RELAY_OPEN },
        .function_button = RUNTIME_CONFIG_FUNCTION_BUTTON_DEFAULT,
    },
};

typedef enum {
    RUNTIME_CONFIG_PERSISTENT_STORE_DIAG_DEFAULT = 0,
    RUNTIME_CONFIG_PERSISTENT_STORE_DIAG_LEGACY_V2,
    RUNTIME_CONFIG_PERSISTENT_STORE_DIAG_ATOMIC_V3,
} RuntimeConfigPersistentStoreDiagnosticMode_t;

static RuntimeConfig_t runtime_config_store;
static uint8_t runtime_config_initialized = 0U;
static uint8_t runtime_config_dirty = 0U;
static uint8_t runtime_config_metronome_dirty = 0U;
static RuntimeConfigMetronome_t runtime_config_persisted_metronome = RUNTIME_CONFIG_METRONOME_DEFAULT;
static RuntimeConfigPersistentStoreDiagnosticMode_t runtime_config_persistent_store_diag_mode = RUNTIME_CONFIG_PERSISTENT_STORE_DIAG_DEFAULT;
static uint8_t runtime_config_persistent_store_diag_slot = 0U;
static uint32_t runtime_config_persistent_store_diag_generation = 0U;
static uint8_t runtime_config_persistent_store_save_blocked = 0U;

static uint8_t RuntimeConfig_FlashV3ImageIsValid(uint32_t slot_address,
                                                 const PersistentStoreHeaderV3_t **header_out);

static void RuntimeConfig_SeedGlobalMutePresetFromLegacyBehavior(Preset_t *preset)
{
    uint8_t cc_slot = 0U;

    if (!preset)
        return;

    /* Older firmware treated mute as an implicit "send volume CC = 0" action.
     * When migrating those snapshots, rebuild an explicit editable preset that
     * preserves the same effective output behavior. */
    for (uint8_t cc_index = 0U; cc_index < PRESET_CC_SLOT_COUNT; ++cc_index)
    {
        preset->cc[cc_index].channel = PRESET_CC_CHANNEL_UNUSED;
        preset->cc[cc_index].cc_number = PRESET_CC_NUMBER_UNUSED;
        preset->cc[cc_index].value = PRESET_CC_VALUE_UNUSED;
    }

    for (uint8_t device_index = 0U; device_index < MIDI_DEVICE_COUNT && cc_slot < PRESET_CC_SLOT_COUNT; ++device_index)
    {
        const RuntimeConfigDevice_t *device = &runtime_config_store.devices[device_index];

        /* Skip unconfigured channels so the migrated mute preset only targets
         * devices that could previously have reacted to volume CCs. */
        if (device->channel < 1U || device->channel > 16U)
            continue;

        if (device->volume1.cc != PRESET_CC_NUMBER_UNUSED && cc_slot < PRESET_CC_SLOT_COUNT)
        {
            preset->cc[cc_slot].channel = device->channel;
            preset->cc[cc_slot].cc_number = device->volume1.cc;
            preset->cc[cc_slot].value = 0U;
            ++cc_slot;
        }

        if (device->volume2.cc != PRESET_CC_NUMBER_UNUSED && cc_slot < PRESET_CC_SLOT_COUNT)
        {
            preset->cc[cc_slot].channel = device->channel;
            preset->cc[cc_slot].cc_number = device->volume2.cc;
            preset->cc[cc_slot].value = 0U;
            ++cc_slot;
        }
    }
}

static void RuntimeConfig_InitGlobalPresetsForLegacySnapshot(void)
{
    /* Legacy snapshots do not carry the new global preset payloads, so start
     * from current defaults and then derive the mute behavior from old device
     * volume assignments where possible. */
    runtime_config_store.global_bypass_preset = runtime_config_global_bypass_preset_default;
    runtime_config_store.global_mute_preset = runtime_config_global_mute_preset_default;
    RuntimeConfig_SeedGlobalMutePresetFromLegacyBehavior(&runtime_config_store.global_mute_preset);
}

static uint8_t RuntimeConfig_ShouldDeferMetronomePersistence(void)
{
    return ClockEngine_IsExternalSignalPresent();
}

static void RuntimeConfig_SyncPersistedMetronome(void)
{
    runtime_config_persisted_metronome = runtime_config_store.metronome;
    runtime_config_metronome_dirty = 0U;
}

static void RuntimeConfig_ResetUserThemesToDefaults(void)
{
    memcpy(runtime_config_store.user_themes,
           runtime_config_defaults.user_themes,
           sizeof(runtime_config_store.user_themes));
}

static void RuntimeConfig_ResetPersistentStoreDiagnostic(void)
{
    runtime_config_persistent_store_diag_mode = RUNTIME_CONFIG_PERSISTENT_STORE_DIAG_DEFAULT;
    runtime_config_persistent_store_diag_slot = 0U;
    runtime_config_persistent_store_diag_generation = 0U;
    runtime_config_persistent_store_save_blocked = 0U;
}

static const char *RuntimeConfig_PersistentStoreSlotStatus(uint32_t slot_address)
{
    const PersistentStoreHeaderV3_t *header = (const PersistentStoreHeaderV3_t *)slot_address;

    if (RuntimeConfig_FlashV3ImageIsValid(slot_address, NULL))
        return "valid";

    if (header->magic == 0xFFFFFFFFUL
     && header->version == 0xFFFFFFFFUL
     && header->commit_marker == 0xFFFFFFFFUL)
    {
        return "erased";
    }

    if (header->magic == PERSISTENT_STORE_MAGIC_V1
     || header->magic == PERSISTENT_STORE_MAGIC_V2)
    {
        return "legacy";
    }

    return "invalid";
}

static void RuntimeConfig_PrintPersistentStoreSlotDiagnostic(char slot_label,
                                                             uint32_t slot_address)
{
    const PersistentStoreHeaderV3_t *header = (const PersistentStoreHeaderV3_t *)slot_address;

    printf("PSTORE slot=%c status=%s magic=%08lX ver=%lu gen=%lu payload=%lu config=%lu commit=%08lX\r\n",
           slot_label,
           RuntimeConfig_PersistentStoreSlotStatus(slot_address),
           (unsigned long)header->magic,
           (unsigned long)header->version,
           (unsigned long)header->generation,
           (unsigned long)header->payload_size,
           (unsigned long)header->config_size,
           (unsigned long)header->commit_marker);
}

static uint8_t RuntimeConfig_PersistentStoreSlotsLookBlank(void)
{
    return (uint8_t)((RuntimeConfig_PersistentStoreSlotStatus(PERSISTENT_STORE_SLOT0_FLASH_ADDR)[0] == 'e')
                 && (RuntimeConfig_PersistentStoreSlotStatus(PERSISTENT_STORE_SLOT1_FLASH_ADDR)[0] == 'e'));
}

static void RuntimeConfig_CopyLegacyDevice(RuntimeConfigDevice_t *destination,
                                           const RuntimeConfigDeviceLegacyV2_t *source)
{
    if (!destination || !source)
        return;

    /* V2 only had one level control. Map that forward into volume1 and leave
     * the newer secondary controls explicitly unused. */
    memset(destination->name, 0, sizeof(destination->name));
    memcpy(destination->name, source->name, sizeof(source->name));
    destination->channel = source->channel;
    destination->active = source->active;
    destination->bypass = source->bypass;
    destination->tap_tempo = source->tap_tempo;
    destination->volume1 = source->level;
    destination->volume2.cc = PRESET_CC_NUMBER_UNUSED;
    destination->volume2.value = 0U;
    destination->mix1.cc = PRESET_CC_NUMBER_UNUSED;
    destination->mix1.value = 0U;
    destination->mix2.cc = PRESET_CC_NUMBER_UNUSED;
    destination->mix2.value = 0U;
    destination->decay1.cc = PRESET_CC_NUMBER_UNUSED;
    destination->decay1.value = 0U;
    destination->decay2.cc = PRESET_CC_NUMBER_UNUSED;
    destination->decay2.value = 0U;
    destination->max_preset = source->max_preset;
}

static void RuntimeConfig_CopyLegacyDeviceV4(RuntimeConfigDevice_t *destination,
                                             const RuntimeConfigDeviceLegacyV4_t *source)
{
    size_t copy_size;
    size_t source_name_length;

    if (!destination || !source)
        return;

    memset(destination->name, 0, sizeof(destination->name));
    if (strncmp(source->name, "Device ", 7U) == 0
     && source->name[7] >= '1'
     && source->name[7] <= '8'
     && source->name[8] == '\0')
    {
        destination->name[0] = 'D';
        destination->name[1] = 'e';
        destination->name[2] = 'v';
        destination->name[3] = source->name[7];
    }
    else
    {
        source_name_length = strnlen(source->name, sizeof(source->name));
        copy_size = (source_name_length < (sizeof(destination->name) - 1U))
            ? source_name_length
            : (sizeof(destination->name) - 1U);
        memcpy(destination->name, source->name, copy_size);
    }
    destination->channel = source->channel;
    destination->active = source->active;
    destination->bypass = source->bypass;
    destination->tap_tempo = source->tap_tempo;
    destination->volume1 = source->level;
    destination->volume2.cc = PRESET_CC_NUMBER_UNUSED;
    destination->volume2.value = 0U;
    destination->mix1.cc = PRESET_CC_NUMBER_UNUSED;
    destination->mix1.value = 0U;
    destination->mix2.cc = PRESET_CC_NUMBER_UNUSED;
    destination->mix2.value = 0U;
    destination->decay1.cc = PRESET_CC_NUMBER_UNUSED;
    destination->decay1.value = 0U;
    destination->decay2.cc = PRESET_CC_NUMBER_UNUSED;
    destination->decay2.value = 0U;
    destination->max_preset = source->max_preset;
}

static void RuntimeConfig_CopyLegacyDeviceV6(RuntimeConfigDevice_t *destination,
                                             const RuntimeConfigDeviceLegacyV6_t *source)
{
    if (!destination || !source)
        return;

    memcpy(destination->name, source->name, sizeof(destination->name));
    destination->channel = source->channel;
    destination->active = source->active;
    destination->bypass = source->bypass;
    destination->tap_tempo = source->tap_tempo;
    destination->volume1 = source->level;
    destination->volume2.cc = PRESET_CC_NUMBER_UNUSED;
    destination->volume2.value = 0U;
    destination->mix1.cc = PRESET_CC_NUMBER_UNUSED;
    destination->mix1.value = 0U;
    destination->mix2.cc = PRESET_CC_NUMBER_UNUSED;
    destination->mix2.value = 0U;
    destination->decay1.cc = PRESET_CC_NUMBER_UNUSED;
    destination->decay1.value = 0U;
    destination->decay2.cc = PRESET_CC_NUMBER_UNUSED;
    destination->decay2.value = 0U;
    destination->max_preset = source->max_preset;
}

static void RuntimeConfig_CopyLegacyDeviceV7(RuntimeConfigDevice_t *destination,
                                             const RuntimeConfigDeviceLegacyV7_t *source)
{
    if (!destination || !source)
        return;

    memcpy(destination->name, source->name, sizeof(destination->name));
    destination->channel = source->channel;
    destination->active = source->active;
    destination->bypass = source->bypass;
    destination->tap_tempo = source->tap_tempo;
    destination->volume1 = source->level;
    destination->volume2.cc = PRESET_CC_NUMBER_UNUSED;
    destination->volume2.value = 0U;
    destination->mix1 = source->mix;
    destination->mix2.cc = PRESET_CC_NUMBER_UNUSED;
    destination->mix2.value = 0U;
    destination->decay1.cc = PRESET_CC_NUMBER_UNUSED;
    destination->decay1.value = 0U;
    destination->decay2.cc = PRESET_CC_NUMBER_UNUSED;
    destination->decay2.value = 0U;
    destination->max_preset = source->max_preset;
}

static void RuntimeConfig_CopyLegacyDeviceV8(RuntimeConfigDevice_t *destination,
                                             const RuntimeConfigDeviceLegacyV8_t *source)
{
    if (!destination || !source)
        return;

    memcpy(destination->name, source->name, sizeof(destination->name));
    destination->channel = source->channel;
    destination->active = source->active;
    destination->bypass = source->bypass;
    destination->tap_tempo = source->tap_tempo;
    destination->volume1 = source->level;
    destination->volume2.cc = PRESET_CC_NUMBER_UNUSED;
    destination->volume2.value = 0U;
    destination->mix1 = source->mix1;
    destination->mix2 = source->mix2;
    destination->decay1 = source->decay1;
    destination->decay2 = source->decay2;
    destination->max_preset = source->max_preset;
}

uint8_t RuntimeConfig_PersistentConfigSizeIsSupported(uint32_t config_size)
{
    return (config_size == sizeof(RuntimeConfig_t)
            || config_size == sizeof(RuntimeConfigLegacyWithRoles_t)
            || config_size == sizeof(RuntimeConfigLegacyWithRolesNoGlobalPresets_t)
            || config_size == sizeof(RuntimeConfigLegacyV14_t)
            || config_size == sizeof(RuntimeConfigLegacyV13_t)
            || config_size == sizeof(RuntimeConfigLegacyV12_t)
            || config_size == sizeof(RuntimeConfigLegacyV11_t)
            || config_size == sizeof(RuntimeConfigLegacyV10_t)
            || config_size == sizeof(RuntimeConfigLegacyV9_t)
            || config_size == sizeof(RuntimeConfigLegacyV8_t)
            || config_size == sizeof(RuntimeConfigLegacyV7_t)
         || config_size == sizeof(RuntimeConfigLegacyV6_t)
         || config_size == sizeof(RuntimeConfigLegacyV5_t)
         || config_size == sizeof(RuntimeConfigLegacyNoUserThemes_t)
         || config_size == sizeof(RuntimeConfigLegacyV4_t)
         || config_size == sizeof(RuntimeConfigLegacyV3_t)
         || config_size == sizeof(RuntimeConfigLegacyV2_t)) ? 1U : 0U;
}

static uint16_t *RuntimeConfig_GetMutableUserThemeFieldPointer(RuntimeConfigUserTheme_t *user_theme,
                                                               RuntimeConfigUserThemeField_t field)
{
    if (!user_theme)
        return NULL;

    switch (field)
    {
    case RUNTIME_CONFIG_USER_THEME_FIELD_DISPLAY_BG:
        return &user_theme->display_bg_colour;
    case RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_FOOTBAR:
        return &user_theme->main_footbar_color;
    case RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_FOOTBAR_TEXT:
        return &user_theme->main_footbar_text_colour;
    case RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_INFO_TEXT:
        return &user_theme->main_info_text_colour;
    case RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_INFO_EDIT_CURSOR_TEXT:
        return &user_theme->main_info_edit_cursor_text_colour;
    case RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_INFO_EDIT_CURSOR_BG:
        return &user_theme->main_info_edit_cursor_bg_colour;
    case RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_INFO_EDIT_CURSOR_SHARED_BG:
        return &user_theme->main_info_edit_cursor_shared_bg_colour;
    case RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_SAVING_POPUP_BG:
        return &user_theme->main_saving_popup_bg_colour;
    case RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_SAVING_POPUP_TEXT:
        return &user_theme->main_saving_popup_text_colour;
    case RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_SAVING_POPUP_BORDER:
        return &user_theme->main_saving_popup_border_colour;
    case RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_MODE_HEADER:
        return &user_theme->main_mode_header_colour;
    case RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_MODE_HEADER_EDIT:
        return &user_theme->main_mode_header_edit_colour;
    case RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_MODE_HEADER_EDIT_BG:
        return &user_theme->main_mode_header_edit_bg_colour;
    case RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_PRESET:
        return &user_theme->main_preset_colour;
    case RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_BANK:
        return &user_theme->main_bank_colour;
    case RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_BANK_WET_DRY:
        return &user_theme->main_bank_wet_dry_colour;
    case RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_SPECIAL_FUNCTION_BUTTON_ACTIVE:
        return &user_theme->main_special_function_button_active_colour;
    case RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_SPECIAL_FUNCTION_BUTTON_INACTIVE:
        return &user_theme->main_special_function_button_inactive_colour;
    case RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_SPECIAL_FUNCTION_BUTTON_ACTIVE_BG:
        return &user_theme->main_special_function_button_active_bg;
    case RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_ALERT_BADGE_TEXT:
        return &user_theme->main_alert_badge_text_colour;
    case RUNTIME_CONFIG_USER_THEME_FIELD_BPM_INTERNAL:
        return &user_theme->bpm_internal_colour;
    case RUNTIME_CONFIG_USER_THEME_FIELD_EXT_BPM:
        return &user_theme->ext_bpm_colour;
    case RUNTIME_CONFIG_USER_THEME_FIELD_COUNT:
    default:
        return NULL;
    }
}

static const uint16_t *RuntimeConfig_GetUserThemeFieldPointer(const RuntimeConfigUserTheme_t *user_theme,
                                                              RuntimeConfigUserThemeField_t field)
{
    return RuntimeConfig_GetMutableUserThemeFieldPointer((RuntimeConfigUserTheme_t *)user_theme, field);
}

static uint8_t RuntimeConfig_NormalizeMidiClockBarCount(uint8_t bar_count)
{
    if (bar_count < RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MIN
     || bar_count > RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MAX)
        return RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_DEFAULT;

    return bar_count;
}

static uint8_t RuntimeConfig_NormalizeMetronomeVolume(uint8_t volume)
{
    return (volume <= RUNTIME_CONFIG_METRONOME_VOLUME_MAX)
        ? volume
        : RUNTIME_CONFIG_METRONOME_VOLUME_MAX;
}

static uint8_t RuntimeConfig_NormalizeFeedbackTaperEnabled(uint8_t enabled)
{
    return enabled ? 1U : 0U;
}

static uint8_t RuntimeConfig_NormalizeFeedbackTaperThreshold(uint8_t threshold)
{
    return (threshold <= RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_THRESHOLD_MAX)
        ? threshold
        : RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_THRESHOLD_MAX;
}

static uint8_t RuntimeConfig_NormalizeFeedbackTaperReduce(uint8_t reduce)
{
    return (reduce <= RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_REDUCE_MAX)
        ? reduce
        : RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_REDUCE_MAX;
}

static RuntimeConfigLiveEnc2Mode_t RuntimeConfig_NormalizeLiveEnc2Mode(uint8_t mode)
{
    if (mode > (uint8_t)RUNTIME_CONFIG_LIVE_ENC2_MODE_TIMEBEND)
        return RUNTIME_CONFIG_GLOBAL_LIVE_ENC2_MODE_DEFAULT;

    return (RuntimeConfigLiveEnc2Mode_t)mode;
}

static RuntimeConfigClockMode_t RuntimeConfig_NormalizeClockMode(uint8_t mode)
{
    if (mode > (uint8_t)RUNTIME_CONFIG_CLOCK_MODE_MASTER)
        return RUNTIME_CONFIG_GLOBAL_CLOCK_MODE_DEFAULT;

    return (RuntimeConfigClockMode_t)mode;
}

static RuntimeConfigExpressionPedalMode_t RuntimeConfig_NormalizeExpressionPedalMode(uint8_t mode)
{
    if (mode > (uint8_t)RUNTIME_CONFIG_EXPRESSION_PEDAL_MODE_TIMEBEND)
        return RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_MODE_DEFAULT;

    return (RuntimeConfigExpressionPedalMode_t)mode;
}

static uint16_t RuntimeConfig_NormalizeExpressionPedalRawBound(uint16_t raw)
{
    if (raw > RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MAX)
        return RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MAX;

    return raw;
}

static uint8_t RuntimeConfig_NormalizeExpressionPedalInvert(uint8_t invert)
{
    return invert ? 1U : 0U;
}

static uint8_t RuntimeConfig_NormalizeExpressionPedalCc(uint8_t cc)
{
    return (cc == PRESET_CC_NUMBER_UNUSED || cc <= 127U)
        ? cc
        : PRESET_CC_NUMBER_UNUSED;
}

static uint8_t RuntimeConfig_NormalizeExpressionPedalValue(uint8_t value)
{
    return (value <= 127U) ? value : 127U;
}

static void RuntimeConfig_NormalizeExpressionPedalCcSlots(RuntimeConfigGlobal_t *global)
{
    if (!global)
        return;

    for (uint8_t slot_index = 0U; slot_index < RUNTIME_CONFIG_EXPRESSION_PEDAL_CC_SLOT_COUNT; ++slot_index)
    {
        RuntimeConfigExpressionPedalCcSlot_t *slot = &global->expression_pedal_cc_slots[slot_index];

        slot->cc = RuntimeConfig_NormalizeExpressionPedalCc(slot->cc);
        slot->heel_value = RuntimeConfig_NormalizeExpressionPedalValue(slot->heel_value);
        slot->toe_value = RuntimeConfig_NormalizeExpressionPedalValue(slot->toe_value);
    }
}

static void RuntimeConfig_InitExpressionPedalCcSlotsDefaults(RuntimeConfigGlobal_t *global)
{
    if (!global)
        return;

    for (uint8_t slot_index = 0U; slot_index < RUNTIME_CONFIG_EXPRESSION_PEDAL_CC_SLOT_COUNT; ++slot_index)
    {
        global->expression_pedal_cc_slots[slot_index].cc = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_CC_DEFAULT;
        global->expression_pedal_cc_slots[slot_index].heel_value = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_HEEL_DEFAULT;
        global->expression_pedal_cc_slots[slot_index].toe_value = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_TOE_DEFAULT;
    }
}

static void RuntimeConfig_NormalizeExpressionPedalCalibration(RuntimeConfigGlobal_t *global)
{
    if (!global)
        return;

    global->expression_pedal_min_raw = RuntimeConfig_NormalizeExpressionPedalRawBound(global->expression_pedal_min_raw);
    global->expression_pedal_max_raw = RuntimeConfig_NormalizeExpressionPedalRawBound(global->expression_pedal_max_raw);
    if (global->expression_pedal_min_raw >= global->expression_pedal_max_raw)
    {
        global->expression_pedal_min_raw = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_MIN_RAW_DEFAULT;
        global->expression_pedal_max_raw = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_MAX_RAW_DEFAULT;
    }
    global->expression_pedal_invert = RuntimeConfig_NormalizeExpressionPedalInvert(global->expression_pedal_invert);
    RuntimeConfig_NormalizeExpressionPedalCcSlots(global);
}

static void RuntimeConfig_ApplyLegacyGlobalV9(RuntimeConfigGlobal_t *destination,
                                              const RuntimeConfigGlobalLegacyV9_t *source)
{
    if (!destination || !source)
        return;

    destination->startup_delay_seconds = source->startup_delay_seconds;
    destination->legacy_idle_timeout_minutes = RUNTIME_CONFIG_GLOBAL_LEGACY_IDLE_TIMEOUT_DEFAULT;
    destination->sync_style = source->sync_style;
    destination->display_mode = source->display_mode;
    destination->backlight_brightness = source->backlight_brightness;
    destination->feedback_taper_enabled = RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_ENABLED_DEFAULT;
    destination->feedback_taper_threshold = RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_THRESHOLD_DEFAULT;
    destination->feedback_taper_reduce = RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_REDUCE_DEFAULT;
    destination->clock_mode = RUNTIME_CONFIG_GLOBAL_CLOCK_MODE_DEFAULT;
    destination->live_enc2_mode = RUNTIME_CONFIG_GLOBAL_LIVE_ENC2_MODE_DEFAULT;
    destination->expression_pedal_mode = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_MODE_DEFAULT;
    destination->expression_pedal_min_raw = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_MIN_RAW_DEFAULT;
    destination->expression_pedal_max_raw = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_MAX_RAW_DEFAULT;
    destination->expression_pedal_invert = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_INVERT_DEFAULT;
    RuntimeConfig_InitExpressionPedalCcSlotsDefaults(destination);
}

static void RuntimeConfig_ApplyLegacyGlobalV10(RuntimeConfigGlobal_t *destination,
                                               const RuntimeConfigGlobalLegacyV10_t *source)
{
    if (!destination || !source)
        return;

    destination->startup_delay_seconds = source->startup_delay_seconds;
    destination->legacy_idle_timeout_minutes = RUNTIME_CONFIG_GLOBAL_LEGACY_IDLE_TIMEOUT_DEFAULT;
    destination->sync_style = source->sync_style;
    destination->display_mode = source->display_mode;
    destination->backlight_brightness = source->backlight_brightness;
    destination->feedback_taper_enabled = source->feedback_taper_enabled;
    destination->feedback_taper_threshold = source->feedback_taper_threshold;
    destination->feedback_taper_reduce = source->feedback_taper_reduce;
    destination->clock_mode = RUNTIME_CONFIG_GLOBAL_CLOCK_MODE_DEFAULT;
    destination->live_enc2_mode = RUNTIME_CONFIG_GLOBAL_LIVE_ENC2_MODE_DEFAULT;
    destination->expression_pedal_mode = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_MODE_DEFAULT;
    destination->expression_pedal_min_raw = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_MIN_RAW_DEFAULT;
    destination->expression_pedal_max_raw = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_MAX_RAW_DEFAULT;
    destination->expression_pedal_invert = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_INVERT_DEFAULT;
    RuntimeConfig_InitExpressionPedalCcSlotsDefaults(destination);
}

static void RuntimeConfig_ApplyLegacyGlobalV11(RuntimeConfigGlobal_t *destination,
                                               const RuntimeConfigGlobalLegacyV11_t *source)
{
    if (!destination || !source)
        return;

    destination->startup_delay_seconds = source->startup_delay_seconds;
    destination->legacy_idle_timeout_minutes = RUNTIME_CONFIG_GLOBAL_LEGACY_IDLE_TIMEOUT_DEFAULT;
    destination->sync_style = source->sync_style;
    destination->display_mode = source->display_mode;
    destination->backlight_brightness = source->backlight_brightness;
    destination->feedback_taper_enabled = source->feedback_taper_enabled;
    destination->feedback_taper_threshold = source->feedback_taper_threshold;
    destination->feedback_taper_reduce = source->feedback_taper_reduce;
    destination->clock_mode = RUNTIME_CONFIG_GLOBAL_CLOCK_MODE_DEFAULT;
    destination->live_enc2_mode = source->live_enc2_mode;
    destination->expression_pedal_mode = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_MODE_DEFAULT;
    destination->expression_pedal_min_raw = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_MIN_RAW_DEFAULT;
    destination->expression_pedal_max_raw = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_MAX_RAW_DEFAULT;
    destination->expression_pedal_invert = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_INVERT_DEFAULT;
    RuntimeConfig_InitExpressionPedalCcSlotsDefaults(destination);
}

static void RuntimeConfig_ApplyLegacyGlobalV12(RuntimeConfigGlobal_t *destination,
                                               const RuntimeConfigGlobalLegacyV12_t *source)
{
    if (!destination || !source)
        return;

    destination->startup_delay_seconds = source->startup_delay_seconds;
    destination->legacy_idle_timeout_minutes = RUNTIME_CONFIG_GLOBAL_LEGACY_IDLE_TIMEOUT_DEFAULT;
    destination->sync_style = source->sync_style;
    destination->display_mode = source->display_mode;
    destination->backlight_brightness = source->backlight_brightness;
    destination->feedback_taper_enabled = source->feedback_taper_enabled;
    destination->feedback_taper_threshold = source->feedback_taper_threshold;
    destination->feedback_taper_reduce = source->feedback_taper_reduce;
    destination->clock_mode = RUNTIME_CONFIG_GLOBAL_CLOCK_MODE_DEFAULT;
    destination->live_enc2_mode = source->live_enc2_mode;
    destination->expression_pedal_mode = source->expression_pedal_mode;
    destination->expression_pedal_min_raw = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_MIN_RAW_DEFAULT;
    destination->expression_pedal_max_raw = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_MAX_RAW_DEFAULT;
    destination->expression_pedal_invert = RUNTIME_CONFIG_GLOBAL_EXPRESSION_PEDAL_INVERT_DEFAULT;
    RuntimeConfig_InitExpressionPedalCcSlotsDefaults(destination);
}

static void RuntimeConfig_ApplyLegacyGlobalV13(RuntimeConfigGlobal_t *destination,
                                               const RuntimeConfigGlobalLegacyV13_t *source)
{
    if (!destination || !source)
        return;

    destination->startup_delay_seconds = source->startup_delay_seconds;
    destination->legacy_idle_timeout_minutes = RUNTIME_CONFIG_GLOBAL_LEGACY_IDLE_TIMEOUT_DEFAULT;
    destination->sync_style = source->sync_style;
    destination->display_mode = source->display_mode;
    destination->backlight_brightness = source->backlight_brightness;
    destination->feedback_taper_enabled = source->feedback_taper_enabled;
    destination->feedback_taper_threshold = source->feedback_taper_threshold;
    destination->feedback_taper_reduce = source->feedback_taper_reduce;
    destination->clock_mode = RUNTIME_CONFIG_GLOBAL_CLOCK_MODE_DEFAULT;
    destination->live_enc2_mode = source->live_enc2_mode;
    destination->expression_pedal_mode = source->expression_pedal_mode;
    destination->expression_pedal_min_raw = source->expression_pedal_min_raw;
    destination->expression_pedal_max_raw = source->expression_pedal_max_raw;
    destination->expression_pedal_invert = source->expression_pedal_invert;
    RuntimeConfig_InitExpressionPedalCcSlotsDefaults(destination);
}

static void RuntimeConfig_ApplyLegacyGlobalV14(RuntimeConfigGlobal_t *destination,
                                               const RuntimeConfigGlobalLegacyV14_t *source)
{
    if (!destination || !source)
        return;

    destination->startup_delay_seconds = source->startup_delay_seconds;
    destination->legacy_idle_timeout_minutes = RUNTIME_CONFIG_GLOBAL_LEGACY_IDLE_TIMEOUT_DEFAULT;
    destination->sync_style = source->sync_style;
    destination->display_mode = source->display_mode;
    destination->backlight_brightness = source->backlight_brightness;
    destination->feedback_taper_enabled = source->feedback_taper_enabled;
    destination->feedback_taper_threshold = source->feedback_taper_threshold;
    destination->feedback_taper_reduce = source->feedback_taper_reduce;
    destination->clock_mode = RUNTIME_CONFIG_GLOBAL_CLOCK_MODE_DEFAULT;
    destination->live_enc2_mode = source->live_enc2_mode;
    destination->expression_pedal_mode = source->expression_pedal_mode;
    destination->expression_pedal_min_raw = source->expression_pedal_min_raw;
    destination->expression_pedal_max_raw = source->expression_pedal_max_raw;
    destination->expression_pedal_invert = source->expression_pedal_invert;
    for (uint8_t slot_index = 0U; slot_index < RUNTIME_CONFIG_EXPRESSION_PEDAL_CC_SLOT_COUNT; ++slot_index)
        destination->expression_pedal_cc_slots[slot_index] = source->expression_pedal_cc_slots[slot_index];
}

static RuntimeConfigMetronomePitch_t RuntimeConfig_NormalizeMetronomePitch(uint8_t pitch)
{
    if (pitch > (uint8_t)RUNTIME_CONFIG_METRONOME_PITCH_HIGH)
        return RUNTIME_CONFIG_METRONOME_PITCH_DEFAULT;

    return (RuntimeConfigMetronomePitch_t)pitch;
}

static uint8_t RuntimeConfig_NormalizeMetronomeBeatsPerBar(uint8_t beats_per_bar)
{
    if (beats_per_bar < RUNTIME_CONFIG_METRONOME_BEATS_PER_BAR_MIN
     || beats_per_bar > RUNTIME_CONFIG_METRONOME_BEATS_PER_BAR_MAX)
        return RUNTIME_CONFIG_METRONOME_BEATS_PER_BAR_DEFAULT;

    return beats_per_bar;
}

static RuntimeConfigMetronomeRhythm_t RuntimeConfig_NormalizeMetronomeRhythm(uint8_t rhythm)
{
    if (rhythm > (uint8_t)RUNTIME_CONFIG_METRONOME_RHYTHM_FOUR_EIGHT)
        return RUNTIME_CONFIG_METRONOME_RHYTHM_DEFAULT;

    return (RuntimeConfigMetronomeRhythm_t)rhythm;
}

static uint16_t RuntimeConfig_NormalizeBacklightBrightness(uint16_t brightness)
{
    if (brightness < RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MIN)
        return RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MIN;
    if (brightness > RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MAX)
        return RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MAX;

    return brightness;
}

RuntimeConfigDisplayMode_t RuntimeConfig_NormalizeDisplayMode(uint8_t display_mode)
{
    if (display_mode >= (uint8_t)RUNTIME_CONFIG_DISPLAY_MODE_COUNT)
        return RUNTIME_CONFIG_DISPLAY_MODE_DARK;

    if ((RuntimeConfigDisplayMode_t)display_mode == RUNTIME_CONFIG_DISPLAY_MODE_USER2
     || (RuntimeConfigDisplayMode_t)display_mode == RUNTIME_CONFIG_DISPLAY_MODE_USER3)
        return RUNTIME_CONFIG_DISPLAY_MODE_USER;

    if ((RuntimeConfigDisplayMode_t)display_mode == RUNTIME_CONFIG_DISPLAY_MODE_C64)
        return RUNTIME_CONFIG_DISPLAY_MODE_DARK;

    return (RuntimeConfigDisplayMode_t)display_mode;
}

static RuntimeConfigDisplayMode_t RuntimeConfig_MigrateLegacyDisplayMode(uint8_t display_mode)
{
    switch (display_mode)
    {
    case 0U:
        return RUNTIME_CONFIG_DISPLAY_MODE_DARK;
    case 1U:
        return RUNTIME_CONFIG_DISPLAY_MODE_BRIGHT;
    case 2U:
        return RUNTIME_CONFIG_DISPLAY_MODE_USER;
    case 4U:
        return RUNTIME_CONFIG_DISPLAY_MODE_BLUESCREEN;
    case 6U:
        return RUNTIME_CONFIG_DISPLAY_MODE_TRIPPING;
    case 7U:
        return RUNTIME_CONFIG_DISPLAY_MODE_USER;
    case 8U:
        return RUNTIME_CONFIG_DISPLAY_MODE_USER;
    case 9U:
        return RUNTIME_CONFIG_DISPLAY_MODE_DARK;
    case 10U:
        return RUNTIME_CONFIG_DISPLAY_MODE_STARLIGHT;
    case 3U: /* removed Weed theme */
    case 5U: /* removed Midnight theme */
    default:
        return RUNTIME_CONFIG_DISPLAY_MODE_DARK;
    }
}

static RuntimeConfigDisplayMode_t RuntimeConfig_DecodePersistedDisplayMode(uint8_t display_mode,
                                                                           uint32_t persistent_version)
{
    if (persistent_version >= PERSISTENT_STORE_VERSION_PRESETS_AND_CONFIG_ATOMIC_COMPACT_DISPLAY_MODES)
        return RuntimeConfig_NormalizeDisplayMode(display_mode);

    return RuntimeConfig_MigrateLegacyDisplayMode(display_mode);
}

static uint8_t RuntimeConfig_FindDisplayModeSelectionIndex(RuntimeConfigDisplayMode_t display_mode)
{
    static const RuntimeConfigDisplayMode_t display_mode_selection_order[] = {
        RUNTIME_CONFIG_DISPLAY_MODE_DARK,
        RUNTIME_CONFIG_DISPLAY_MODE_BRIGHT,
        RUNTIME_CONFIG_DISPLAY_MODE_TRIPPING,
        RUNTIME_CONFIG_DISPLAY_MODE_BLUESCREEN,
        RUNTIME_CONFIG_DISPLAY_MODE_STARLIGHT,
        RUNTIME_CONFIG_DISPLAY_MODE_USER,
    };

    for (uint8_t index = 0U; index < (uint8_t)(sizeof(display_mode_selection_order) / sizeof(display_mode_selection_order[0])); ++index)
    {
        if (display_mode_selection_order[index] == display_mode)
            return index;
    }

    return 0U;
}

RuntimeConfigDisplayMode_t RuntimeConfig_StepDisplayMode(RuntimeConfigDisplayMode_t display_mode, int8_t delta)
{
    static const RuntimeConfigDisplayMode_t display_mode_selection_order[] = {
        RUNTIME_CONFIG_DISPLAY_MODE_DARK,
        RUNTIME_CONFIG_DISPLAY_MODE_BRIGHT,
        RUNTIME_CONFIG_DISPLAY_MODE_TRIPPING,
        RUNTIME_CONFIG_DISPLAY_MODE_BLUESCREEN,
        RUNTIME_CONFIG_DISPLAY_MODE_STARLIGHT,
        RUNTIME_CONFIG_DISPLAY_MODE_USER,
    };
    uint8_t selection_index = RuntimeConfig_FindDisplayModeSelectionIndex(
        RuntimeConfig_NormalizeDisplayMode((uint8_t)display_mode));
    uint8_t selection_count = (uint8_t)(sizeof(display_mode_selection_order) / sizeof(display_mode_selection_order[0]));
    uint8_t remaining_steps;

    if (delta == 0)
        return display_mode_selection_order[selection_index];

    remaining_steps = (delta > 0) ? (uint8_t)delta : (uint8_t)(-delta);
    while (remaining_steps-- > 0U)
    {
        if (delta > 0)
            selection_index = (selection_index + 1U < selection_count) ? (uint8_t)(selection_index + 1U) : 0U;
        else
            selection_index = (selection_index > 0U) ? (uint8_t)(selection_index - 1U) : (uint8_t)(selection_count - 1U);
    }

    return display_mode_selection_order[selection_index];
}

static void RuntimeConfig_NormalizeLoadedStore(void)
{
    for (uint8_t bank_index = 0U; bank_index < PRESET_BANK_COUNT; ++bank_index)
    {
        runtime_config_store.banks[bank_index].midi_clock_bar_count = RuntimeConfig_NormalizeMidiClockBarCount(
            runtime_config_store.banks[bank_index].midi_clock_bar_count);
    }

    runtime_config_store.global.backlight_brightness = RuntimeConfig_NormalizeBacklightBrightness(
        runtime_config_store.global.backlight_brightness);
    runtime_config_store.global.legacy_idle_timeout_minutes = RUNTIME_CONFIG_GLOBAL_LEGACY_IDLE_TIMEOUT_DEFAULT;
    runtime_config_store.global.feedback_taper_enabled = RuntimeConfig_NormalizeFeedbackTaperEnabled(
        runtime_config_store.global.feedback_taper_enabled);
    runtime_config_store.global.feedback_taper_threshold = RuntimeConfig_NormalizeFeedbackTaperThreshold(
        runtime_config_store.global.feedback_taper_threshold);
    runtime_config_store.global.feedback_taper_reduce = RuntimeConfig_NormalizeFeedbackTaperReduce(
        runtime_config_store.global.feedback_taper_reduce);
    runtime_config_store.global.clock_mode = RuntimeConfig_NormalizeClockMode(
        (uint8_t)runtime_config_store.global.clock_mode);
    runtime_config_store.global.live_enc2_mode = RuntimeConfig_NormalizeLiveEnc2Mode(
        (uint8_t)runtime_config_store.global.live_enc2_mode);
    runtime_config_store.global.expression_pedal_mode = RuntimeConfig_NormalizeExpressionPedalMode(
        (uint8_t)runtime_config_store.global.expression_pedal_mode);
    RuntimeConfig_NormalizeExpressionPedalCalibration(&runtime_config_store.global);
    runtime_config_store.global.display_mode = RuntimeConfig_NormalizeDisplayMode(
        (uint8_t)runtime_config_store.global.display_mode);
    runtime_config_store.metronome.volume = RuntimeConfig_NormalizeMetronomeVolume(
        runtime_config_store.metronome.volume);
    runtime_config_store.metronome.pitch = RuntimeConfig_NormalizeMetronomePitch(
        (uint8_t)runtime_config_store.metronome.pitch);
    runtime_config_store.metronome.beats_per_bar = RuntimeConfig_NormalizeMetronomeBeatsPerBar(
        runtime_config_store.metronome.beats_per_bar);
    runtime_config_store.metronome.rhythm = RuntimeConfig_NormalizeMetronomeRhythm(
        (uint8_t)runtime_config_store.metronome.rhythm);
}

static void RuntimeConfig_ApplyLegacyV2Snapshot(const RuntimeConfigLegacyV2_t *legacy_store)
{
    if (!legacy_store)
        return;

    RuntimeConfig_ResetUserThemesToDefaults();

    for (uint8_t bank_index = 0U; bank_index < PRESET_BANK_COUNT; ++bank_index)
    {
        memcpy(runtime_config_store.banks[bank_index].name,
               legacy_store->banks[bank_index].name,
               sizeof(runtime_config_store.banks[bank_index].name));
        runtime_config_store.banks[bank_index].wet_dry_enabled = legacy_store->banks[bank_index].wet_dry_enabled;
        runtime_config_store.banks[bank_index].function_button = legacy_store->banks[bank_index].function_button;
        runtime_config_store.banks[bank_index].midi_clock_bar_count = RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_DEFAULT;
    }

    for (uint8_t device_index = 0U; device_index < MIDI_DEVICE_COUNT; ++device_index)
        RuntimeConfig_CopyLegacyDevice(&runtime_config_store.devices[device_index],
                                       &legacy_store->devices[device_index]);

    runtime_config_store.global.startup_delay_seconds = legacy_store->global.startup_delay_seconds;
    runtime_config_store.global.legacy_idle_timeout_minutes = RUNTIME_CONFIG_GLOBAL_LEGACY_IDLE_TIMEOUT_DEFAULT;
    runtime_config_store.global.sync_style = legacy_store->global.sync_style;
    runtime_config_store.global.display_mode = RUNTIME_CONFIG_GLOBAL_DISPLAY_MODE_DEFAULT;
    runtime_config_store.global.backlight_brightness = legacy_store->global.backlight_brightness;
    runtime_config_store.global.feedback_taper_enabled = RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_ENABLED_DEFAULT;
    runtime_config_store.global.feedback_taper_threshold = RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_THRESHOLD_DEFAULT;
    runtime_config_store.global.feedback_taper_reduce = RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_REDUCE_DEFAULT;
    runtime_config_store.global.clock_mode = RUNTIME_CONFIG_GLOBAL_CLOCK_MODE_DEFAULT;
    runtime_config_store.global.live_enc2_mode = RUNTIME_CONFIG_GLOBAL_LIVE_ENC2_MODE_DEFAULT;
    runtime_config_store.metronome = runtime_config_defaults.metronome;
}

static void RuntimeConfig_ApplyLegacyV3Snapshot(const RuntimeConfigLegacyV3_t *legacy_store)
{
    if (!legacy_store)
        return;

    RuntimeConfig_ResetUserThemesToDefaults();

    memcpy(runtime_config_store.banks,
           legacy_store->banks,
           sizeof(runtime_config_store.banks));

    for (uint8_t device_index = 0U; device_index < MIDI_DEVICE_COUNT; ++device_index)
        RuntimeConfig_CopyLegacyDevice(&runtime_config_store.devices[device_index],
                                       &legacy_store->devices[device_index]);

    runtime_config_store.global.startup_delay_seconds = legacy_store->global.startup_delay_seconds;
    runtime_config_store.global.legacy_idle_timeout_minutes = RUNTIME_CONFIG_GLOBAL_LEGACY_IDLE_TIMEOUT_DEFAULT;
    runtime_config_store.global.sync_style = legacy_store->global.sync_style;
    runtime_config_store.global.display_mode = RUNTIME_CONFIG_GLOBAL_DISPLAY_MODE_DEFAULT;
    runtime_config_store.global.backlight_brightness = legacy_store->global.backlight_brightness;
    runtime_config_store.global.feedback_taper_enabled = RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_ENABLED_DEFAULT;
    runtime_config_store.global.feedback_taper_threshold = RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_THRESHOLD_DEFAULT;
    runtime_config_store.global.feedback_taper_reduce = RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_REDUCE_DEFAULT;
    runtime_config_store.global.clock_mode = RUNTIME_CONFIG_GLOBAL_CLOCK_MODE_DEFAULT;
    runtime_config_store.global.live_enc2_mode = RUNTIME_CONFIG_GLOBAL_LIVE_ENC2_MODE_DEFAULT;
    runtime_config_store.metronome = runtime_config_defaults.metronome;
}

static void RuntimeConfig_ApplyLegacyV4Snapshot(const RuntimeConfigLegacyV4_t *legacy_store)
{
    if (!legacy_store)
        return;

    RuntimeConfig_ResetUserThemesToDefaults();

    memcpy(runtime_config_store.banks,
           legacy_store->banks,
           sizeof(runtime_config_store.banks));

    for (uint8_t device_index = 0U; device_index < MIDI_DEVICE_COUNT; ++device_index)
        RuntimeConfig_CopyLegacyDeviceV4(&runtime_config_store.devices[device_index],
                                         &legacy_store->devices[device_index]);

    runtime_config_store.global.startup_delay_seconds = legacy_store->global.startup_delay_seconds;
    runtime_config_store.global.legacy_idle_timeout_minutes = RUNTIME_CONFIG_GLOBAL_LEGACY_IDLE_TIMEOUT_DEFAULT;
    runtime_config_store.global.sync_style = legacy_store->global.sync_style;
    runtime_config_store.global.display_mode = RUNTIME_CONFIG_GLOBAL_DISPLAY_MODE_DEFAULT;
    runtime_config_store.global.backlight_brightness = legacy_store->global.backlight_brightness;
    runtime_config_store.global.feedback_taper_enabled = RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_ENABLED_DEFAULT;
    runtime_config_store.global.feedback_taper_threshold = RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_THRESHOLD_DEFAULT;
    runtime_config_store.global.feedback_taper_reduce = RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_REDUCE_DEFAULT;
    runtime_config_store.global.clock_mode = RUNTIME_CONFIG_GLOBAL_CLOCK_MODE_DEFAULT;
    runtime_config_store.global.live_enc2_mode = RUNTIME_CONFIG_GLOBAL_LIVE_ENC2_MODE_DEFAULT;
    runtime_config_store.metronome = runtime_config_defaults.metronome;
}

static void RuntimeConfig_ApplyLegacyV5Snapshot(const RuntimeConfigLegacyV5_t *legacy_store)
{
    if (!legacy_store)
        return;

    memcpy(runtime_config_store.banks,
           legacy_store->banks,
           sizeof(runtime_config_store.banks));
    for (uint8_t device_index = 0U; device_index < MIDI_DEVICE_COUNT; ++device_index)
        RuntimeConfig_CopyLegacyDeviceV6(&runtime_config_store.devices[device_index],
                                         &legacy_store->devices[device_index]);
    RuntimeConfig_ApplyLegacyGlobalV9(&runtime_config_store.global, &legacy_store->global);
    memcpy(runtime_config_store.user_themes,
           legacy_store->user_themes,
           sizeof(runtime_config_store.user_themes));
    runtime_config_store.metronome = runtime_config_defaults.metronome;
}

static void RuntimeConfig_ApplyLegacyNoUserThemesSnapshot(const RuntimeConfigLegacyNoUserThemes_t *legacy_store)
{
    if (!legacy_store)
        return;

    memcpy(runtime_config_store.banks,
           legacy_store->banks,
           sizeof(runtime_config_store.banks));
    for (uint8_t device_index = 0U; device_index < MIDI_DEVICE_COUNT; ++device_index)
        RuntimeConfig_CopyLegacyDeviceV6(&runtime_config_store.devices[device_index],
                                         &legacy_store->devices[device_index]);
    RuntimeConfig_ApplyLegacyGlobalV9(&runtime_config_store.global, &legacy_store->global);
    runtime_config_store.metronome = runtime_config_defaults.metronome;
    RuntimeConfig_ResetUserThemesToDefaults();
}

static void RuntimeConfig_ApplyLegacyV6Snapshot(const RuntimeConfigLegacyV6_t *legacy_store)
{
    if (!legacy_store)
        return;

    memcpy(runtime_config_store.banks,
           legacy_store->banks,
           sizeof(runtime_config_store.banks));
    for (uint8_t device_index = 0U; device_index < MIDI_DEVICE_COUNT; ++device_index)
        RuntimeConfig_CopyLegacyDeviceV6(&runtime_config_store.devices[device_index],
                                         &legacy_store->devices[device_index]);
    RuntimeConfig_ApplyLegacyGlobalV9(&runtime_config_store.global, &legacy_store->global);
    runtime_config_store.metronome = legacy_store->metronome;
    memcpy(runtime_config_store.user_themes,
           legacy_store->user_themes,
           sizeof(runtime_config_store.user_themes));
}

static void RuntimeConfig_ApplyLegacyV7Snapshot(const RuntimeConfigLegacyV7_t *legacy_store)
{
    if (!legacy_store)
        return;

    memcpy(runtime_config_store.banks,
           legacy_store->banks,
           sizeof(runtime_config_store.banks));
    for (uint8_t device_index = 0U; device_index < MIDI_DEVICE_COUNT; ++device_index)
        RuntimeConfig_CopyLegacyDeviceV7(&runtime_config_store.devices[device_index],
                                         &legacy_store->devices[device_index]);
    RuntimeConfig_ApplyLegacyGlobalV9(&runtime_config_store.global, &legacy_store->global);
    runtime_config_store.metronome = legacy_store->metronome;
    memcpy(runtime_config_store.user_themes,
           legacy_store->user_themes,
           sizeof(runtime_config_store.user_themes));
}

static void RuntimeConfig_ApplyLegacyV8Snapshot(const RuntimeConfigLegacyV8_t *legacy_store)
{
    if (!legacy_store)
        return;

    memcpy(runtime_config_store.banks,
           legacy_store->banks,
           sizeof(runtime_config_store.banks));
    for (uint8_t device_index = 0U; device_index < MIDI_DEVICE_COUNT; ++device_index)
        RuntimeConfig_CopyLegacyDeviceV8(&runtime_config_store.devices[device_index],
                                         &legacy_store->devices[device_index]);
    RuntimeConfig_ApplyLegacyGlobalV9(&runtime_config_store.global, &legacy_store->global);
    runtime_config_store.metronome = legacy_store->metronome;
    memcpy(runtime_config_store.user_themes,
           legacy_store->user_themes,
           sizeof(runtime_config_store.user_themes));
    RuntimeConfig_InitGlobalPresetsForLegacySnapshot();
}

static void RuntimeConfig_ApplyLegacyV9Snapshot(const RuntimeConfigLegacyV9_t *legacy_store)
{
    if (!legacy_store)
        return;

    memcpy(runtime_config_store.banks,
           legacy_store->banks,
           sizeof(runtime_config_store.banks));
    memcpy(runtime_config_store.devices,
           legacy_store->devices,
           sizeof(runtime_config_store.devices));
    RuntimeConfig_ApplyLegacyGlobalV9(&runtime_config_store.global, &legacy_store->global);
    runtime_config_store.metronome = legacy_store->metronome;
    memcpy(runtime_config_store.user_themes,
           legacy_store->user_themes,
           sizeof(runtime_config_store.user_themes));
    /* V9 predates stored global overlay presets, so synthesize them after the
     * rest of the snapshot has been copied into the current layout. */
    RuntimeConfig_InitGlobalPresetsForLegacySnapshot();
}

static void RuntimeConfig_ApplyLegacyV10Snapshot(const RuntimeConfigLegacyV10_t *legacy_store)
{
    if (!legacy_store)
        return;

    memcpy(runtime_config_store.banks,
           legacy_store->banks,
           sizeof(runtime_config_store.banks));
    memcpy(runtime_config_store.devices,
           legacy_store->devices,
           sizeof(runtime_config_store.devices));
    RuntimeConfig_ApplyLegacyGlobalV10(&runtime_config_store.global, &legacy_store->global);
    runtime_config_store.metronome = legacy_store->metronome;
    memcpy(runtime_config_store.user_themes,
           legacy_store->user_themes,
           sizeof(runtime_config_store.user_themes));
    /* V10 has the modern bank/device/global layout but still predates the two
     * editable global overlay presets, so upgrade them the same way as V9. */
    RuntimeConfig_InitGlobalPresetsForLegacySnapshot();
}

static void RuntimeConfig_ApplyLegacyV11Snapshot(const RuntimeConfigLegacyV11_t *legacy_store)
{
    if (!legacy_store)
        return;

    memcpy(runtime_config_store.banks,
           legacy_store->banks,
           sizeof(runtime_config_store.banks));
    memcpy(runtime_config_store.devices,
           legacy_store->devices,
           sizeof(runtime_config_store.devices));
    RuntimeConfig_ApplyLegacyGlobalV11(&runtime_config_store.global, &legacy_store->global);
    runtime_config_store.metronome = legacy_store->metronome;
    memcpy(runtime_config_store.user_themes,
           legacy_store->user_themes,
           sizeof(runtime_config_store.user_themes));
    runtime_config_store.global_bypass_preset = legacy_store->global_bypass_preset;
    runtime_config_store.global_mute_preset = legacy_store->global_mute_preset;
}

static void RuntimeConfig_ApplyLegacyV12Snapshot(const RuntimeConfigLegacyV12_t *legacy_store)
{
    if (!legacy_store)
        return;

    memcpy(runtime_config_store.banks,
           legacy_store->banks,
           sizeof(runtime_config_store.banks));
    memcpy(runtime_config_store.devices,
           legacy_store->devices,
           sizeof(runtime_config_store.devices));
    RuntimeConfig_ApplyLegacyGlobalV12(&runtime_config_store.global, &legacy_store->global);
    runtime_config_store.metronome = legacy_store->metronome;
    memcpy(runtime_config_store.user_themes,
           legacy_store->user_themes,
           sizeof(runtime_config_store.user_themes));
    runtime_config_store.global_bypass_preset = legacy_store->global_bypass_preset;
    runtime_config_store.global_mute_preset = legacy_store->global_mute_preset;
}

static void RuntimeConfig_ApplyLegacyV13Snapshot(const RuntimeConfigLegacyV13_t *legacy_store)
{
    if (!legacy_store)
        return;

    memcpy(runtime_config_store.banks,
           legacy_store->banks,
           sizeof(runtime_config_store.banks));
    memcpy(runtime_config_store.devices,
           legacy_store->devices,
           sizeof(runtime_config_store.devices));
    RuntimeConfig_ApplyLegacyGlobalV13(&runtime_config_store.global, &legacy_store->global);
    runtime_config_store.metronome = legacy_store->metronome;
    memcpy(runtime_config_store.user_themes,
           legacy_store->user_themes,
           sizeof(runtime_config_store.user_themes));
    runtime_config_store.global_bypass_preset = legacy_store->global_bypass_preset;
    runtime_config_store.global_mute_preset = legacy_store->global_mute_preset;
}

static void RuntimeConfig_ApplyLegacyV14Snapshot(const RuntimeConfigLegacyV14_t *legacy_store)
{
    if (!legacy_store)
        return;

    memcpy(runtime_config_store.banks,
           legacy_store->banks,
           sizeof(runtime_config_store.banks));
    memcpy(runtime_config_store.devices,
           legacy_store->devices,
           sizeof(runtime_config_store.devices));
    RuntimeConfig_ApplyLegacyGlobalV14(&runtime_config_store.global, &legacy_store->global);
    runtime_config_store.metronome = legacy_store->metronome;
    memcpy(runtime_config_store.user_themes,
           legacy_store->user_themes,
           sizeof(runtime_config_store.user_themes));
    runtime_config_store.global_bypass_preset = legacy_store->global_bypass_preset;
    runtime_config_store.global_mute_preset = legacy_store->global_mute_preset;
}

static void RuntimeConfig_CopyLegacyBanksWithRoles(const RuntimeConfigBankLegacyWithRoles_t *source)
{
    if (!source)
        return;

    for (uint8_t bank_index = 0U; bank_index < PRESET_BANK_COUNT; ++bank_index)
    {
        memcpy(runtime_config_store.banks[bank_index].name,
               source[bank_index].name,
               sizeof(runtime_config_store.banks[bank_index].name));
        runtime_config_store.banks[bank_index].wet_dry_enabled = source[bank_index].wet_dry_enabled;
        runtime_config_store.banks[bank_index].function_button = source[bank_index].function_button;
        runtime_config_store.banks[bank_index].midi_clock_bar_count = source[bank_index].midi_clock_bar_count;
    }
}

static void RuntimeConfig_ApplyLegacyWithRolesSnapshot(
    const RuntimeConfigLegacyWithRoles_t *legacy_store)
{
    if (!legacy_store)
        return;

    RuntimeConfig_CopyLegacyBanksWithRoles(legacy_store->banks);
    memcpy(runtime_config_store.devices,
           legacy_store->devices,
           sizeof(runtime_config_store.devices));
    RuntimeConfig_ApplyLegacyGlobalV11(&runtime_config_store.global, &legacy_store->global);
    runtime_config_store.metronome = legacy_store->metronome;
    memcpy(runtime_config_store.user_themes,
           legacy_store->user_themes,
           sizeof(runtime_config_store.user_themes));
    runtime_config_store.global_bypass_preset = legacy_store->global_bypass_preset;
    runtime_config_store.global_mute_preset = legacy_store->global_mute_preset;
}

static void RuntimeConfig_ApplyLegacyWithRolesNoGlobalPresetsSnapshot(
    const RuntimeConfigLegacyWithRolesNoGlobalPresets_t *legacy_store)
{
    if (!legacy_store)
        return;

    RuntimeConfig_CopyLegacyBanksWithRoles(legacy_store->banks);
    memcpy(runtime_config_store.devices,
           legacy_store->devices,
           sizeof(runtime_config_store.devices));
    RuntimeConfig_ApplyLegacyGlobalV11(&runtime_config_store.global, &legacy_store->global);
    runtime_config_store.metronome = legacy_store->metronome;
    memcpy(runtime_config_store.user_themes,
           legacy_store->user_themes,
           sizeof(runtime_config_store.user_themes));
    RuntimeConfig_InitGlobalPresetsForLegacySnapshot();
}

static uint32_t RuntimeConfig_FlashChecksum(const uint8_t *data, size_t size)
{
    uint32_t hash = 2166136261UL;

    for (size_t index = 0U; index < size; ++index)
    {
        hash ^= data[index];
        hash *= 16777619UL;
    }

    return hash;
}

static uint8_t RuntimeConfig_FlashHeaderV2IsValid(const PersistentStoreHeaderV2_t *header)
{
    if (!header)
        return 0U;

    if (header->magic != PERSISTENT_STORE_MAGIC_V2
     || header->version != PERSISTENT_STORE_VERSION_PRESETS_AND_CONFIG
     || header->bank_count != PRESET_BANK_COUNT
     || header->presets_per_bank != PRESETS_PER_BANK
     || header->preset_count != PRESET_COUNT
     || !Presets_PersistentPayloadSizeIsSupported(header->payload_size)
            || !RuntimeConfig_PersistentConfigSizeIsSupported(header->config_size))
        return 0U;

    return ((sizeof(PersistentStoreHeaderV2_t)
          + header->payload_size
          + header->config_size) <= PERSISTENT_STORE_FLASH_SIZE_BYTES) ? 1U : 0U;
}

static uint8_t RuntimeConfig_FlashHeaderV3IsValid(const PersistentStoreHeaderV3_t *header)
{
    if (!header)
        return 0U;

    if (header->magic != PERSISTENT_STORE_MAGIC_V3
            || (header->version != PERSISTENT_STORE_VERSION_PRESETS_AND_CONFIG_ATOMIC
                && header->version != PERSISTENT_STORE_VERSION_PRESETS_AND_CONFIG_ATOMIC_COMPACT_DISPLAY_MODES
                && header->version != PERSISTENT_STORE_VERSION_PRESETS_AND_CONFIG_ATOMIC_USER_THEMES
                && header->version != PERSISTENT_STORE_VERSION_PRESETS_AND_CONFIG_ATOMIC_METRONOME)
     || header->commit_marker != PERSISTENT_STORE_COMMIT_MARKER
     || header->bank_count != PRESET_BANK_COUNT
     || header->presets_per_bank != PRESETS_PER_BANK
     || header->preset_count != PRESET_COUNT
     || !Presets_PersistentPayloadSizeIsSupported(header->payload_size)
            || !RuntimeConfig_PersistentConfigSizeIsSupported(header->config_size))
        return 0U;

    return ((sizeof(PersistentStoreHeaderV3_t)
          + header->payload_size
          + header->config_size) <= PERSISTENT_STORE_FLASH_SIZE_BYTES) ? 1U : 0U;
}

static uint8_t RuntimeConfig_FlashGenerationIsNewer(uint32_t candidate, uint32_t reference)
{
    return ((int32_t)(candidate - reference) > 0) ? 1U : 0U;
}

static uint8_t RuntimeConfig_FlashV3ImageIsValid(uint32_t slot_address,
                                                 const PersistentStoreHeaderV3_t **header_out)
{
    const PersistentStoreHeaderV3_t *header = (const PersistentStoreHeaderV3_t *)slot_address;
    const uint8_t *preset_payload;
    const uint8_t *config_payload;

    if (!RuntimeConfig_FlashHeaderV3IsValid(header))
        return 0U;

    preset_payload = (const uint8_t *)(slot_address + sizeof(PersistentStoreHeaderV3_t));
    config_payload = preset_payload + header->payload_size;

    if (RuntimeConfig_FlashChecksum(preset_payload, header->payload_size) != header->checksum)
        return 0U;

    if (RuntimeConfig_FlashChecksum(config_payload, header->config_size) != header->config_checksum)
        return 0U;

    if (header_out)
        *header_out = header;

    return 1U;
}

static const PersistentStoreHeaderV3_t *RuntimeConfig_FindLatestPersistentStoreV3(void)
{
    static const uint32_t slot_addresses[] = {
        PERSISTENT_STORE_SLOT0_FLASH_ADDR,
        PERSISTENT_STORE_SLOT1_FLASH_ADDR,
    };
    const PersistentStoreHeaderV3_t *selected_header = NULL;
    uint32_t selected_generation = 0U;

    for (size_t slot_index = 0U; slot_index < (sizeof(slot_addresses) / sizeof(slot_addresses[0])); ++slot_index)
    {
        const PersistentStoreHeaderV3_t *header = NULL;

        if (!RuntimeConfig_FlashV3ImageIsValid(slot_addresses[slot_index], &header))
            continue;

        if (!selected_header || RuntimeConfig_FlashGenerationIsNewer(header->generation, selected_generation))
        {
            selected_header = header;
            selected_generation = header->generation;
        }
    }

    return selected_header;
}

static void RuntimeConfig_TryLoadPersistentStore(void)
{
    const PersistentStoreHeaderV3_t *header_v3 = RuntimeConfig_FindLatestPersistentStoreV3();
    const PersistentStoreHeaderV2_t *header = (const PersistentStoreHeaderV2_t *)PERSISTENT_STORE_FLASH_ADDR;
    const uint8_t *config_payload;

    RuntimeConfig_ResetPersistentStoreDiagnostic();

    if (header_v3)
    {
        runtime_config_persistent_store_diag_mode = RUNTIME_CONFIG_PERSISTENT_STORE_DIAG_ATOMIC_V3;
        runtime_config_persistent_store_diag_slot = ((uintptr_t)header_v3 == (uintptr_t)PERSISTENT_STORE_SLOT1_FLASH_ADDR) ? 1U : 0U;
        runtime_config_persistent_store_diag_generation = header_v3->generation;
        config_payload = (const uint8_t *)header_v3
                       + sizeof(PersistentStoreHeaderV3_t)
                       + header_v3->payload_size;

        if (header_v3->config_size == sizeof(runtime_config_store))
        {
            memcpy(&runtime_config_store, config_payload, sizeof(runtime_config_store));
            runtime_config_store.global.display_mode = RuntimeConfig_DecodePersistedDisplayMode(
                (uint8_t)runtime_config_store.global.display_mode,
                header_v3->version);
        }
        else if (header_v3->config_size == sizeof(RuntimeConfigLegacyWithRoles_t))
        {
            RuntimeConfigLegacyWithRoles_t legacy_store;

            memcpy(&legacy_store, config_payload, sizeof(legacy_store));
            RuntimeConfig_ApplyLegacyWithRolesSnapshot(&legacy_store);
        }
        else if (header_v3->config_size == sizeof(RuntimeConfigLegacyWithRolesNoGlobalPresets_t))
        {
            RuntimeConfigLegacyWithRolesNoGlobalPresets_t legacy_store;

            memcpy(&legacy_store, config_payload, sizeof(legacy_store));
            RuntimeConfig_ApplyLegacyWithRolesNoGlobalPresetsSnapshot(&legacy_store);
        }
        else if (header_v3->config_size == sizeof(RuntimeConfigLegacyV14_t))
        {
            RuntimeConfigLegacyV14_t legacy_store;

            memcpy(&legacy_store, config_payload, sizeof(legacy_store));
            RuntimeConfig_ApplyLegacyV14Snapshot(&legacy_store);
        }
        else if (header_v3->config_size == sizeof(RuntimeConfigLegacyV13_t))
        {
            RuntimeConfigLegacyV13_t legacy_store;

            memcpy(&legacy_store, config_payload, sizeof(legacy_store));
            RuntimeConfig_ApplyLegacyV13Snapshot(&legacy_store);
        }
        else if (header_v3->config_size == sizeof(RuntimeConfigLegacyV12_t))
        {
            RuntimeConfigLegacyV12_t legacy_store;

            memcpy(&legacy_store, config_payload, sizeof(legacy_store));
            RuntimeConfig_ApplyLegacyV12Snapshot(&legacy_store);
        }
        else if (header_v3->config_size == sizeof(RuntimeConfigLegacyV11_t))
        {
            RuntimeConfigLegacyV11_t legacy_store;

            memcpy(&legacy_store, config_payload, sizeof(legacy_store));
            RuntimeConfig_ApplyLegacyV11Snapshot(&legacy_store);
        }
        else if (header_v3->config_size == sizeof(RuntimeConfigLegacyV10_t))
        {
            RuntimeConfigLegacyV10_t legacy_store;

            memcpy(&legacy_store, config_payload, sizeof(legacy_store));
            RuntimeConfig_ApplyLegacyV10Snapshot(&legacy_store);
        }
        else if (header_v3->config_size == sizeof(RuntimeConfigLegacyV9_t))
        {
            RuntimeConfigLegacyV9_t legacy_store;

            memcpy(&legacy_store, config_payload, sizeof(legacy_store));
            RuntimeConfig_ApplyLegacyV9Snapshot(&legacy_store);
        }
        else if (header_v3->config_size == sizeof(RuntimeConfigLegacyV8_t))
        {
            RuntimeConfigLegacyV8_t legacy_store;

            memcpy(&legacy_store, config_payload, sizeof(legacy_store));
            RuntimeConfig_ApplyLegacyV8Snapshot(&legacy_store);
        }
        else if (header_v3->config_size == sizeof(RuntimeConfigLegacyV7_t))
        {
            RuntimeConfigLegacyV7_t legacy_store;

            memcpy(&legacy_store, config_payload, sizeof(legacy_store));
            RuntimeConfig_ApplyLegacyV7Snapshot(&legacy_store);
        }
        else if (header_v3->config_size == sizeof(RuntimeConfigLegacyV6_t))
        {
            RuntimeConfigLegacyV6_t legacy_store;

            memcpy(&legacy_store, config_payload, sizeof(legacy_store));
            RuntimeConfig_ApplyLegacyV6Snapshot(&legacy_store);
        }
        else if (header_v3->config_size == sizeof(RuntimeConfigLegacyV5_t))
        {
            RuntimeConfigLegacyV5_t legacy_store;

            memcpy(&legacy_store, config_payload, sizeof(legacy_store));
            RuntimeConfig_ApplyLegacyV5Snapshot(&legacy_store);
        }
        else if (header_v3->config_size == sizeof(RuntimeConfigLegacyNoUserThemes_t))
        {
            RuntimeConfigLegacyNoUserThemes_t legacy_store;

            memcpy(&legacy_store, config_payload, sizeof(legacy_store));
            RuntimeConfig_ApplyLegacyNoUserThemesSnapshot(&legacy_store);
        }
        else if (header_v3->config_size == sizeof(RuntimeConfigLegacyV4_t))
        {
            RuntimeConfigLegacyV4_t legacy_store;

            memcpy(&legacy_store, config_payload, sizeof(legacy_store));
            RuntimeConfig_ApplyLegacyV4Snapshot(&legacy_store);
        }
        else if (header_v3->config_size == sizeof(RuntimeConfigLegacyV3_t))
        {
            RuntimeConfigLegacyV3_t legacy_store;

            memcpy(&legacy_store, config_payload, sizeof(legacy_store));
            RuntimeConfig_ApplyLegacyV3Snapshot(&legacy_store);
        }
        else
        {
            RuntimeConfigLegacyV2_t legacy_store;

            memcpy(&legacy_store, config_payload, sizeof(legacy_store));
            RuntimeConfig_ApplyLegacyV2Snapshot(&legacy_store);
        }

        RuntimeConfig_NormalizeLoadedStore();
        return;
    }

    if (!RuntimeConfig_FlashHeaderV2IsValid(header))
    {
        if (!RuntimeConfig_PersistentStoreSlotsLookBlank())
            runtime_config_persistent_store_save_blocked = 1U;
        return;
    }

    config_payload = (const uint8_t *)(PERSISTENT_STORE_FLASH_ADDR
                                     + sizeof(PersistentStoreHeaderV2_t)
                                     + header->payload_size);

    if (RuntimeConfig_FlashChecksum(config_payload, header->config_size) != header->config_checksum)
        return;

    runtime_config_persistent_store_diag_mode = RUNTIME_CONFIG_PERSISTENT_STORE_DIAG_LEGACY_V2;

    if (header->config_size == sizeof(runtime_config_store))
    {
        memcpy(&runtime_config_store, config_payload, sizeof(runtime_config_store));
        runtime_config_store.global.display_mode = RuntimeConfig_DecodePersistedDisplayMode(
            (uint8_t)runtime_config_store.global.display_mode,
            header->version);
    }
    else if (header->config_size == sizeof(RuntimeConfigLegacyWithRoles_t))
    {
        RuntimeConfigLegacyWithRoles_t legacy_store;

        memcpy(&legacy_store, config_payload, sizeof(legacy_store));
        RuntimeConfig_ApplyLegacyWithRolesSnapshot(&legacy_store);
    }
    else if (header->config_size == sizeof(RuntimeConfigLegacyWithRolesNoGlobalPresets_t))
    {
        RuntimeConfigLegacyWithRolesNoGlobalPresets_t legacy_store;

        memcpy(&legacy_store, config_payload, sizeof(legacy_store));
        RuntimeConfig_ApplyLegacyWithRolesNoGlobalPresetsSnapshot(&legacy_store);
    }
    else if (header->config_size == sizeof(RuntimeConfigLegacyV14_t))
    {
        RuntimeConfigLegacyV14_t legacy_store;

        memcpy(&legacy_store, config_payload, sizeof(legacy_store));
        RuntimeConfig_ApplyLegacyV14Snapshot(&legacy_store);
    }
    else if (header->config_size == sizeof(RuntimeConfigLegacyV13_t))
    {
        RuntimeConfigLegacyV13_t legacy_store;

        memcpy(&legacy_store, config_payload, sizeof(legacy_store));
        RuntimeConfig_ApplyLegacyV13Snapshot(&legacy_store);
    }
    else if (header->config_size == sizeof(RuntimeConfigLegacyV12_t))
    {
        RuntimeConfigLegacyV12_t legacy_store;

        memcpy(&legacy_store, config_payload, sizeof(legacy_store));
        RuntimeConfig_ApplyLegacyV12Snapshot(&legacy_store);
    }
    else if (header->config_size == sizeof(RuntimeConfigLegacyV11_t))
    {
        RuntimeConfigLegacyV11_t legacy_store;

        memcpy(&legacy_store, config_payload, sizeof(legacy_store));
        RuntimeConfig_ApplyLegacyV11Snapshot(&legacy_store);
    }
    else if (header->config_size == sizeof(RuntimeConfigLegacyV10_t))
    {
        RuntimeConfigLegacyV10_t legacy_store;

        memcpy(&legacy_store, config_payload, sizeof(legacy_store));
        RuntimeConfig_ApplyLegacyV10Snapshot(&legacy_store);
    }
    else if (header->config_size == sizeof(RuntimeConfigLegacyV9_t))
    {
        RuntimeConfigLegacyV9_t legacy_store;

        memcpy(&legacy_store, config_payload, sizeof(legacy_store));
        RuntimeConfig_ApplyLegacyV9Snapshot(&legacy_store);
    }
    else if (header->config_size == sizeof(RuntimeConfigLegacyV8_t))
    {
        RuntimeConfigLegacyV8_t legacy_store;

        memcpy(&legacy_store, config_payload, sizeof(legacy_store));
        RuntimeConfig_ApplyLegacyV8Snapshot(&legacy_store);
    }
    else if (header->config_size == sizeof(RuntimeConfigLegacyV7_t))
    {
        RuntimeConfigLegacyV7_t legacy_store;

        memcpy(&legacy_store, config_payload, sizeof(legacy_store));
        RuntimeConfig_ApplyLegacyV7Snapshot(&legacy_store);
    }
    else if (header->config_size == sizeof(RuntimeConfigLegacyV6_t))
    {
        RuntimeConfigLegacyV6_t legacy_store;

        memcpy(&legacy_store, config_payload, sizeof(legacy_store));
        RuntimeConfig_ApplyLegacyV6Snapshot(&legacy_store);
    }
    else if (header->config_size == sizeof(RuntimeConfigLegacyV5_t))
    {
        RuntimeConfigLegacyV5_t legacy_store;

        memcpy(&legacy_store, config_payload, sizeof(legacy_store));
        RuntimeConfig_ApplyLegacyV5Snapshot(&legacy_store);
    }
    else if (header->config_size == sizeof(RuntimeConfigLegacyNoUserThemes_t))
    {
        RuntimeConfigLegacyNoUserThemes_t legacy_store;

        memcpy(&legacy_store, config_payload, sizeof(legacy_store));
        RuntimeConfig_ApplyLegacyNoUserThemesSnapshot(&legacy_store);
    }
    else if (header->config_size == sizeof(RuntimeConfigLegacyV4_t))
    {
        RuntimeConfigLegacyV4_t legacy_store;

        memcpy(&legacy_store, config_payload, sizeof(legacy_store));
        RuntimeConfig_ApplyLegacyV4Snapshot(&legacy_store);
    }
    else if (header->config_size == sizeof(RuntimeConfigLegacyV3_t))
    {
        RuntimeConfigLegacyV3_t legacy_store;

        memcpy(&legacy_store, config_payload, sizeof(legacy_store));
        RuntimeConfig_ApplyLegacyV3Snapshot(&legacy_store);
    }
    else
    {
        RuntimeConfigLegacyV2_t legacy_store;

        memcpy(&legacy_store, config_payload, sizeof(legacy_store));
        RuntimeConfig_ApplyLegacyV2Snapshot(&legacy_store);
    }

    RuntimeConfig_NormalizeLoadedStore();
}

static void RuntimeConfig_EnsureInitialized(void)
{
    if (runtime_config_initialized)
        return;

    memcpy(&runtime_config_store, &runtime_config_defaults, sizeof(runtime_config_store));
    RuntimeConfig_TryLoadPersistentStore();
    runtime_config_initialized = 1U;
    runtime_config_dirty = 0U;
    RuntimeConfig_SyncPersistedMetronome();
}

void RuntimeConfig_Init(void)
{
    RuntimeConfig_EnsureInitialized();
}

__attribute__((section(".RamFunc")))
uint8_t RuntimeConfig_GetMidiClockBarCountFast(uint8_t bank_index)
{
    uint8_t bar_count;

    if (!runtime_config_initialized || bank_index >= PRESET_BANK_COUNT)
        return RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_DEFAULT;

    bar_count = runtime_config_store.banks[bank_index].midi_clock_bar_count;
    if (bar_count < RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MIN
     || bar_count > RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MAX)
    {
        return RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_DEFAULT;
    }

    return bar_count;
}

const RuntimeConfig_t *RuntimeConfig_Get(void)
{
    RuntimeConfig_EnsureInitialized();
    return &runtime_config_store;
}

RuntimeConfig_t *RuntimeConfig_GetMutable(void)
{
    RuntimeConfig_EnsureInitialized();
    return &runtime_config_store;
}

const RuntimeConfigBank_t *RuntimeConfig_GetBank(uint8_t bank_index)
{
    RuntimeConfig_EnsureInitialized();

    if (bank_index >= PRESET_BANK_COUNT)
        return &runtime_config_blank_bank;

    return &runtime_config_store.banks[bank_index];
}

RuntimeConfigBank_t *RuntimeConfig_GetMutableBank(uint8_t bank_index)
{
    RuntimeConfig_EnsureInitialized();

    if (bank_index >= PRESET_BANK_COUNT)
        return NULL;

    return &runtime_config_store.banks[bank_index];
}

void RuntimeConfig_ResetBankToDefaults(uint8_t bank_index)
{
    RuntimeConfig_EnsureInitialized();

    if (bank_index >= PRESET_BANK_COUNT)
        return;

    runtime_config_store.banks[bank_index] = runtime_config_defaults.banks[bank_index];
}

const RuntimeConfigFunctionButton_t *RuntimeConfig_GetFunctionButton(uint8_t bank_index)
{
    return &RuntimeConfig_GetBank(bank_index)->function_button;
}

RuntimeConfigFunctionButton_t *RuntimeConfig_GetMutableFunctionButton(uint8_t bank_index)
{
    RuntimeConfigBank_t *bank = RuntimeConfig_GetMutableBank(bank_index);

    if (!bank)
        return NULL;

    return &bank->function_button;
}

const RuntimeConfigDevice_t *RuntimeConfig_GetDevice(uint8_t device_index)
{
    RuntimeConfig_EnsureInitialized();

    if (device_index >= MIDI_DEVICE_COUNT)
        return &runtime_config_blank_device;

    return &runtime_config_store.devices[device_index];
}

RuntimeConfigDevice_t *RuntimeConfig_GetMutableDevice(uint8_t device_index)
{
    RuntimeConfig_EnsureInitialized();

    if (device_index >= MIDI_DEVICE_COUNT)
        return NULL;

    return &runtime_config_store.devices[device_index];
}

void RuntimeConfig_ResetDeviceToDefaults(uint8_t device_index)
{
    RuntimeConfig_EnsureInitialized();

    if (device_index >= MIDI_DEVICE_COUNT)
        return;

    runtime_config_store.devices[device_index] = runtime_config_defaults.devices[device_index];
}

const Preset_t *RuntimeConfig_GetGlobalBypassPreset(void)
{
    RuntimeConfig_EnsureInitialized();
    return &runtime_config_store.global_bypass_preset;
}

Preset_t *RuntimeConfig_GetMutableGlobalBypassPreset(void)
{
    RuntimeConfig_EnsureInitialized();
    return &runtime_config_store.global_bypass_preset;
}

const Preset_t *RuntimeConfig_GetGlobalMutePreset(void)
{
    RuntimeConfig_EnsureInitialized();
    return &runtime_config_store.global_mute_preset;
}

Preset_t *RuntimeConfig_GetMutableGlobalMutePreset(void)
{
    RuntimeConfig_EnsureInitialized();
    return &runtime_config_store.global_mute_preset;
}

void RuntimeConfig_ResetGlobalBypassPresetToDefaults(void)
{
    RuntimeConfig_EnsureInitialized();
    runtime_config_store.global_bypass_preset = runtime_config_global_bypass_preset_default;
}

void RuntimeConfig_ResetGlobalMutePresetToDefaults(void)
{
    RuntimeConfig_EnsureInitialized();
    runtime_config_store.global_mute_preset = runtime_config_global_mute_preset_default;
}

const RuntimeConfigGlobal_t *RuntimeConfig_GetGlobal(void)
{
    RuntimeConfig_EnsureInitialized();
    return &runtime_config_store.global;
}

RuntimeConfigGlobal_t *RuntimeConfig_GetMutableGlobal(void)
{
    RuntimeConfig_EnsureInitialized();
    return &runtime_config_store.global;
}

const RuntimeConfigMetronome_t *RuntimeConfig_GetMetronome(void)
{
    RuntimeConfig_EnsureInitialized();
    return &runtime_config_store.metronome;
}

RuntimeConfigMetronome_t *RuntimeConfig_GetMutableMetronome(void)
{
    RuntimeConfig_EnsureInitialized();
    return &runtime_config_store.metronome;
}

uint8_t RuntimeConfig_TryGetUserThemeIndex(RuntimeConfigDisplayMode_t display_mode, uint8_t *theme_index)
{
    switch (RuntimeConfig_NormalizeDisplayMode((uint8_t)display_mode))
    {
    case RUNTIME_CONFIG_DISPLAY_MODE_USER:
        if (theme_index)
            *theme_index = 0U;
        return 1U;
    default:
        return 0U;
    }
}

const RuntimeConfigUserTheme_t *RuntimeConfig_GetUserTheme(RuntimeConfigDisplayMode_t display_mode)
{
    uint8_t theme_index;

    RuntimeConfig_EnsureInitialized();

    if (!RuntimeConfig_TryGetUserThemeIndex(display_mode, &theme_index))
        return NULL;

    return &runtime_config_store.user_themes[theme_index];
}

RuntimeConfigUserTheme_t *RuntimeConfig_GetMutableUserTheme(RuntimeConfigDisplayMode_t display_mode)
{
    uint8_t theme_index;

    RuntimeConfig_EnsureInitialized();

    if (!RuntimeConfig_TryGetUserThemeIndex(display_mode, &theme_index))
        return NULL;

    return &runtime_config_store.user_themes[theme_index];
}

uint16_t RuntimeConfig_GetUserThemeColour(const RuntimeConfigUserTheme_t *user_theme,
                                          RuntimeConfigUserThemeField_t field)
{
    const uint16_t *colour = RuntimeConfig_GetUserThemeFieldPointer(user_theme, field);

    return colour ? *colour : 0U;
}

uint8_t RuntimeConfig_SetUserThemeColour(RuntimeConfigUserTheme_t *user_theme,
                                         RuntimeConfigUserThemeField_t field,
                                         uint16_t colour)
{
    uint16_t *target = RuntimeConfig_GetMutableUserThemeFieldPointer(user_theme, field);

    if (!target || *target == colour)
        return 0U;

    *target = colour;
    return 1U;
}

void RuntimeConfig_MarkDirty(void)
{
    RuntimeConfig_EnsureInitialized();
    runtime_config_dirty = 1U;
}

void RuntimeConfig_MarkMetronomeDirty(void)
{
    RuntimeConfig_EnsureInitialized();
    runtime_config_metronome_dirty = (memcmp(&runtime_config_store.metronome,
                                             &runtime_config_persisted_metronome,
                                             sizeof(runtime_config_store.metronome)) != 0) ? 1U : 0U;
}

uint8_t RuntimeConfig_IsDirty(void)
{
    RuntimeConfig_EnsureInitialized();
    return (runtime_config_dirty
         || (runtime_config_metronome_dirty && !RuntimeConfig_ShouldDeferMetronomePersistence())) ? 1U : 0U;
}

void RuntimeConfig_ClearDirty(void)
{
    RuntimeConfig_EnsureInitialized();
    runtime_config_dirty = 0U;

    if (!RuntimeConfig_ShouldDeferMetronomePersistence())
        RuntimeConfig_SyncPersistedMetronome();
}

uint8_t RuntimeConfig_SaveIfDirty(void)
{
    RuntimeConfig_EnsureInitialized();

    if (!RuntimeConfig_IsDirty())
        return 1U;

    return Presets_SaveIfDirty();
}

uint8_t RuntimeConfig_PersistentStoreSaveIsBlocked(void)
{
    RuntimeConfig_EnsureInitialized();
    return runtime_config_persistent_store_save_blocked;
}

uint8_t RuntimeConfig_PersistentSnapshotLooksFactoryDefault(const RuntimeConfig_t *snapshot)
{
    if (!snapshot)
        return 0U;

    return (memcmp(snapshot, &runtime_config_defaults, sizeof(runtime_config_defaults)) == 0) ? 1U : 0U;
}

void RuntimeConfig_CopyPersistentSaveSnapshot(RuntimeConfig_t *snapshot)
{
    RuntimeConfig_EnsureInitialized();

    if (!snapshot)
        return;

    *snapshot = runtime_config_store;
    if (RuntimeConfig_ShouldDeferMetronomePersistence())
        snapshot->metronome = runtime_config_persisted_metronome;
}

void RuntimeConfig_FormatPersistentStoreStatusText(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0U)
        return;

    RuntimeConfig_EnsureInitialized();

    switch (runtime_config_persistent_store_diag_mode)
    {
    case RUNTIME_CONFIG_PERSISTENT_STORE_DIAG_ATOMIC_V3:
        (void)snprintf(buffer,
                       buffer_size,
                       "img%c %04lX",
                       runtime_config_persistent_store_diag_slot ? 'B' : 'A',
                       (unsigned long)(runtime_config_persistent_store_diag_generation & 0xFFFFUL));
        break;

    case RUNTIME_CONFIG_PERSISTENT_STORE_DIAG_LEGACY_V2:
        (void)snprintf(buffer, buffer_size, "img legacy");
        break;

    default:
        (void)snprintf(buffer, buffer_size, "img default");
        break;
    }
}

void RuntimeConfig_FormatPersistentStoreHealthText(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0U)
        return;

    RuntimeConfig_EnsureInitialized();

    switch (runtime_config_persistent_store_diag_mode)
    {
    case RUNTIME_CONFIG_PERSISTENT_STORE_DIAG_ATOMIC_V3:
        (void)snprintf(buffer, buffer_size, "flash: valid crc");
        break;

    case RUNTIME_CONFIG_PERSISTENT_STORE_DIAG_LEGACY_V2:
        (void)snprintf(buffer, buffer_size, "flash: legacy ok");
        break;

    default:
        if (runtime_config_persistent_store_save_blocked)
            (void)snprintf(buffer, buffer_size, "flash: save blocked");
        else
            (void)snprintf(buffer, buffer_size, "flash: blank defaults");
        break;
    }
}

void RuntimeConfig_PrintPersistentStoreDiagnostics(void)
{
    RuntimeConfig_EnsureInitialized();

    RuntimeConfig_PrintPersistentStoreSlotDiagnostic('A', PERSISTENT_STORE_SLOT0_FLASH_ADDR);
    RuntimeConfig_PrintPersistentStoreSlotDiagnostic('B', PERSISTENT_STORE_SLOT1_FLASH_ADDR);

    switch (runtime_config_persistent_store_diag_mode)
    {
    case RUNTIME_CONFIG_PERSISTENT_STORE_DIAG_ATOMIC_V3:
        printf("PSTORE loaded=atomic slot=%c gen=%lu reason=valid_crc\r\n",
               runtime_config_persistent_store_diag_slot ? 'B' : 'A',
               (unsigned long)runtime_config_persistent_store_diag_generation);
        break;

    case RUNTIME_CONFIG_PERSISTENT_STORE_DIAG_LEGACY_V2:
        printf("PSTORE loaded=legacy reason=valid_crc\r\n");
        break;

    default:
        printf("PSTORE loaded=default reason=%s\r\n",
               runtime_config_persistent_store_save_blocked ? "unreadable_store" : "blank_store");
        break;
    }

    if (runtime_config_persistent_store_save_blocked)
        printf("PSTORE save=blocked reason=loaded_defaults\r\n");
}

void RuntimeConfig_ApplySnapshot(const RuntimeConfig_t *snapshot)
{
    if (!snapshot)
        return;

    memcpy(&runtime_config_store, snapshot, sizeof(runtime_config_store));
    runtime_config_initialized = 1U;
    runtime_config_dirty = 0U;
    RuntimeConfig_SyncPersistedMetronome();
}

void RuntimeConfig_ResetToDefaults(void)
{
    memcpy(&runtime_config_store, &runtime_config_defaults, sizeof(runtime_config_store));
    runtime_config_initialized = 1U;
    runtime_config_dirty = 0U;
    RuntimeConfig_SyncPersistedMetronome();
}
