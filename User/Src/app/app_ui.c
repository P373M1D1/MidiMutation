#include "app/app_ui.h"

#include "app_event.h"
#include "app/app_requests.h"
#include "app/app_state.h"
#include "display_functions.h"
#include "midi/midi_monitor.h"
#include "midi_devices.h"
#include "midi_functions.h"
#include "runtime_config.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>

static const char AppUi_PresetEditNameCharset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz ";
#define APP_UI_PRESET_LEARN_VALUE_TIMEOUT_MS 3000U
#define APP_UI_RANDOM_SAVE_POPUP_CHOOSE_SLOT "CHOOSE SAVE SLOT"
#define APP_UI_RANDOM_SAVE_POPUP_CONFIRM_OVERWRITE "CONFIRM OVERWRITE"

typedef enum
{
    APP_UI_RANDOM_SAVE_STATE_IDLE = 0,
    APP_UI_RANDOM_SAVE_STATE_SLOT_SELECT,
    APP_UI_RANDOM_SAVE_STATE_CONFIRM_OVERWRITE,
} AppUiRandomSaveState_t;

typedef struct {
    uint8_t active;
    DisplayPresetEditField_t field;
    uint32_t last_valid_cc_tick;
    uint32_t last_seen_monitor_revision;
} AppUiPresetLearnState_t;

static AppUiPresetLearnState_t app_ui_preset_learn_state = {
    .active = 0U,
    .field = { DISPLAY_PRESET_EDIT_FIELD_NONE, 0U },
    .last_valid_cc_tick = 0U,
    .last_seen_monitor_revision = 0U,
};

typedef struct {
    AppUiRandomSaveState_t state;
    uint8_t selected_slot;
} AppUiRandomSaveContext_t;

static AppUiRandomSaveContext_t app_ui_random_save = {
    .state = APP_UI_RANDOM_SAVE_STATE_IDLE,
    .selected_slot = 0U,
};

static uint8_t AppUi_PresetEditAdjustSentinelValue(uint8_t *value,
                                                   uint8_t unused_value,
                                                   uint8_t min_value,
                                                   uint8_t max_value,
                                                   int8_t delta);
static int16_t AppUi_PresetEditFindNameCharsetIndex(char ch);
static void AppUi_PresetEditLoadNameCells(const Preset_t *preset, char *name_cells);
static void AppUi_PresetEditStoreNameCells(Preset_t *preset, const char *name_cells);
static uint8_t AppUi_PresetEditAdjustNameCharacter(Preset_t *preset, int8_t delta);
static uint8_t AppUi_PresetEditAdjustProgramValue(Preset_t *preset, uint8_t slot, int8_t delta);
static const Preset_t *AppUi_GetEditableActivePreset(void);
static Preset_t *AppUi_GetMutableEditableActivePreset(void);
static uint8_t AppUi_GetEditableActivePresetIndex(uint8_t *preset_index);
static uint8_t AppUi_PresetEditFieldSupportsLearning(DisplayPresetEditField_t field);
static void AppUi_PresetEditStopLearningSession(void);
static uint8_t AppUi_RandomSaveCurrentPresetIsRandomOverlay(void);
static uint8_t AppUi_RandomSavePresetHasAnyProgram(const Preset_t *preset);
static uint8_t AppUi_RandomSavePresetHasAnyCc(const Preset_t *preset);
static uint8_t AppUi_RandomSavePresetHasAnyRelayState(const Preset_t *preset);
static uint8_t AppUi_RandomSavePresetSlotLooksEmpty(const Preset_t *preset, uint8_t slot_index);
static uint8_t AppUi_RandomSaveSlotRequiresOverwrite(uint8_t slot_index);
static uint8_t AppUi_RandomSaveCommitToSlot(uint8_t slot_index);
static uint8_t AppUi_RandomSaveEnterNameEditForSlot(uint8_t slot_index);

static uint8_t AppUi_PresetEditFieldSupportsLearning(DisplayPresetEditField_t field)
{
    switch (field.type)
    {
    case DISPLAY_PRESET_EDIT_FIELD_PROGRAM:
    case DISPLAY_PRESET_EDIT_FIELD_CC_NUMBER:
    case DISPLAY_PRESET_EDIT_FIELD_CC_VALUE:
        return 1U;

    default:
        return 0U;
    }
}

