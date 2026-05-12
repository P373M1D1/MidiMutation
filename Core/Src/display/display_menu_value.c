#include "display_functions.h"
#include "display/display_internal.h"
#include "display/display_menu_page_device_edit.h"
#include "display/display_menu_page_function_button_compare.h"
#include "display/display_value_helpers.h"
#include "runtime_config.h"

static uint8_t Display_AdjustWrappedU8(uint8_t *value, uint8_t min_value, uint8_t max_value, int8_t delta)
{
    int32_t next_value;
    int32_t span;

    if (!value || min_value > max_value || delta == 0)
        return 0U;

    span = (int32_t)max_value - (int32_t)min_value + 1L;
    next_value = (int32_t)(*value) + (int32_t)delta;

    while (next_value < (int32_t)min_value)
        next_value += span;

    while (next_value > (int32_t)max_value)
        next_value -= span;

    if ((uint8_t)next_value == *value)
        return 0U;

    *value = (uint8_t)next_value;
    return 1U;
}

static uint8_t Display_AdjustClampedU8(uint8_t *value, uint8_t min_value, uint8_t max_value, int8_t delta)
{
    int32_t next_value;

    if (!value || min_value > max_value || delta == 0)
        return 0U;

    next_value = (int32_t)(*value) + (int32_t)delta;

    if (next_value < (int32_t)min_value)
        next_value = (int32_t)min_value;
    else if (next_value > (int32_t)max_value)
        next_value = (int32_t)max_value;

    if ((uint8_t)next_value == *value)
        return 0U;

    *value = (uint8_t)next_value;
    return 1U;
}

static uint8_t Display_AdjustDirectionalU8(uint8_t *value,
                                           uint8_t negative_value,
                                           uint8_t positive_value,
                                           int8_t delta)
{
    uint8_t next_value;

    if (!value || delta == 0)
        return 0U;

    next_value = (delta > 0) ? positive_value : negative_value;
    if (*value == next_value)
        return 0U;

    *value = next_value;
    return 1U;
}

static uint8_t Display_AdjustWrappedOptionalU8(uint8_t *value,
                                               uint8_t unused_value,
                                               uint8_t min_value,
                                               uint8_t max_value,
                                               int8_t delta)
{
    int32_t current_value;
    int32_t next_value;
    int32_t unused_marker;
    int32_t span;

    if (!value || min_value > max_value || delta == 0)
        return 0U;

    unused_marker = (int32_t)min_value - 1L;
    span = (int32_t)max_value - (int32_t)min_value + 2L;
    current_value = (*value == unused_value) ? unused_marker : (int32_t)(*value);
    next_value = current_value + (int32_t)delta;

    while (next_value < unused_marker)
        next_value += span;

    while (next_value > (int32_t)max_value)
        next_value -= span;

    if (next_value == current_value)
        return 0U;

    *value = (next_value == unused_marker) ? unused_value : (uint8_t)next_value;
    return 1U;
}

static uint8_t Display_MenuFunctionButtonMessagePageIsActive(void)
{
    return ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_FUNCTION_BUTTON
         && display_state.menu_function_button_selection_index >= MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX) ? 1U : 0U;
}

