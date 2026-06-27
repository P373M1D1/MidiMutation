#include "display_functions.h"
#include "display/display_internal.h"
#include "display/display_palette_registry.h"
#include "display/display_menu_page_device_edit.h"
#include "display/display_menu_page_function_button_compare.h"
#include "display/display_value_helpers.h"
#include "runtime_config.h"

/* Value-edit engine for menu pages.
 *
 * This file owns numeric/enum adjustment rules and any immediate side effects
 * caused by a change (mark dirty, apply brightness immediately, force a full
 * redraw after theme changes). Rendering remains in the page/row modules. */

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

static uint8_t Display_AdjustClampedU16(uint16_t *value, uint16_t min_value, uint16_t max_value, int8_t delta)
{
    int32_t next_value;

    if (!value || min_value > max_value || delta == 0)
        return 0U;

    next_value = (int32_t)(*value) + (int32_t)delta;

    if (next_value < (int32_t)min_value)
        next_value = (int32_t)min_value;
    else if (next_value > (int32_t)max_value)
        next_value = (int32_t)max_value;

    if ((uint16_t)next_value == *value)
        return 0U;

    *value = (uint16_t)next_value;
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

static uint8_t Display_AdjustExpressionRawBound(RuntimeConfigGlobal_t *global,
                                                uint8_t adjust_heel,
                                                int8_t delta)
{
    uint8_t adjust_min;

    if (!global)
        return 0U;

    adjust_min = adjust_heel
        ? (global->expression_pedal_invert ? 0U : 1U)
        : (global->expression_pedal_invert ? 1U : 0U);

    if (adjust_min)
    {
        uint16_t max_value = (global->expression_pedal_max_raw > RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MIN)
            ? (uint16_t)(global->expression_pedal_max_raw - 1U)
            : RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MIN;

        return Display_AdjustClampedU16(&global->expression_pedal_min_raw,
                                        RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MIN,
                                        max_value,
                                        delta);
    }

    {
        uint16_t min_value = (global->expression_pedal_min_raw < RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MAX)
            ? (uint16_t)(global->expression_pedal_min_raw + 1U)
            : RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MAX;

        return Display_AdjustClampedU16(&global->expression_pedal_max_raw,
                                        min_value,
                                        RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MAX,
                                        delta);
    }
}

static RuntimeConfigMetronomeRhythm_t Display_StepMetronomeRhythm(RuntimeConfigMetronomeRhythm_t rhythm,
                                                                  int8_t delta)
{
    static const RuntimeConfigMetronomeRhythm_t rhythm_selection_order[] = {
        RUNTIME_CONFIG_METRONOME_RHYTHM_QUARTER_NOTES,
        RUNTIME_CONFIG_METRONOME_RHYTHM_FOUR_EIGHT,
        RUNTIME_CONFIG_METRONOME_RHYTHM_OFFBEAT,
        RUNTIME_CONFIG_METRONOME_RHYTHM_TRIPLETS,
        RUNTIME_CONFIG_METRONOME_RHYTHM_SHUFFLE,
    };
    uint8_t selection_index = 0U;
    uint8_t selection_count = (uint8_t)(sizeof(rhythm_selection_order) / sizeof(rhythm_selection_order[0]));
    uint8_t remaining_steps;

    while (selection_index + 1U < selection_count && rhythm_selection_order[selection_index] != rhythm)
        selection_index++;

    if (delta == 0)
        return rhythm_selection_order[selection_index];

    remaining_steps = (delta > 0) ? (uint8_t)delta : (uint8_t)(-delta);
    if (delta > 0)
    {
        uint8_t max_forward_steps = (uint8_t)(selection_count - 1U - selection_index);
        selection_index = (remaining_steps > max_forward_steps)
            ? (uint8_t)(selection_count - 1U)
            : (uint8_t)(selection_index + remaining_steps);
    }
    else
    {
        selection_index = (remaining_steps > selection_index)
            ? 0U
            : (uint8_t)(selection_index - remaining_steps);
    }

    return rhythm_selection_order[selection_index];
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
    RuntimeConfigFunctionButton_t *function_button = Presets_GetMutableFunctionButton(display_state.menu_active_preset_index);
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

static uint8_t Display_MenuApplyUserThemePaletteIndex(uint16_t next_palette_index)
{
    RuntimeConfigGlobal_t *global = RuntimeConfig_GetMutableGlobal();
    RuntimeConfigDisplayMode_t user_theme_mode = RuntimeConfig_NormalizeDisplayMode(display_state.menu_active_user_theme_mode);
    RuntimeConfigUserTheme_t *user_theme = RuntimeConfig_GetMutableUserTheme(user_theme_mode);
    RuntimeConfigUserThemeField_t field = (RuntimeConfigUserThemeField_t)display_state.menu_user_theme_selection_index;
    uint8_t changed;

    if (!display_state.menu_mode_active || (DisplayMenuPage_t)display_state.menu_page != DISPLAY_MENU_PAGE_USER_THEME)
        return 0U;

    if (!user_theme)
        return 0U;

    changed = RuntimeConfig_SetUserThemeColour(user_theme,
                                               field,
                                               DisplayPalette_GetValue(next_palette_index));
    if (!changed)
        return 0U;

    RuntimeConfig_MarkDirty();

    if (global && global->display_mode == user_theme_mode)
    {
        /* USER-theme colour edits happen inside menu mode, so repainting the
         * current menu shell is sufficient; a full display reset is overkill. */
        display_state.menu_draw_state_valid = 0U;
        Display_MenuRefresh();
        return 1U;
    }

    Display_MenuRedrawCurrentValue();
    return 1U;
}

uint8_t Display_MenuAdjustUserThemeHue(int8_t delta)
{
    RuntimeConfigDisplayMode_t user_theme_mode = RuntimeConfig_NormalizeDisplayMode(display_state.menu_active_user_theme_mode);
    const RuntimeConfigUserTheme_t *user_theme = RuntimeConfig_GetUserTheme(user_theme_mode);
    RuntimeConfigUserThemeField_t field = (RuntimeConfigUserThemeField_t)display_state.menu_user_theme_selection_index;
    uint16_t current_palette_index;

    if (!display_state.menu_user_theme_edit_active || delta == 0 || !user_theme)
        return 0U;

    current_palette_index = DisplayPalette_FindIndexByValue(RuntimeConfig_GetUserThemeColour(user_theme, field));
    return Display_MenuApplyUserThemePaletteIndex(DisplayPalette_StepHueIndex(current_palette_index, delta));
}

uint8_t Display_MenuAdjustUserThemeBrightness(int8_t delta)
{
    RuntimeConfigDisplayMode_t user_theme_mode = RuntimeConfig_NormalizeDisplayMode(display_state.menu_active_user_theme_mode);
    const RuntimeConfigUserTheme_t *user_theme = RuntimeConfig_GetUserTheme(user_theme_mode);
    RuntimeConfigUserThemeField_t field = (RuntimeConfigUserThemeField_t)display_state.menu_user_theme_selection_index;
    uint16_t current_palette_index;

    if (!display_state.menu_user_theme_edit_active || delta == 0 || !user_theme)
        return 0U;

    current_palette_index = DisplayPalette_FindIndexByValue(RuntimeConfig_GetUserThemeColour(user_theme, field));
    return Display_MenuApplyUserThemePaletteIndex(DisplayPalette_StepBrightnessIndex(current_palette_index, delta));
}

uint8_t Display_MenuAdjustValue(int8_t delta)
{
    RuntimeConfigGlobal_t *global = RuntimeConfig_GetMutableGlobal();
    RuntimeConfigMetronome_t *metronome = RuntimeConfig_GetMutableMetronome();
    RuntimeConfigBank_t *bank = RuntimeConfig_GetMutableBank(display_state.menu_active_bank_index);
    RuntimeConfigDevice_t *device = RuntimeConfig_GetMutableDevice(display_state.menu_active_device_index);
    uint8_t changed = 0U;
    uint8_t full_redraw = 0U;
    uint8_t metronome_dirty = 0U;

    if (!display_state.menu_mode_active || delta == 0)
        return 0U;

    if ((DisplayMenuTextField_t)display_state.menu_text_edit_field != DISPLAY_MENU_TEXT_FIELD_NONE)
    {
        /* Text editing bypasses the normal numeric/enum page logic and mutates
         * the currently selected character cell directly. */
        changed = Display_MenuAdjustTextCharacter(delta);
        if (!changed)
            return 0U;

        if ((DisplayMenuTextField_t)display_state.menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_NAME
         || (DisplayMenuTextField_t)display_state.menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_ACTIVE_LABEL
         || (DisplayMenuTextField_t)display_state.menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_INACTIVE_LABEL)
        {
            Presets_MarkDirty();
        }
        else
        {
            RuntimeConfig_MarkDirty();
        }
        Display_MenuRedrawCurrentValue();
        return 1U;
    }

    if (Display_MenuFunctionButtonMessagePageIsActive())
    {
        /* The compare table has its own sub-field cursoring, so message edits
         * are handled before the generic page switch below. */
        changed = Display_AdjustFunctionButtonMessageValue(delta);
        if (!changed)
            return 0U;

        Presets_MarkDirty();
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
        if (!device)
            return 0U;

        if (Display_MenuDeviceAutoCcRowIsSelected())
        {
            PresetCCSlot_t *auto_cc = Display_GetSelectedDeviceAutoCc(device);

            if (!auto_cc)
                return 0U;

            switch (display_state.menu_device_cc_field_index)
            {
            case 0U:
                changed = Display_AdjustWrappedOptionalU8(&auto_cc->channel,
                                                          PRESET_CC_CHANNEL_UNUSED,
                                                          1U,
                                                          16U,
                                                          delta);
                break;
            case 1U:
                changed = Display_AdjustWrappedOptionalU8(&auto_cc->cc_number,
                                                          PRESET_CC_NUMBER_UNUSED,
                                                          0U,
                                                          127U,
                                                          delta);
                break;
            case 2U:
                changed = Display_AdjustWrappedOptionalU8(&auto_cc->value,
                                                          PRESET_CC_VALUE_UNUSED,
                                                          0U,
                                                          127U,
                                                          delta);
                break;
            default:
                break;
            }
        }
        else
        {
            MidiCC_t *device_cc = Display_GetSelectedDeviceCc(device);

            if (Display_MenuDeviceCcRowIsSelected() && device_cc)
            {
                /* DEVICE_EDIT CC rows expose multiple editable sub-fields on
                 * one row, unlike the single-value rows elsewhere. */
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
                case MENU_DEVICE_EDIT_ITEM_MAX_PRESET:
                    changed = Display_AdjustWrappedU8(&device->max_preset, 1U, 127U, delta);
                    break;
                case MENU_DEVICE_EDIT_ITEM_RANDOM_BYPASS:
                    changed = Display_AdjustClampedU8(&device->random_bypass_percent,
                                                      0U,
                                                      RUNTIME_CONFIG_RANDOM_BYPASS_PERCENT_MAX,
                                                      delta);
                    break;
                case MENU_DEVICE_EDIT_ITEM_CHANNEL:
                    changed = Display_AdjustWrappedU8(&device->channel, 1U, 16U, delta);
                    break;
                default:
                    break;
                }
            }
        }
    }
    else if ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_EXPRESSION)
    {
        if (!global)
            return 0U;

        if (display_state.menu_expression_selection_index == MENU_EXPRESSION_ITEM_MODE)
        {
            uint8_t expression_mode = (uint8_t)global->expression_pedal_mode;

            changed = Display_AdjustWrappedU8(&expression_mode,
                                              (uint8_t)RUNTIME_CONFIG_EXPRESSION_PEDAL_MODE_DISABLED,
                                              (uint8_t)RUNTIME_CONFIG_EXPRESSION_PEDAL_MODE_TIMEBEND,
                                              delta);
            if (changed)
                global->expression_pedal_mode = (RuntimeConfigExpressionPedalMode_t)expression_mode;
        }
        else if (display_state.menu_expression_selection_index == MENU_EXPRESSION_ITEM_SET_HEEL)
        {
            changed = Display_AdjustExpressionRawBound(global, 1U, delta);
        }
        else if (display_state.menu_expression_selection_index == MENU_EXPRESSION_ITEM_SET_TOE)
        {
            changed = Display_AdjustExpressionRawBound(global, 0U, delta);
        }
        else if (display_state.menu_expression_selection_index == MENU_EXPRESSION_ITEM_INVERT)
        {
            changed = Display_AdjustWrappedU8(&global->expression_pedal_invert, 0U, 1U, delta);
        }
        else if (display_state.menu_expression_selection_index >= MENU_EXPRESSION_ITEM_CC_FIRST
              && display_state.menu_expression_selection_index < MENU_EXPRESSION_ITEM_COUNT)
        {
            RuntimeConfigExpressionPedalCcSlot_t *slot = &global->expression_pedal_cc_slots[
                display_state.menu_expression_selection_index - MENU_EXPRESSION_ITEM_CC_FIRST];

            switch (display_state.menu_expression_field_index)
            {
            case 0U:
                changed = Display_AdjustWrappedOptionalU8(&slot->cc,
                                                          PRESET_CC_NUMBER_UNUSED,
                                                          0U,
                                                          127U,
                                                          delta);
                break;
            case 1U:
                changed = Display_AdjustWrappedU8(&slot->heel_value, 0U, 127U, delta);
                break;
            case 2U:
                changed = Display_AdjustWrappedU8(&slot->toe_value, 0U, 127U, delta);
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
        case MENU_GLOBAL_ITEM_STARTUP_DELAY:
            changed = Display_AdjustClampedU8(&global->startup_delay_seconds,
                                              RUNTIME_CONFIG_GLOBAL_STARTUP_DELAY_MIN,
                                              RUNTIME_CONFIG_GLOBAL_STARTUP_DELAY_MAX,
                                              delta);
            break;

        case MENU_GLOBAL_ITEM_SYNC_STYLE:
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

        case MENU_GLOBAL_ITEM_CLOCK_MODE:
        {
            uint8_t clock_mode = (uint8_t)global->clock_mode;

            changed = Display_AdjustWrappedU8(&clock_mode,
                                              (uint8_t)RUNTIME_CONFIG_CLOCK_MODE_MONITOR,
                                              (uint8_t)RUNTIME_CONFIG_CLOCK_MODE_MASTER,
                                              delta);
            if (changed)
                global->clock_mode = (RuntimeConfigClockMode_t)clock_mode;
            break;
        }

        case MENU_GLOBAL_ITEM_LIVE_ENC2:
        {
            uint8_t live_enc2_mode = (uint8_t)global->live_enc2_mode;

            changed = Display_AdjustWrappedU8(&live_enc2_mode,
                                              (uint8_t)RUNTIME_CONFIG_LIVE_ENC2_MODE_PRESET_BANK_SCROLL,
                                              (uint8_t)RUNTIME_CONFIG_LIVE_ENC2_MODE_TIMEBEND,
                                              delta);
            if (changed)
                global->live_enc2_mode = (RuntimeConfigLiveEnc2Mode_t)live_enc2_mode;
            break;
        }

        case MENU_GLOBAL_ITEM_THEME:
        {
            RuntimeConfigDisplayMode_t next_display_mode = RuntimeConfig_StepDisplayMode(global->display_mode,
                                                                                         delta);

            changed = next_display_mode != global->display_mode;
            if (changed)
            {
                global->display_mode = next_display_mode;
                full_redraw = 1U;
            }
            break;
        }

        case MENU_GLOBAL_ITEM_BRIGHTNESS:
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

        case MENU_GLOBAL_ITEM_FEEDBACK_TAPER:
            changed = Display_AdjustWrappedU8(&global->feedback_taper_enabled, 0U, 1U, delta);
            break;

        case MENU_GLOBAL_ITEM_THRESHOLD:
            changed = Display_AdjustClampedU8(&global->feedback_taper_threshold,
                                              0U,
                                              RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_THRESHOLD_MAX,
                                              delta);
            break;

        case MENU_GLOBAL_ITEM_REDUCE:
            changed = Display_AdjustClampedU8(&global->feedback_taper_reduce,
                                              0U,
                                              RUNTIME_CONFIG_GLOBAL_FEEDBACK_TAPER_REDUCE_MAX,
                                              delta);
            break;

        default:
            break;
        }
    }
    else if ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_METRONOME)
    {
        if (!metronome)
            return 0U;

        metronome_dirty = 1U;

        switch (display_state.menu_metronome_selection_index)
        {
        case 0U:
            changed = Display_AdjustClampedU8(&metronome->volume,
                                              0U,
                                              RUNTIME_CONFIG_METRONOME_VOLUME_MAX,
                                              delta);
            break;

        case 1U:
        {
            uint8_t pitch = (uint8_t)metronome->pitch;

            changed = Display_AdjustClampedU8(&pitch,
                                              (uint8_t)RUNTIME_CONFIG_METRONOME_PITCH_LOW,
                                              (uint8_t)RUNTIME_CONFIG_METRONOME_PITCH_HIGH,
                                              delta);
            if (changed)
                metronome->pitch = (RuntimeConfigMetronomePitch_t)pitch;
            break;
        }

        case 2U:
            changed = Display_AdjustClampedU8(&metronome->beats_per_bar,
                                              RUNTIME_CONFIG_METRONOME_BEATS_PER_BAR_MIN,
                                              RUNTIME_CONFIG_METRONOME_BEATS_PER_BAR_MAX,
                                              delta);
            break;

        case 3U:
        {
            RuntimeConfigMetronomeRhythm_t next_rhythm = Display_StepMetronomeRhythm(metronome->rhythm,
                                                                                      delta);

            changed = next_rhythm != metronome->rhythm;
            if (changed)
                metronome->rhythm = next_rhythm;
            break;
        }

        default:
            break;
        }
    }
    else if ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_USER_THEME)
    {
        RuntimeConfigDisplayMode_t user_theme_mode = RuntimeConfig_NormalizeDisplayMode(display_state.menu_active_user_theme_mode);
        const RuntimeConfigUserTheme_t *user_theme = RuntimeConfig_GetUserTheme(user_theme_mode);
        RuntimeConfigUserThemeField_t field = (RuntimeConfigUserThemeField_t)display_state.menu_user_theme_selection_index;
        uint16_t current_palette_index;
        uint16_t next_palette_index;

        if (!user_theme)
            return 0U;

        current_palette_index = DisplayPalette_FindIndexByValue(RuntimeConfig_GetUserThemeColour(user_theme, field));
        next_palette_index = DisplayPalette_StepIndex(current_palette_index, delta);
        return Display_MenuApplyUserThemePaletteIndex(next_palette_index);
    }
    else
        return 0U;

    if (!changed)
        return 0U;

    if (metronome_dirty)
        RuntimeConfig_MarkMetronomeDirty();
    else
        RuntimeConfig_MarkDirty();

    if (full_redraw)
    {
        /* Theme changes invalidate fonts, colours, and cached row state all at
         * once, so the safe path is to rebuild the entire display surface. */
        Display_ForceFullDisplayRedraw();
        return 1U;
    }

    Display_MenuRedrawCurrentValue();
    return 1U;
}