static void AppUi_PresetEditStopLearningSession(void)
{
    if (!app_ui_preset_learn_state.active)
        return;

    app_ui_preset_learn_state.active = 0U;
    app_ui_preset_learn_state.field.type = DISPLAY_PRESET_EDIT_FIELD_NONE;
    app_ui_preset_learn_state.field.itemIndex = 0U;
    app_ui_preset_learn_state.last_valid_cc_tick = 0U;
    app_ui_preset_learn_state.last_seen_monitor_revision = 0U;

    Display_HideLearningPopup(AppUi_GetEditableActivePreset());
}

static const Preset_t *AppUi_GetEditableActivePreset(void)
{
    const Preset_t *active_preset = AppState_GetActivePreset();

    if (!active_preset)
        return NULL;

    /* A preset is editable when the live selection points at either a normal
     * bank slot or one of the runtime-owned global overlay presets. */
    if (active_preset == Presets_Get(AppState_GetActivePresetIndex()))
        return active_preset;

    if (Presets_IsGlobalBypassPreset(active_preset) || Presets_IsGlobalMutePreset(active_preset))
        return active_preset;

    return NULL;
}

static Preset_t *AppUi_GetMutableEditableActivePreset(void)
{
    const Preset_t *active_preset = AppState_GetActivePreset();

    if (!active_preset)
        return NULL;

    if (active_preset == Presets_Get(AppState_GetActivePresetIndex()))
        return Presets_GetMutable(AppState_GetActivePresetIndex());

    /* Overlay presets do not live in preset_store, so route edits back to the
     * owning runtime-config objects instead of returning a stale bank slot. */
    if (Presets_IsGlobalBypassPreset(active_preset))
        return Presets_GetMutableGlobalBypassPreset();

    if (Presets_IsGlobalMutePreset(active_preset))
        return Presets_GetMutableGlobalMutePreset();

    return NULL;
}

static uint8_t AppUi_GetEditableActivePresetIndex(uint8_t *preset_index)
{
    const Preset_t *active_preset = AppUi_GetEditableActivePreset();

    if (!active_preset || !preset_index)
        return 0U;

    if (Presets_IsGlobalBypassPreset(active_preset))
    {
        *preset_index = PRESET_GLOBAL_BYPASS_INDEX;
        return 1U;
    }

    if (Presets_IsGlobalMutePreset(active_preset))
    {
        *preset_index = PRESET_GLOBAL_MUTE_INDEX;
        return 1U;
    }

    *preset_index = AppState_GetActivePresetIndex();
    return 1U;
}

uint8_t AppUi_PresetEditCurrentPresetIsEditable(void)
{
    return AppUi_GetEditableActivePreset() ? 1U : 0U;
}

static uint8_t AppUi_PresetEditAdjustSentinelValue(uint8_t *value,
                                                   uint8_t unused_value,
                                                   uint8_t min_value,
                                                   uint8_t max_value,
                                                   int8_t delta)
{
    int16_t current_value;
    int16_t next_value;
    int16_t unused_marker = (int16_t)min_value - 1;

    if (delta == 0)
        return 0U;

    /* Treat the sentinel as one step below the legal range so encoder turns can
     * move cleanly between "unused" and the first valid numeric value. */
    current_value = (*value == unused_value) ? unused_marker : (int16_t)(*value);
    next_value = current_value + (int16_t)delta;

    if (next_value < unused_marker)
        next_value = unused_marker;
    else if (next_value > (int16_t)max_value)
        next_value = (int16_t)max_value;

    if (next_value == current_value)
        return 0U;

    *value = (next_value == unused_marker) ? unused_value : (uint8_t)next_value;
    return 1U;
}

static int16_t AppUi_PresetEditFindNameCharsetIndex(char ch)
{
    for (uint8_t index = 0U; index < (sizeof(AppUi_PresetEditNameCharset) - 1U); ++index)
    {
        if (AppUi_PresetEditNameCharset[index] == ch)
            return (int16_t)index;
    }

    return 0;
}

static void AppUi_PresetEditLoadNameCells(const Preset_t *preset, char *name_cells)
{
    size_t name_length;

    memset(name_cells, ' ', PRESET_NAME_LENGTH);
    if (!preset)
        return;

    name_length = strnlen(preset->name, PRESET_NAME_LENGTH);
    memcpy(name_cells, preset->name, name_length);
}

