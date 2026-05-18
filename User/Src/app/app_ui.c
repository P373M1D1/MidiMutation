#include "app/app_ui.h"

#include "app_event.h"
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

const Preset_t *AppUi_GetCurrentDisplayPreset(void)
{
    return active_preset ? active_preset : Presets_Get(current_bank * PRESETS_PER_BANK);
}

uint8_t AppUi_PresetEditCurrentPresetIsEditable(void)
{
    const Preset_t *active_real_preset = Presets_Get(active_preset_index);

    return (active_preset != NULL && active_preset == active_real_preset) ? 1U : 0U;
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

    preset = Presets_GetMutable(active_preset_index);
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
            return 1U;
        }

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
    Display_RefreshPresetEditMode(active_preset, g_bpm);
    return 1U;
}

void AppUi_PresetEditExit(void)
{
    if (!Display_PresetEditIsActive())
        return;

    Display_PresetEditExit();
    App_QueueScreensaverActivityEvent();
    Display_RefreshPresetEditMode(AppUi_GetCurrentDisplayPreset(), g_bpm);

    if (Presets_IsDirty())
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

    preset = Presets_Get(active_preset_index);
    if (!preset)
        return 0U;

    Midi_LoadPreset(preset);
    return 1U;
}

uint8_t AppUi_PresetEditResetCurrentPresetToDefaults(void)
{
    if (!Display_PresetEditIsActive())
        return 0U;

    if (!AppUi_PresetEditCurrentPresetIsEditable())
    {
        AppUi_PresetEditExit();
        return 0U;
    }

    Presets_ResetPresetToDefaults(active_preset_index);
    Presets_MarkDirty();
    return 1U;
}

uint8_t AppUi_PresetEditBackOutOneLevel(void)
{
    if (!Display_PresetEditIsActive())
        return 0U;

    if (Display_PresetNameEditIsActive())
    {
        Display_PresetNameEditExit();
        App_QueueScreensaverActivityEvent();
        if (active_preset)
            Display_PresetEditRefreshCurrentField(active_preset);
        return 1U;
    }

    AppUi_PresetEditExit();
    return 1U;
}

void AppUi_ServiceMenuPreviewHold(uint8_t encoder2_switch_pressed)
{
    static uint8_t preview_visible = 0U;
    uint8_t should_preview = (Display_MenuPreviewCanShow() && encoder2_switch_pressed) ? 1U : 0U;

    if (should_preview == preview_visible)
        return;

    preview_visible = should_preview;

    if (should_preview)
    {
        Display_MenuPreviewEnter(AppUi_GetCurrentDisplayPreset(), g_bpm);
        return;
    }

    Display_MenuPreviewExit();
}

void AppUi_MenuSaveIfDirty(void)
{
    if (!RuntimeConfig_IsDirty())
        return;

    App_QueueSaveRequestEvent(APP_EVENT_SAVE_KIND_RUNTIME_CONFIG);
}

uint8_t AppUi_MenuBackOutOneLevel(void)
{
    uint8_t sub_editor_active;

    if (!Display_MenuIsActive())
        return 0U;

    sub_editor_active = Display_MenuSubEditorIsActive();
    Display_MenuBack();
    App_QueueScreensaverActivityEvent();
    if (!sub_editor_active)
        AppUi_MenuSaveIfDirty();

    if (!Display_MenuIsActive())
        App_QueueRedrawMainScreenEvent();

    return 1U;
}

uint8_t AppUi_MenuEnter(void)
{
    if (Display_MenuIsActive() || Display_PresetEditIsActive())
        return 0U;

    App_QueueScreensaverWakeEvent();
    Display_MenuEnter();
    return 1U;
}