static uint8_t Display_AdjustFunctionButtonMessageValue(int8_t delta)
{
    RuntimeConfigFunctionButton_t *function_button = RuntimeConfig_GetMutableFunctionButton(display_state.menu_active_bank_index);
    uint8_t message_selection_index;

    if (!function_button || delta == 0 || !Display_MenuFunctionButtonMessagePageIsActive())
        return 0U;

    message_selection_index = Display_GetFunctionButtonMessageSelectionIndex();

    if (message_selection_index < RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT)
    {
        RuntimeConfigProgramMessage_t *active_program_message = &function_button->active_programs[message_selection_index];
        RuntimeConfigProgramMessage_t *inactive_program_message = &function_button->inactive_programs[message_selection_index];

        switch (display_state.menu_function_button_message_field_index)
        {
        case 0U:
            return Display_AdjustWrappedOptionalU8(&active_program_message->channel,
                                                   PRESET_CC_CHANNEL_UNUSED,
                                                   1U,
                                                   16U,
                                                   delta);
        case 1U:
            return Display_AdjustWrappedOptionalU8(&active_program_message->program,
                                                   PRESET_PROGRAM_NONE,
                                                   0U,
                                                   127U,
                                                   delta);
        case 2U:
            return Display_AdjustWrappedOptionalU8(&inactive_program_message->channel,
                                                   PRESET_CC_CHANNEL_UNUSED,
                                                   1U,
                                                   16U,
                                                   delta);
        case 3U:
            return Display_AdjustWrappedOptionalU8(&inactive_program_message->program,
                                                   PRESET_PROGRAM_NONE,
                                                   0U,
                                                   127U,
                                                   delta);
        default:
            return 0U;
        }
    }

    {
        uint8_t cc_index = (uint8_t)(message_selection_index - RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT);
        PresetCCSlot_t *active_cc_message = &function_button->active_cc[cc_index];
        PresetCCSlot_t *inactive_cc_message = &function_button->inactive_cc[cc_index];

        switch (display_state.menu_function_button_message_field_index)
        {
        case 0U:
            return Display_AdjustWrappedOptionalU8(&active_cc_message->channel,
                                                   PRESET_CC_CHANNEL_UNUSED,
                                                   1U,
                                                   16U,
                                                   delta);
        case 1U:
            return Display_AdjustWrappedOptionalU8(&active_cc_message->cc_number,
                                                   PRESET_CC_NUMBER_UNUSED,
                                                   0U,
                                                   127U,
                                                   delta);
        case 2U:
            return Display_AdjustWrappedOptionalU8(&active_cc_message->value,
                                                   PRESET_CC_VALUE_UNUSED,
                                                   0U,
                                                   127U,
                                                   delta);
        case 3U:
            return Display_AdjustWrappedOptionalU8(&inactive_cc_message->channel,
                                                   PRESET_CC_CHANNEL_UNUSED,
                                                   1U,
                                                   16U,
                                                   delta);
        case 4U:
            return Display_AdjustWrappedOptionalU8(&inactive_cc_message->cc_number,
                                                   PRESET_CC_NUMBER_UNUSED,
                                                   0U,
                                                   127U,
                                                   delta);
        case 5U:
            return Display_AdjustWrappedOptionalU8(&inactive_cc_message->value,
                                                   PRESET_CC_VALUE_UNUSED,
                                                   0U,
                                                   127U,
                                                   delta);
        default:
            return 0U;
        }
    }
}