static void AppUi_PresetEditStoreNameCells(Preset_t *preset, const char *name_cells)
{
    int16_t last_non_space_index;

    if (!preset)
        return;

    memset(preset->name, 0, sizeof(preset->name));

    /* Trim trailing padding back out so stored names stay compact and existing
     * callers that expect a normal C string continue to work. */
    for (last_non_space_index = (int16_t)PRESET_NAME_LENGTH - 1; last_non_space_index >= 0; --last_non_space_index)
    {
        if (name_cells[last_non_space_index] != ' ')
            break;
    }

    if (last_non_space_index < 0)
        return;

    memcpy(preset->name, name_cells, (size_t)last_non_space_index + 1U);
    preset->name[last_non_space_index + 1] = '\0';
}

static uint8_t AppUi_PresetEditAdjustNameCharacter(Preset_t *preset, int8_t delta)
{
    char name_cells[PRESET_NAME_LENGTH];
    uint8_t name_index;
    int16_t current_charset_index;
    int16_t next_charset_index;
    int16_t charset_length = (int16_t)(sizeof(AppUi_PresetEditNameCharset) - 1U);

    if (!preset || delta == 0 || !Display_PresetNameEditIsActive())
        return 0U;

    name_index = Display_PresetNameEditGetCursorIndex();
    if (name_index >= PRESET_NAME_LENGTH)
        return 0U;

    AppUi_PresetEditLoadNameCells(preset, name_cells);
    current_charset_index = AppUi_PresetEditFindNameCharsetIndex(name_cells[name_index]);
    next_charset_index = current_charset_index + (int16_t)delta;

    while (next_charset_index < 0)
        next_charset_index += charset_length;

    while (next_charset_index >= charset_length)
        next_charset_index -= charset_length;

    if (name_cells[name_index] == AppUi_PresetEditNameCharset[next_charset_index])
        return 0U;

    name_cells[name_index] = AppUi_PresetEditNameCharset[next_charset_index];
    AppUi_PresetEditStoreNameCells(preset, name_cells);
    return 1U;
}

static uint8_t AppUi_PresetEditAdjustProgramValue(Preset_t *preset, uint8_t slot, int8_t delta)
{
    const MidiDevice_t *device;
    uint8_t previous_program;
    uint8_t max_program;

    if (!preset || slot >= PRESET_DEVICE_SLOTS)
        return 0U;

    device = MidiDevices_Get(slot);
    max_program = device ? device->max_preset : 127U;

    previous_program = preset->prg[slot].program;

    if (!AppUi_PresetEditAdjustSentinelValue(&preset->prg[slot].program,
                                             PRESET_PROGRAM_NONE,
                                             0U,
                                             max_program,
                                             delta))
    {
        return 0U;
    }

    /* Preview program changes immediately so the user hears the new selection
     * while still inside the edit field. */
    if (preset->prg[slot].program != previous_program && device != NULL)
        Midi_SendDeviceProgramSlotTransition(slot, previous_program, preset->prg[slot].program);

    return 1U;
}

