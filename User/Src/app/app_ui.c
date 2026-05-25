#include "app/app_ui.h"

#include "app_event.h"
#include "app/app_board_init.h"
#include "app/app_requests.h"
#include "app/app_state.h"
#include "display_functions.h"
#include "midi_devices.h"
#include "midi_functions.h"
#include "runtime_config.h"
#include <string.h>

static const char AppUi_PresetEditNameCharset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz ";

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

static const Preset_t *AppUi_GetEditableActivePreset(void)
{
    const Preset_t *active_preset = AppState_GetActivePreset();

    if (!active_preset)
        return NULL;

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

    if (preset->prg[slot].program != previous_program && device != NULL)
        Midi_SendDeviceProgramSlot(slot, preset->prg[slot].program);

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

    case DISPLAY_PRESET_EDIT_FIELD_RELAY:
        if (field.itemIndex >= PRESET_RELAY_COUNT)
            return 0U;

        {
            uint8_t next_state = (delta > 0) ? PRESET_RELAY_CLOSED : PRESET_RELAY_OPEN;

            if (preset->relay[field.itemIndex] == next_state)
                return 0U;

            preset->relay[field.itemIndex] = next_state;
            AppBoard_SetRelayState(field.itemIndex, next_state);
            return 1U;
        }

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

    App_QueueScreensaverWakeEvent();
    Display_PresetEditEnter();
    AppUi_RequestPresetEditModeRefresh();
    return 1U;
}

void AppUi_PresetEditExit(void)
{
    if (!Display_PresetEditIsActive())
        return;

    Display_PresetEditExit();
    App_QueueScreensaverActivityEvent();
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
        App_QueueScreensaverActivityEvent();
        if (active_preset)
            AppUi_RequestPresetEditFieldRefresh();
        return 1U;
    }

    AppUi_PresetEditExit();
    return 1U;
}