uint8_t Display_MenuAdjustValue(int8_t delta)
{
    RuntimeConfigGlobal_t *global = RuntimeConfig_GetMutableGlobal();
    RuntimeConfigBank_t *bank = RuntimeConfig_GetMutableBank(display_state.menu_active_bank_index);
    RuntimeConfigDevice_t *device = RuntimeConfig_GetMutableDevice(display_state.menu_active_device_index);
    uint8_t changed = 0U;
    uint8_t full_redraw = 0U;

    if (!display_state.menu_mode_active || delta == 0)
        return 0U;

    if ((DisplayMenuTextField_t)display_state.menu_text_edit_field != DISPLAY_MENU_TEXT_FIELD_NONE)
    {
        changed = Display_MenuAdjustTextCharacter(delta);
        if (!changed)
            return 0U;

        RuntimeConfig_MarkDirty();
        Display_MenuRedrawCurrentValue();
        return 1U;
    }

    if (Display_MenuFunctionButtonMessagePageIsActive())
    {
        changed = Display_AdjustFunctionButtonMessageValue(delta);
        if (!changed)
            return 0U;

        RuntimeConfig_MarkDirty();
        Display_MenuRedrawCurrentItem();
        return 1U;
    }

    if ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_BANK_EDIT)
    {
        if (!bank)
            return 0U;

        switch (display_state.menu_bank_edit_selection_index)
        {
        case 1U:
            changed = Display_AdjustWrappedU8(&bank->wet_dry_enabled, 0U, 1U, delta);
            break;
        case 3U:
            changed = Display_AdjustClampedU8(&bank->midi_clock_bar_count,
                                              RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MIN,
                                              RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MAX,
                                              delta);
            break;
        default:
            break;
        }
    }
    else if ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_DEVICE_EDIT)
    {
        MidiCC_t *device_cc = Display_GetSelectedDeviceCc(device);

        if (!device)
            return 0U;

        if (Display_MenuDeviceCcRowIsSelected() && device_cc)
        {
            switch (display_state.menu_device_cc_field_index)
            {
            case 0U:
                changed = Display_AdjustWrappedOptionalU8(&device_cc->cc,
                                                          PRESET_CC_NUMBER_UNUSED,
                                                          0U,
                                                          127U,
                                                          delta);
                break;
            case 1U:
                changed = Display_AdjustWrappedU8(&device_cc->value, 0U, 127U, delta);
                break;
            default:
                break;
            }
        }
        else
        {
            switch (display_state.menu_device_edit_selection_index)
            {
            case 1U:
                changed = Display_AdjustWrappedU8(&device->max_preset, 1U, 127U, delta);
                break;
            case 2U:
                changed = Display_AdjustWrappedU8(&device->channel, 1U, 16U, delta);
                break;
            default:
                break;
            }
        }
    }
    else if ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_GLOBAL)
    {
        if (!global)
            return 0U;

        switch (display_state.menu_global_selection_index)
        {
        case 0U:
            changed = Display_AdjustClampedU8(&global->startup_delay_seconds,
                                              RUNTIME_CONFIG_GLOBAL_STARTUP_DELAY_MIN,
                                              RUNTIME_CONFIG_GLOBAL_STARTUP_DELAY_MAX,
                                              delta);
            break;

        case 1U:
            changed = Display_AdjustClampedU8(&global->screensaver_timeout_minutes,
                                              RUNTIME_CONFIG_GLOBAL_SCREENSAVER_MIN,
                                              RUNTIME_CONFIG_GLOBAL_SCREENSAVER_MAX,
                                              delta);
            break;

        case 2U:
        {
            uint8_t sync_style = (uint8_t)global->sync_style;

            changed = Display_AdjustDirectionalU8(&sync_style,
                                                  (uint8_t)RUNTIME_CONFIG_SYNC_STYLE_MIDI_CLOCK,
                                                  (uint8_t)RUNTIME_CONFIG_SYNC_STYLE_TAP_TEMPO_CC,
                                                  delta);
            if (changed)
                global->sync_style = (RuntimeConfigSyncStyle_t)sync_style;
            break;
        }

        case 3U:
        {
            uint8_t display_mode = (uint8_t)global->display_mode;

            changed = Display_AdjustDirectionalU8(&display_mode,
                                                  (uint8_t)RUNTIME_CONFIG_DISPLAY_MODE_DARK,
                                                  (uint8_t)RUNTIME_CONFIG_DISPLAY_MODE_BRIGHT,
                                                  delta);
            if (changed)
            {
                global->display_mode = (RuntimeConfigDisplayMode_t)display_mode;
                full_redraw = 1U;
                display_state.main_layout_dirty = 1U;
                display_state.bpm_display_valid = 0U;
            }
            break;
        }

        case 4U:
        {
            uint8_t brightness_ui = Display_GetGlobalBrightnessUiValue(global->backlight_brightness);

            changed = Display_AdjustClampedU8(&brightness_ui,
                                              0U,
                                              RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_UI_MAX,
                                              delta);
            if (changed)
            {
                global->backlight_brightness = Display_GetBrightnessFromUiValue(brightness_ui);
                Display_ApplyConfiguredBacklightBrightnessNow();
            }
            break;
        }

        default:
            break;
        }
    }
    else
        return 0U;

    if (!changed)
        return 0U;

    RuntimeConfig_MarkDirty();

    if (full_redraw)
    {
        Display_ForceFullDisplayRedraw();
        return 1U;
    }

    Display_MenuRedrawCurrentValue();
    return 1U;
}