uint8_t AppUi_PresetEditApplyDelta(int8_t delta)
{
    Preset_t *preset;
    DisplayPresetEditField_t field;

    if (!Display_PresetEditIsActive() || delta == 0)
        return 0U;

    if (!AppUi_PresetEditCurrentPresetIsEditable())
    {
        AppUi_PresetEditExit();
        return 0U;
    }

    preset = AppUi_GetMutableEditableActivePreset();
    if (!preset)
        return 0U;

    if (Display_PresetNameEditIsActive())
        return AppUi_PresetEditAdjustNameCharacter(preset, delta);

    field = Display_PresetEditGetField();
    switch (field.type)
    {
    case DISPLAY_PRESET_EDIT_FIELD_NAME:
        return 0U;

    case DISPLAY_PRESET_EDIT_FIELD_PROGRAM:
        return AppUi_PresetEditAdjustProgramValue(preset, field.itemIndex, delta);

    case DISPLAY_PRESET_EDIT_FIELD_FUNCTION_BUTTON:
        return 0U;

    case DISPLAY_PRESET_EDIT_FIELD_CC_CHANNEL:
        if (field.itemIndex >= PRESET_CC_SLOT_COUNT)
            return 0U;
        return AppUi_PresetEditAdjustSentinelValue(&preset->cc[field.itemIndex].channel,
                                                   PRESET_CC_CHANNEL_UNUSED,
                                                   1U,
                                                   16U,
                                                   delta);

    case DISPLAY_PRESET_EDIT_FIELD_CC_NUMBER:
        if (field.itemIndex >= PRESET_CC_SLOT_COUNT)
            return 0U;
        return AppUi_PresetEditAdjustSentinelValue(&preset->cc[field.itemIndex].cc_number,
                                                   PRESET_CC_NUMBER_UNUSED,
                                                   0U,
                                                   127U,
                                                   delta);

    case DISPLAY_PRESET_EDIT_FIELD_CC_VALUE:
        if (field.itemIndex >= PRESET_CC_SLOT_COUNT)
            return 0U;
        return AppUi_PresetEditAdjustSentinelValue(&preset->cc[field.itemIndex].value,
                                                   PRESET_CC_VALUE_UNUSED,
                                                   0U,
                                                   127U,
                                                   delta);

    case DISPLAY_PRESET_EDIT_FIELD_INIT:
        return 0U;

    default:
        return 0U;
    }
}

uint8_t AppUi_PresetEditEnter(void)
{
    if (Display_MenuIsActive() || Display_PresetEditIsActive() || !AppUi_PresetEditCurrentPresetIsEditable())
        return 0U;

    Display_PresetEditEnter();
    AppUi_RequestPresetEditModeRefresh();
    return 1U;
}

void AppUi_PresetEditExit(void)
{
    if (!Display_PresetEditIsActive())
        return;

    AppUi_PresetEditStopLearningSession();

    Display_PresetEditExit();
    AppUi_RequestPresetEditModeRefresh();

    if (Presets_IsDirty() || RuntimeConfig_IsDirty())
        App_QueueSaveRequestEvent(APP_EVENT_SAVE_KIND_PRESETS);
}

uint8_t AppUi_PresetEditSendCurrentPreset(void)
{
    const Preset_t *preset;

    if (!Display_PresetEditIsActive())
        return 0U;

    if (!AppUi_PresetEditCurrentPresetIsEditable())
    {
        AppUi_PresetEditExit();
        return 1U;
    }

    preset = AppUi_GetEditableActivePreset();
    if (!preset)
        return 0U;

    Midi_LoadPreset(preset);
    return 1U;
}

uint8_t AppUi_PresetEditToggleLearningSession(void)
{
    DisplayPresetEditField_t field;

    if (!Display_PresetEditIsActive())
        return 0U;

    if (!AppUi_PresetEditCurrentPresetIsEditable())
    {
        AppUi_PresetEditExit();
        return 0U;
    }

    if (app_ui_preset_learn_state.active)
    {
        AppUi_PresetEditStopLearningSession();
        return 1U;
    }

    field = Display_PresetEditGetField();
    if (!AppUi_PresetEditFieldSupportsLearning(field))
        return 0U;

    app_ui_preset_learn_state.active = 1U;
    app_ui_preset_learn_state.field = field;
    app_ui_preset_learn_state.last_valid_cc_tick = HAL_GetTick();
    {
        MidiMonitorEntry_t latest_received_entry;
        uint32_t latest_received_revision = 0U;

        (void)MidiMonitor_TryGetLatestEntry(&latest_received_entry,
                                            &latest_received_revision);
        app_ui_preset_learn_state.last_seen_monitor_revision =
            latest_received_revision;
    }
    Display_ShowLearningPopup();
    return 1U;
}

void AppUi_PresetEditLearningService(void)
{
    Preset_t *preset;
    DisplayPresetEditField_t field;
    MidiMonitorEntry_t latest_entry;
    uint32_t latest_revision;
    uint8_t has_new_entry = 0U;

    if (!app_ui_preset_learn_state.active)
        return;

    if (!Display_PresetEditIsActive())
    {
        AppUi_PresetEditStopLearningSession();
        return;
    }

    preset = AppUi_GetMutableEditableActivePreset();
    if (!preset)
    {
        AppUi_PresetEditStopLearningSession();
        return;
    }

    field = app_ui_preset_learn_state.field;

    if (MidiMonitor_TryGetLatestEntry(&latest_entry, &latest_revision)
     && latest_revision != app_ui_preset_learn_state.last_seen_monitor_revision)
    {
        app_ui_preset_learn_state.last_seen_monitor_revision = latest_revision;
        has_new_entry = 1U;
    }

    switch (field.type)
    {
    case DISPLAY_PRESET_EDIT_FIELD_PROGRAM:
        if (field.itemIndex < PRESET_DEVICE_SLOTS)
        {
            if (has_new_entry && latest_entry.type == MIDI_MONITOR_MESSAGE_PROGRAM_CHANGE)
            {
                preset->prg[field.itemIndex].program = latest_entry.value1;
                AppUi_PresetEditMarkDirty();
                AppUi_RequestPresetEditFieldRefresh();
                AppUi_PresetEditStopLearningSession();
            }
        }
        else
        {
            AppUi_PresetEditStopLearningSession();
        }
        break;

    case DISPLAY_PRESET_EDIT_FIELD_CC_NUMBER:
        if (field.itemIndex < PRESET_CC_SLOT_COUNT)
        {
            if (has_new_entry && latest_entry.type == MIDI_MONITOR_MESSAGE_CONTROL_CHANGE)
            {
                preset->cc[field.itemIndex].cc_number = latest_entry.value1;
                AppUi_PresetEditMarkDirty();
                AppUi_RequestPresetEditFieldRefresh();
                AppUi_PresetEditStopLearningSession();
            }
        }
        else
        {
            AppUi_PresetEditStopLearningSession();
        }
        break;

    case DISPLAY_PRESET_EDIT_FIELD_CC_VALUE:
        if (field.itemIndex < PRESET_CC_SLOT_COUNT)
        {
            if (has_new_entry && latest_entry.type == MIDI_MONITOR_MESSAGE_CONTROL_CHANGE)
            {
                preset->cc[field.itemIndex].value = latest_entry.value2;
                app_ui_preset_learn_state.last_valid_cc_tick = HAL_GetTick();
                AppUi_PresetEditMarkDirty();
                AppUi_RequestPresetEditFieldRefresh();
            }

            if ((HAL_GetTick() - app_ui_preset_learn_state.last_valid_cc_tick) >= APP_UI_PRESET_LEARN_VALUE_TIMEOUT_MS)
                AppUi_PresetEditStopLearningSession();
        }
        else
        {
            AppUi_PresetEditStopLearningSession();
        }
        break;

    default:
        AppUi_PresetEditStopLearningSession();
        break;
    }
}

uint8_t AppUi_PresetEditResetCurrentPresetToDefaults(void)
{
    uint8_t preset_index;

    if (!Display_PresetEditIsActive())
        return 0U;

    if (!AppUi_PresetEditCurrentPresetIsEditable())
    {
        AppUi_PresetEditExit();
        return 0U;
    }

    if (!AppUi_GetEditableActivePresetIndex(&preset_index))
        return 0U;

    Presets_ResetPresetToDefaults(preset_index);

    if (preset_index >= PRESET_COUNT)
        RuntimeConfig_MarkDirty();
    else
        Presets_MarkDirty();

    return 1U;
}

uint8_t AppUi_PresetEditEnterFunctionButtonEditor(void)
{
    uint8_t preset_index;

    if (!Display_PresetEditIsActive() || !AppUi_PresetEditCurrentPresetIsEditable())
        return 0U;

    if (!AppUi_GetEditableActivePresetIndex(&preset_index))
        return 0U;

    Display_MenuEnterPresetFunctionButtonEditor(preset_index);
    return 1U;
}

void AppUi_PresetEditMarkDirty(void)
{
    const Preset_t *active_preset = AppUi_GetEditableActivePreset();

    if (!active_preset)
        return;

    if (Presets_IsGlobalBypassPreset(active_preset) || Presets_IsGlobalMutePreset(active_preset))
        RuntimeConfig_MarkDirty();
    else
        Presets_MarkDirty();
}

uint8_t AppUi_PresetEditBackOutOneLevel(void)
{
    if (!Display_PresetEditIsActive())
        return 0U;

    if (Display_PresetNameEditIsActive())
    {
        const Preset_t *active_preset = AppState_GetActivePreset();

        Display_PresetNameEditExit();
        if (active_preset)
            AppUi_RequestPresetEditFieldRefresh();
        return 1U;
    }

    AppUi_PresetEditExit();
    return 1U;
}

static uint8_t AppUi_RandomSaveCurrentPresetIsRandomOverlay(void)
{
    const Preset_t *active_preset = AppState_GetActivePreset();

    return Presets_IsRandomPreset(active_preset);
}

static uint8_t AppUi_RandomSavePresetHasAnyProgram(const Preset_t *preset)
{
    if (!preset)
        return 0U;

    for (uint8_t slot = 0U; slot < PRESET_DEVICE_SLOTS; ++slot)
    {
        if (preset->prg[slot].program != PRESET_PROGRAM_NONE)
            return 1U;
    }

    return 0U;
}

static uint8_t AppUi_RandomSavePresetHasAnyCc(const Preset_t *preset)
{
    if (!preset)
        return 0U;

    for (uint8_t slot = 0U; slot < PRESET_CC_SLOT_COUNT; ++slot)
    {
        if (preset->cc[slot].channel != PRESET_CC_CHANNEL_UNUSED
         || preset->cc[slot].cc_number != PRESET_CC_NUMBER_UNUSED
         || preset->cc[slot].value != PRESET_CC_VALUE_UNUSED)
        {
            return 1U;
        }
    }

    return 0U;
}

static uint8_t AppUi_RandomSavePresetHasAnyRelayState(const Preset_t *preset)
{
    if (!preset)
        return 0U;

    for (uint8_t relay = 0U; relay < PRESET_RELAY_COUNT; ++relay)
    {
        if (preset->relay[relay] != PRESET_RELAY_OPEN)
            return 1U;
    }

    return 0U;
}

static uint8_t AppUi_RandomSavePresetSlotLooksEmpty(const Preset_t *preset, uint8_t slot_index)
{
    char default_name[PRESET_NAME_LENGTH + 1U];

    if (!preset || slot_index >= PRESETS_PER_BANK)
        return 0U;

    (void)snprintf(default_name, sizeof(default_name), "Preset %u", (unsigned)(slot_index + 1U));

    if (strncmp(preset->name, default_name, PRESET_NAME_LENGTH) != 0)
        return 0U;

    if (AppUi_RandomSavePresetHasAnyProgram(preset))
        return 0U;

    if (AppUi_RandomSavePresetHasAnyCc(preset))
        return 0U;

    if (AppUi_RandomSavePresetHasAnyRelayState(preset))
        return 0U;

    return 1U;
}

static uint8_t AppUi_RandomSaveSlotRequiresOverwrite(uint8_t slot_index)
{
    uint8_t target_index;
    const Preset_t *target_preset;

    if (slot_index >= PRESETS_PER_BANK)
        return 1U;

    target_index = (uint8_t)(AppState_GetCurrentBank() * PRESETS_PER_BANK + slot_index);
    target_preset = Presets_Get(target_index);

    return AppUi_RandomSavePresetSlotLooksEmpty(target_preset, slot_index) ? 0U : 1U;
}

static uint8_t AppUi_RandomSaveCommitToSlot(uint8_t slot_index)
{
    Preset_t *target_preset;
    const Preset_t *source_preset = AppState_GetActivePreset();
    uint8_t target_index;

    if (!source_preset || !Presets_IsRandomPreset(source_preset) || slot_index >= PRESETS_PER_BANK)
        return 0U;

    target_index = (uint8_t)(AppState_GetCurrentBank() * PRESETS_PER_BANK + slot_index);
    target_preset = Presets_GetMutable(target_index);
    if (!target_preset)
        return 0U;

    /* Random mode currently mutates only program slots, so saving the random
     * result copies those values into the selected bank slot without wiping
     * the slot's name, CC rows, relay states, or function-button settings. */
    for (uint8_t program_slot = 0U; program_slot < PRESET_DEVICE_SLOTS; ++program_slot)
        target_preset->prg[program_slot] = source_preset->prg[program_slot];

    Presets_MarkDirty();
    App_QueueSaveRequestEvent(APP_EVENT_SAVE_KIND_PRESETS);

    return 1U;
}

static uint8_t AppUi_RandomSaveEnterNameEditForSlot(uint8_t slot_index)
{
    uint8_t target_index;

    if (slot_index >= PRESETS_PER_BANK)
        return 0U;

    target_index = (uint8_t)(AppState_GetCurrentBank() * PRESETS_PER_BANK + slot_index);

    /* After storing the random result, jump straight to that slot and place
     * the user in name-edit mode so naming is part of the same save flow. */
    App_ActivatePreset(target_index);

    if (!AppUi_PresetEditEnter())
        return 0U;

    Display_PresetNameEditEnter();
    AppUi_RequestPresetEditFieldRefresh();
    return 1U;
}

uint8_t AppUi_RandomSaveCanStart(void)
{
    if (Display_MenuIsActive() || Display_PresetEditIsActive())
        return 0U;

    if (app_ui_random_save.state != APP_UI_RANDOM_SAVE_STATE_IDLE)
        return 0U;

    return AppUi_RandomSaveCurrentPresetIsRandomOverlay();
}

uint8_t AppUi_RandomSaveIsInProgress(void)
{
    return (app_ui_random_save.state != APP_UI_RANDOM_SAVE_STATE_IDLE) ? 1U : 0U;
}

uint8_t AppUi_RandomSaveIsAwaitingOverwriteConfirm(void)
{
    return (app_ui_random_save.state == APP_UI_RANDOM_SAVE_STATE_CONFIRM_OVERWRITE) ? 1U : 0U;
}

uint8_t AppUi_RandomSaveStartSelection(void)
{
    if (!AppUi_RandomSaveCanStart())
        return 0U;

    app_ui_random_save.state = APP_UI_RANDOM_SAVE_STATE_SLOT_SELECT;
    app_ui_random_save.selected_slot = 0U;
    Display_ShowBackupPopupMessage(APP_UI_RANDOM_SAVE_POPUP_CHOOSE_SLOT);

    return 1U;
}

uint8_t AppUi_RandomSaveHandlePresetSlotPress(uint8_t slot_index)
{
    if (slot_index >= PRESETS_PER_BANK
     || app_ui_random_save.state != APP_UI_RANDOM_SAVE_STATE_SLOT_SELECT)
    {
        return 0U;
    }

    app_ui_random_save.selected_slot = slot_index;

    if (AppUi_RandomSaveSlotRequiresOverwrite(slot_index))
    {
        app_ui_random_save.state = APP_UI_RANDOM_SAVE_STATE_CONFIRM_OVERWRITE;
        Display_ShowBackupPopupMessage(APP_UI_RANDOM_SAVE_POPUP_CONFIRM_OVERWRITE);
        return 1U;
    }

    app_ui_random_save.state = APP_UI_RANDOM_SAVE_STATE_IDLE;
    Display_HideBackupPopup(AppUi_GetCurrentDisplayPreset());
    if (AppUi_RandomSaveCommitToSlot(slot_index))
    {
        if (!AppUi_RandomSaveEnterNameEditForSlot(slot_index))
            AppUi_RequestActiveDisplayRefresh();
    }

    return 1U;
}

uint8_t AppUi_RandomSaveConfirmOverwrite(void)
{
    if (app_ui_random_save.state != APP_UI_RANDOM_SAVE_STATE_CONFIRM_OVERWRITE)
        return 0U;

    app_ui_random_save.state = APP_UI_RANDOM_SAVE_STATE_IDLE;
    Display_HideBackupPopup(AppUi_GetCurrentDisplayPreset());
    if (AppUi_RandomSaveCommitToSlot(app_ui_random_save.selected_slot))
    {
        if (!AppUi_RandomSaveEnterNameEditForSlot(app_ui_random_save.selected_slot))
            AppUi_RequestActiveDisplayRefresh();
    }

    return 1U;
}

uint8_t AppUi_RandomSaveCancel(void)
{
    if (app_ui_random_save.state == APP_UI_RANDOM_SAVE_STATE_IDLE)
        return 0U;

    app_ui_random_save.state = APP_UI_RANDOM_SAVE_STATE_IDLE;
    Display_HideBackupPopup(AppUi_GetCurrentDisplayPreset());
    AppUi_RequestActiveDisplayRefresh();
    return 1U;
}
