#include <string.h>

#include "display_functions.h"
#include "display/display_internal.h"
#include "display/display_menu_page_device_edit.h"
#include "display/display_menu_page_midi_monitor.h"
#include "display/display_menu_pages.h"
#include "display/display_value_helpers.h"
#include "midi/midi_monitor.h"
#include "midi_devices.h"
#include "presets.h"
#include "runtime_config.h"

/* Menu controller and focus/navigation logic.
 *
 * This module decides which menu item, page, or sub-field is currently active
 * and how selection moves in response to encoder/button input. Rendering stays
 * in the row/page modules; this file should answer "where is focus now?" and
 * "what page should we enter/leave?" without composing pixels directly. */

/* ── Focus index helpers for FUNCTION_BUTTON and DEVICE_EDIT pages ─────────── */

static uint8_t Display_GetFunctionButtonMessageFieldCount(uint8_t row_index)
{
    return (row_index < RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT) ? 4U : 6U;
}

static uint8_t Display_GetFunctionButtonFocusFieldCount(uint8_t selection_index)
{
    if (selection_index < MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX)
        return 1U;

    return Display_GetFunctionButtonMessageFieldCount((uint8_t)(selection_index - MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX));
}

static uint16_t Display_GetFunctionButtonFocusCount(void)
{
    uint16_t focus_count = 0U;

    /* Flatten the visible FUNCTION_BUTTON page into a single cursor domain so
     * encoder navigation can move across both row boundaries and sub-fields. */
    for (uint8_t selection_index = 0U; selection_index < MENU_FUNCTION_BUTTON_ITEM_COUNT; ++selection_index)
        focus_count = (uint16_t)(focus_count + Display_GetFunctionButtonFocusFieldCount(selection_index));

    return focus_count;
}

static uint16_t Display_GetFunctionButtonFocusIndex(void)
{
    uint16_t focus_index = 0U;

    for (uint8_t selection_index = 0U; selection_index < display_state.menu_function_button_selection_index; ++selection_index)
        focus_index = (uint16_t)(focus_index + Display_GetFunctionButtonFocusFieldCount(selection_index));

    /* Inline equivalent of Display_MenuFunctionButtonMessagePageIsActive() */
    if ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_FUNCTION_BUTTON
     && display_state.menu_function_button_selection_index >= MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX)
    {
        uint8_t field_count = Display_GetFunctionButtonFocusFieldCount(display_state.menu_function_button_selection_index);

        if (display_state.menu_function_button_message_field_index >= field_count)
            display_state.menu_function_button_message_field_index = 0U;

        focus_index = (uint16_t)(focus_index + display_state.menu_function_button_message_field_index);
    }

    return focus_index;
}

static void Display_SetFunctionButtonFocusIndex(uint16_t focus_index)
{
    for (uint8_t selection_index = 0U; selection_index < MENU_FUNCTION_BUTTON_ITEM_COUNT; ++selection_index)
    {
        uint8_t field_count = Display_GetFunctionButtonFocusFieldCount(selection_index);

        if (focus_index < field_count)
        {
            /* Translate the flattened focus index back into the pair of values
             * the rest of the display code understands: row selection plus the
             * active field inside that row. */
            display_state.menu_function_button_selection_index = selection_index;
            display_state.menu_function_button_message_field_index = (selection_index < MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX)
                ? 0U
                : (uint8_t)focus_index;
            return;
        }

        focus_index = (uint16_t)(focus_index - field_count);
    }

    display_state.menu_function_button_selection_index = (uint8_t)(MENU_FUNCTION_BUTTON_ITEM_COUNT - 1U);
    display_state.menu_function_button_message_field_index = (uint8_t)(Display_GetFunctionButtonFocusFieldCount(display_state.menu_function_button_selection_index) - 1U);
}

static uint8_t Display_GetDeviceEditFocusFieldCount(uint8_t selection_index)
{
    if (selection_index >= 3U && selection_index <= 5U)
        return 2U;

    return 1U;
}

static uint16_t Display_GetDeviceEditFocusCount(void)
{
    uint16_t focus_count = 0U;

    for (uint8_t selection_index = 0U; selection_index < MENU_DEVICE_EDIT_ITEM_COUNT; ++selection_index)
        focus_count = (uint16_t)(focus_count + Display_GetDeviceEditFocusFieldCount(selection_index));

    return focus_count;
}

static uint16_t Display_GetDeviceEditFocusIndex(void)
{
    uint16_t focus_index = 0U;

    for (uint8_t selection_index = 0U; selection_index < display_state.menu_device_edit_selection_index; ++selection_index)
        focus_index = (uint16_t)(focus_index + Display_GetDeviceEditFocusFieldCount(selection_index));

    if ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_DEVICE_EDIT
     && display_state.menu_device_edit_selection_index >= 3U
        && display_state.menu_device_edit_selection_index <= 11U)
    {
        uint8_t field_count = Display_GetDeviceEditFocusFieldCount(display_state.menu_device_edit_selection_index);

        if (display_state.menu_device_cc_field_index >= field_count)
            display_state.menu_device_cc_field_index = 0U;

        focus_index = (uint16_t)(focus_index + display_state.menu_device_cc_field_index);
    }

    return focus_index;
}

static void Display_SetDeviceEditFocusIndex(uint16_t focus_index)
{
    for (uint8_t selection_index = 0U; selection_index < MENU_DEVICE_EDIT_ITEM_COUNT; ++selection_index)
    {
        uint8_t field_count = Display_GetDeviceEditFocusFieldCount(selection_index);

        if (focus_index < field_count)
        {
            display_state.menu_device_edit_selection_index = selection_index;
            display_state.menu_device_cc_field_index = (selection_index >= 3U && selection_index <= 11U)
                ? (uint8_t)focus_index
                : 0U;
            return;
        }

        focus_index = (uint16_t)(focus_index - field_count);
    }

    display_state.menu_device_edit_selection_index = (uint8_t)(MENU_DEVICE_EDIT_ITEM_COUNT - 1U);
    display_state.menu_device_cc_field_index = 0U;
}

static uint8_t Display_GetExpressionFocusFieldCount(uint8_t selection_index)
{
    return (selection_index == 0U) ? 1U : 3U;
}

static uint16_t Display_GetExpressionFocusCount(void)
{
    uint16_t focus_count = 0U;

    for (uint8_t selection_index = 0U; selection_index < MENU_EXPRESSION_ITEM_COUNT; ++selection_index)
        focus_count = (uint16_t)(focus_count + Display_GetExpressionFocusFieldCount(selection_index));

    return focus_count;
}

static uint16_t Display_GetExpressionFocusIndex(void)
{
    uint16_t focus_index = 0U;

    for (uint8_t selection_index = 0U; selection_index < display_state.menu_expression_selection_index; ++selection_index)
        focus_index = (uint16_t)(focus_index + Display_GetExpressionFocusFieldCount(selection_index));

    if ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_EXPRESSION
        && display_state.menu_expression_selection_index > 0U)
    {
        if (display_state.menu_expression_field_index >= 3U)
            display_state.menu_expression_field_index = 0U;

        focus_index = (uint16_t)(focus_index + display_state.menu_expression_field_index);
    }

    return focus_index;
}

static void Display_SetExpressionFocusIndex(uint16_t focus_index)
{
    for (uint8_t selection_index = 0U; selection_index < MENU_EXPRESSION_ITEM_COUNT; ++selection_index)
    {
        uint8_t field_count = Display_GetExpressionFocusFieldCount(selection_index);

        if (focus_index < field_count)
        {
            display_state.menu_expression_selection_index = selection_index;
            display_state.menu_expression_field_index = (selection_index == 0U)
                ? 0U
                : (uint8_t)focus_index;
            return;
        }

        focus_index = (uint16_t)(focus_index - field_count);
    }

    display_state.menu_expression_selection_index = (uint8_t)(MENU_EXPRESSION_ITEM_COUNT - 1U);
    display_state.menu_expression_field_index = 2U;
}

/* ── Existing transient-editor reset ──────────────────────────────────────── */

static void Display_ResetMenuTransientEditors(void)
{
    /* These editor cursors are page-local scratch state. Reset them on page
     * transitions so a new page never inherits stale text/CC sub-selection. */
    display_state.menu_function_button_message_selection_index = 0U;
    display_state.menu_function_button_message_field_index = 0U;
    display_state.menu_function_button_message_field_edit_active = 0U;
    display_state.menu_device_cc_field_index = 0U;
    display_state.menu_device_cc_field_edit_active = 0U;
    display_state.menu_device_cc_learn_armed = 0U;
    display_state.menu_expression_selection_index = 0U;
    display_state.menu_expression_field_index = 0U;
    display_state.menu_expression_learn_armed = 0U;
    display_state.menu_text_edit_field = (uint8_t)DISPLAY_MENU_TEXT_FIELD_NONE;
    display_state.menu_text_edit_cursor_index = 0U;
    display_state.menu_user_theme_edit_active = 0U;
}

static void Display_RestoreFactorySettings(void)
{
    /* Factory reset spans both persistent models: runtime config plus every
     * preset slot. Mark both dirty so the save service writes one consistent
     * combined image afterwards. */
    RuntimeConfig_ResetToDefaults();

    for (uint8_t preset_index = 0U; preset_index < PRESET_COUNT; ++preset_index)
        Presets_ResetPresetToDefaults(preset_index);

    RuntimeConfig_MarkDirty();
    Presets_MarkDirty();
}

static void Display_ResetActiveDeviceToUnusedDefaults(RuntimeConfigDevice_t *device)
{
    (void)device;
    RuntimeConfig_ResetDeviceToDefaults(display_state.menu_active_device_index);
}

void Display_MenuEnter(void)
{
    display_state.menu_mode_active = 1U;
    display_state.menu_preview_active = 0U;
    display_state.menu_draw_state_valid = 0U;
    display_state.menu_last_drawn_page = (uint8_t)DISPLAY_MENU_PAGE_ROOT;
    display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_ROOT;
    Display_ResetMenuTransientEditors();
    display_state.menu_root_selection_index = 0U;
    display_state.menu_bank_selection_index = current_bank;
    display_state.menu_active_bank_index = current_bank;
    display_state.menu_active_preset_index = 0U;
    display_state.menu_return_to_preset_edit = 0U;
    display_state.menu_bank_edit_selection_index = 0U;
    display_state.menu_function_button_selection_index = 0U;
    display_state.menu_device_selection_index = 0U;
    display_state.menu_active_device_index = 0U;
    display_state.menu_device_edit_selection_index = 0U;
    display_state.menu_global_selection_index = 0U;
    display_state.menu_expression_selection_index = 0U;
    display_state.menu_expression_field_index = 0U;
    display_state.menu_expression_learn_armed = 0U;
    display_state.menu_metronome_selection_index = 0U;
    display_state.menu_metronome_quick_access_live = 0U;
    display_state.menu_user_theme_selection_index = 0U;
    display_state.menu_active_user_theme_mode = (uint8_t)RUNTIME_CONFIG_DISPLAY_MODE_USER;
    display_state.main_layout_dirty = 1U;
    display_state.bpm_display_valid = 0U;
    display_state.transport_status_valid = 0U;
    Display_MenuRefresh();
}

uint8_t Display_MenuEnterMetronomeQuickAccess(void)
{
    if (display_state.preset_edit_mode_active)
        return 0U;

    if (!display_state.menu_mode_active)
    {
        display_state.menu_mode_active = 1U;
        display_state.menu_preview_active = 0U;
        display_state.menu_draw_state_valid = 0U;
        display_state.menu_last_drawn_page = (uint8_t)DISPLAY_MENU_PAGE_ROOT;
        display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_METRONOME;
        Display_ResetMenuTransientEditors();
        display_state.menu_root_selection_index = 3U;
        display_state.menu_bank_selection_index = current_bank;
        display_state.menu_active_bank_index = current_bank;
        display_state.menu_active_preset_index = 0U;
        display_state.menu_return_to_preset_edit = 0U;
        display_state.menu_bank_edit_selection_index = 0U;
        display_state.menu_function_button_selection_index = 0U;
        display_state.menu_device_selection_index = 0U;
        display_state.menu_active_device_index = 0U;
        display_state.menu_device_edit_selection_index = 0U;
        display_state.menu_global_selection_index = 0U;
        display_state.menu_metronome_selection_index = 0U;
        display_state.menu_metronome_quick_access_live = 1U;
        display_state.menu_user_theme_selection_index = 0U;
        display_state.menu_active_user_theme_mode = (uint8_t)RUNTIME_CONFIG_DISPLAY_MODE_USER;
        display_state.main_layout_dirty = 1U;
        display_state.bpm_display_valid = 0U;
        display_state.transport_status_valid = 0U;
        Display_MenuRefresh();
        return 1U;
    }

    display_state.menu_preview_active = 0U;
    display_state.menu_draw_state_valid = 0U;
    display_state.menu_root_selection_index = 3U;
    display_state.menu_metronome_selection_index = 0U;
    display_state.menu_metronome_quick_access_live = 0U;
    display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_METRONOME;
    Display_ResetMenuTransientEditors();
    Display_MenuRefresh();
    return 1U;
}

void Display_MenuEnterPresetFunctionButtonEditor(uint8_t preset_index)
{
    display_state.menu_mode_active = 1U;
    display_state.menu_preview_active = 0U;
    display_state.menu_draw_state_valid = 0U;
    display_state.menu_last_drawn_page = (uint8_t)DISPLAY_MENU_PAGE_ROOT;
    display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_FUNCTION_BUTTON;
    Display_ResetMenuTransientEditors();
    display_state.menu_bank_selection_index = current_bank;
    display_state.menu_active_bank_index = current_bank;
    display_state.menu_active_preset_index = preset_index;
    display_state.menu_return_to_preset_edit = 1U;
    display_state.menu_function_button_selection_index = 0U;
    display_state.main_layout_dirty = 1U;
    display_state.bpm_display_valid = 0U;
    display_state.transport_status_valid = 0U;
    Display_MenuRefresh();
}

void Display_MenuExit(void)
{
    Display_ResetMenuTransientEditors();
    display_state.menu_preview_active = 0U;
    display_state.menu_draw_state_valid = 0U;
    display_state.menu_last_drawn_page = (uint8_t)DISPLAY_MENU_PAGE_ROOT;
    display_state.menu_mode_active = 0U;
    display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_ROOT;
    display_state.menu_active_preset_index = 0U;
    display_state.menu_return_to_preset_edit = 0U;
    display_state.menu_metronome_quick_access_live = 0U;
    display_state.main_layout_dirty = 1U;
    display_state.bpm_display_valid = 0U;
    display_state.transport_status_valid = 0U;
}

void Display_MenuTextEditExit(void)
{
    if (display_state.menu_text_edit_field == (uint8_t)DISPLAY_MENU_TEXT_FIELD_NONE)
        return;

    display_state.menu_text_edit_field = (uint8_t)DISPLAY_MENU_TEXT_FIELD_NONE;
    display_state.menu_text_edit_cursor_index = 0U;

    if (display_state.menu_mode_active)
        Display_MenuRefresh();
}

void Display_MenuHome(void)
{
    if (!display_state.menu_mode_active)
        return;

    if (display_state.menu_return_to_preset_edit
     && ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_FUNCTION_BUTTON
      || (DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES
      || (DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES))
    {
        Display_MenuExit();
        return;
    }

    if (display_state.menu_page == (uint8_t)DISPLAY_MENU_PAGE_BANK_INIT_CONFIRM)
    {
        Display_ResetMenuTransientEditors();
        RuntimeConfig_ResetBankToDefaults(display_state.menu_active_bank_index);
        RuntimeConfig_MarkDirty();
        display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_BANK_EDIT;
        Display_MenuRefresh();
        return;
    }

    if (display_state.menu_page == (uint8_t)DISPLAY_MENU_PAGE_DEVICE_INIT_CONFIRM)
    {
        RuntimeConfigDevice_t *device = RuntimeConfig_GetMutableDevice(display_state.menu_active_device_index);

        Display_ResetMenuTransientEditors();
        if (device)
        {
            Display_ResetActiveDeviceToUnusedDefaults(device);
            RuntimeConfig_MarkDirty();
        }

        display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_DEVICE_EDIT;
        Display_MenuRefresh();
        return;
    }

    if (display_state.menu_page == (uint8_t)DISPLAY_MENU_PAGE_FACTORY_RESET_CONFIRM)
    {
        Display_ResetMenuTransientEditors();
        Display_RestoreFactorySettings();
        display_state.menu_global_selection_index = (uint8_t)(MENU_GLOBAL_ITEM_COUNT - 1U);
        display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_GLOBAL;
        Display_MenuRefresh();
        return;
    }

    Display_ResetMenuTransientEditors();
    switch ((DisplayMenuPage_t)display_state.menu_page)
    {
    case DISPLAY_MENU_PAGE_GLOBAL:
    case DISPLAY_MENU_PAGE_EXPRESSION:
    case DISPLAY_MENU_PAGE_USER_THEME:
        display_state.menu_root_selection_index = 2U;
        break;
    case DISPLAY_MENU_PAGE_METRONOME:
        display_state.menu_metronome_quick_access_live = 0U;
        display_state.menu_root_selection_index = 3U;
        break;
    case DISPLAY_MENU_PAGE_MIDI_MONITOR:
        display_state.menu_root_selection_index = 4U;
        break;
    case DISPLAY_MENU_PAGE_DEVICE_EDIT:
    case DISPLAY_MENU_PAGE_DEVICES:
        display_state.menu_root_selection_index = 1U;
        break;
    case DISPLAY_MENU_PAGE_BANK_EDIT:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES:
    case DISPLAY_MENU_PAGE_BANKS:
        display_state.menu_root_selection_index = 0U;
        break;
    default:
        break;
    }

    display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_ROOT;
    Display_MenuRefresh();
}

uint8_t Display_MenuPreviewCanShow(void)
{
    return (display_state.menu_mode_active
         && (DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_USER_THEME) ? 1U : 0U;
}

uint8_t Display_MenuPreviewIsActive(void)
{
    return display_state.menu_preview_active;
}

uint8_t Display_MenuUserThemeEditIsActive(void)
{
    return (display_state.menu_mode_active
         && (DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_USER_THEME
         && display_state.menu_user_theme_edit_active) ? 1U : 0U;
}

void Display_MenuPreviewEnter(const Preset_t *p, uint16_t bpm)
{
    if (!Display_MenuPreviewCanShow() || display_state.menu_preview_active || !p)
        return;

    display_state.menu_preview_active = 1U;
    display_state.main_layout_dirty = 1U;
    display_state.bpm_display_valid = 0U;
    display_state.transport_status_valid = 0U;
    Display_DrawMainScreen(p, bpm);
}

void Display_MenuPreviewExit(void)
{
    if (!display_state.menu_preview_active)
        return;

    display_state.menu_preview_active = 0U;
    display_state.menu_draw_state_valid = 0U;
    Display_MenuRefresh();
}

uint8_t Display_MenuBack(void)
{
    if (!display_state.menu_mode_active)
        return 0U;

    if ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_USER_THEME
     && display_state.menu_user_theme_edit_active)
    {
        display_state.menu_user_theme_edit_active = 0U;
        Display_DrawFootbar();
        return 1U;
    }

    switch ((DisplayMenuPage_t)display_state.menu_page)
    {
    case DISPLAY_MENU_PAGE_BANKS:
        display_state.menu_root_selection_index = 0U;
        break;
    case DISPLAY_MENU_PAGE_BANK_EDIT:
        display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_BANKS;
        Display_MenuRefresh();
        return 1U;
    case DISPLAY_MENU_PAGE_BANK_INIT_CONFIRM:
        display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_BANK_EDIT;
        Display_MenuRefresh();
        return 1U;
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON:
        if (display_state.menu_return_to_preset_edit)
            Display_MenuExit();
        else
        {
            display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_BANK_EDIT;
            Display_MenuRefresh();
        }
        return 1U;
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES:
        Display_ResetMenuTransientEditors();
        display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_FUNCTION_BUTTON;
        Display_MenuRefresh();
        return 1U;
    case DISPLAY_MENU_PAGE_DEVICE_EDIT:
        display_state.menu_device_cc_field_index = 0U;
        display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_DEVICES;
        Display_MenuRefresh();
        return 1U;
    case DISPLAY_MENU_PAGE_DEVICE_INIT_CONFIRM:
        display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_DEVICE_EDIT;
        Display_MenuRefresh();
        return 1U;
    case DISPLAY_MENU_PAGE_FACTORY_RESET_CONFIRM:
        display_state.menu_global_selection_index = (uint8_t)(MENU_GLOBAL_ITEM_COUNT - 1U);
        display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_GLOBAL;
        Display_MenuRefresh();
        return 1U;
    case DISPLAY_MENU_PAGE_METRONOME:
        if (display_state.menu_metronome_quick_access_live)
        {
            Display_MenuExit();
            return 1U;
        }

        display_state.menu_root_selection_index = 3U;
        display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_ROOT;
        Display_MenuRefresh();
        return 1U;
    case DISPLAY_MENU_PAGE_MIDI_MONITOR:
        display_state.menu_root_selection_index = 4U;
        display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_ROOT;
        Display_MenuRefresh();
        return 1U;
    case DISPLAY_MENU_PAGE_EXPRESSION:
        display_state.menu_global_selection_index = 9U;
        display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_GLOBAL;
        Display_MenuRefresh();
        return 1U;
    case DISPLAY_MENU_PAGE_USER_THEME:
        display_state.menu_global_selection_index = 4U;
        display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_GLOBAL;
        Display_MenuRefresh();
        return 1U;
    case DISPLAY_MENU_PAGE_DEVICES:
        display_state.menu_root_selection_index = 1U;
        break;
    case DISPLAY_MENU_PAGE_GLOBAL:
        display_state.menu_root_selection_index = 2U;
        break;
    case DISPLAY_MENU_PAGE_ROOT:
        Display_MenuExit();
        return 1U;
    default:
        break;
    }

    display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_ROOT;
    Display_MenuRefresh();
    return 1U;
}

/* ── Selection navigation and activation (moved from display_functions.c) ─── */

uint8_t Display_MenuIsActive(void)
{
    return display_state.menu_mode_active;
}

uint8_t Display_MenuConfirmActionIsActive(void)
{
    if (!display_state.menu_mode_active)
        return 0U;

    return Display_MenuPageUsesConfirmFootbar((DisplayMenuPage_t)display_state.menu_page);
}

uint8_t Display_MenuMoveSelection(int8_t delta)
{
    uint8_t *selection;
    uint8_t item_count;
    uint8_t previous_selection;
    int16_t next_selection;

    if (!display_state.menu_mode_active || delta == 0
     || display_state.menu_text_edit_field != (uint8_t)DISPLAY_MENU_TEXT_FIELD_NONE)
        return 0U;

    if ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_FUNCTION_BUTTON)
    {
        uint16_t current_focus = Display_GetFunctionButtonFocusIndex();
        uint16_t focus_count = Display_GetFunctionButtonFocusCount();
        int32_t next_focus = (int32_t)current_focus + (int32_t)delta;

        if (next_focus < 0)
            next_focus = 0;
        else if (next_focus >= (int32_t)focus_count)
            next_focus = (int32_t)focus_count - 1;

        if ((uint16_t)next_focus == current_focus)
            return 0U;

        previous_selection = display_state.menu_function_button_selection_index;
        Display_SetFunctionButtonFocusIndex((uint16_t)next_focus);

        if (display_state.menu_function_button_selection_index == previous_selection)
            Display_MenuRedrawCurrentItem();
        else
            Display_MenuRedrawSelectionChange((DisplayMenuPage_t)display_state.menu_page, previous_selection);

        return 1U;
    }

    if ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_DEVICE_EDIT)
    {
        uint16_t current_focus = Display_GetDeviceEditFocusIndex();
        uint16_t focus_count = Display_GetDeviceEditFocusCount();
        int32_t next_focus = (int32_t)current_focus + (int32_t)delta;

        if (next_focus < 0)
            next_focus = 0;
        else if (next_focus >= (int32_t)focus_count)
            next_focus = (int32_t)focus_count - 1;

        if ((uint16_t)next_focus == current_focus)
            return 0U;

        previous_selection = display_state.menu_device_edit_selection_index;
        Display_SetDeviceEditFocusIndex((uint16_t)next_focus);

        if (display_state.menu_device_edit_selection_index == previous_selection)
            Display_MenuRedrawCurrentItem();
        else
            Display_MenuRedrawSelectionChange((DisplayMenuPage_t)display_state.menu_page, previous_selection);

        return 1U;
    }

    if ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_EXPRESSION)
    {
        uint16_t current_focus = Display_GetExpressionFocusIndex();
        uint16_t focus_count = Display_GetExpressionFocusCount();
        int32_t next_focus = (int32_t)current_focus + (int32_t)delta;

        if (next_focus < 0)
            next_focus = 0;
        else if (next_focus >= (int32_t)focus_count)
            next_focus = (int32_t)focus_count - 1;

        if ((uint16_t)next_focus == current_focus)
            return 0U;

        previous_selection = display_state.menu_expression_selection_index;
        Display_SetExpressionFocusIndex((uint16_t)next_focus);

        if (display_state.menu_expression_selection_index == previous_selection)
            Display_MenuRedrawCurrentItem();
        else
            Display_MenuRedrawSelectionChange((DisplayMenuPage_t)display_state.menu_page, previous_selection);

        return 1U;
    }

    selection = Display_GetMenuPageSelectionPointer((DisplayMenuPage_t)display_state.menu_page);
    item_count = Display_GetMenuPageItemCount((DisplayMenuPage_t)display_state.menu_page);

    if (!selection || item_count == 0U)
        return 0U;

    next_selection = (int16_t)(*selection) + (int16_t)delta;
    if (next_selection < 0)
        next_selection = 0;
    else if (next_selection >= (int16_t)item_count)
        next_selection = (int16_t)item_count - 1;

    if ((uint8_t)next_selection == *selection)
        return 0U;

    previous_selection = *selection;
    *selection = (uint8_t)next_selection;

    Display_MenuRedrawSelectionChange((DisplayMenuPage_t)display_state.menu_page, previous_selection);
    return 1U;
}

uint8_t Display_MenuActivate(void)
{
    DisplayMenuTextField_t text_field;
    const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();

    if (!display_state.menu_mode_active)
        return 0U;

    if (display_state.menu_text_edit_field != (uint8_t)DISPLAY_MENU_TEXT_FIELD_NONE)
    {
        Display_MenuTextEditExit();
        return 1U;
    }

    switch ((DisplayMenuPage_t)display_state.menu_page)
    {
    case DISPLAY_MENU_PAGE_ROOT:
        switch (display_state.menu_root_selection_index)
        {
        case 0U:
            display_state.menu_bank_selection_index = current_bank;
            display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_BANKS;
            break;
        case 1U:
            display_state.menu_device_selection_index = 0U;
            display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_DEVICES;
            break;
        case 2U:
            display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_GLOBAL;
            display_state.menu_global_selection_index = 0U;
            break;
        case 3U:
            display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_METRONOME;
            display_state.menu_metronome_selection_index = 0U;
            display_state.menu_metronome_quick_access_live = 0U;
            break;
        case 4U:
            Display_MenuMidiMonitorEnter();
            display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_MIDI_MONITOR;
            break;
        default:
            return 0U;
        }
        break;
    case DISPLAY_MENU_PAGE_GLOBAL:
        if (display_state.menu_global_selection_index == (uint8_t)(MENU_GLOBAL_ITEM_COUNT - 1U))
        {
            display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_FACTORY_RESET_CONFIRM;
            Display_MenuRefresh();
            return 1U;
        }

        if (display_state.menu_global_selection_index == 4U
         && global
         && RuntimeConfig_TryGetUserThemeIndex(global->display_mode, NULL))
        {
            display_state.menu_active_user_theme_mode = (uint8_t)global->display_mode;
            display_state.menu_user_theme_selection_index = 0U;
            display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_USER_THEME;
            Display_MenuRefresh();
            return 1U;
        }

        if (display_state.menu_global_selection_index == 9U)
        {
            display_state.menu_expression_selection_index = 0U;
            display_state.menu_expression_field_index = 0U;
            display_state.menu_expression_learn_armed = 0U;
            display_state.menu_text_edit_field = (uint8_t)DISPLAY_MENU_TEXT_FIELD_NONE;
            display_state.menu_text_edit_cursor_index = 0U;
            display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_EXPRESSION;
            Display_MenuRefresh();
            return 1U;
        }

        return 0U;
    case DISPLAY_MENU_PAGE_METRONOME:
        return 0U;
    case DISPLAY_MENU_PAGE_MIDI_MONITOR:
        return 0U;
    case DISPLAY_MENU_PAGE_USER_THEME:
        display_state.menu_user_theme_edit_active = display_state.menu_user_theme_edit_active ? 0U : 1U;
        Display_DrawFootbar();
        return 1U;
    case DISPLAY_MENU_PAGE_BANKS:
        display_state.menu_active_bank_index = display_state.menu_bank_selection_index;
        display_state.menu_bank_edit_selection_index = 0U;
        display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_BANK_EDIT;
        break;
    case DISPLAY_MENU_PAGE_BANK_EDIT:
        text_field = Display_GetMenuTextFieldForSelection();
        if (text_field != DISPLAY_MENU_TEXT_FIELD_NONE)
        {
            Display_MenuTextEditEnter(text_field);
            return 1U;
        }

        if (display_state.menu_bank_edit_selection_index == 3U)
        {
            display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_BANK_INIT_CONFIRM;
            Display_MenuRefresh();
            return 1U;
        }
        else
            return 0U;
        break;
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON:
        text_field = Display_GetMenuTextFieldForSelection();
        if (text_field != DISPLAY_MENU_TEXT_FIELD_NONE)
        {
            Display_MenuTextEditEnter(text_field);
            return 1U;
        }

        return 0U;
    case DISPLAY_MENU_PAGE_DEVICE_EDIT:
        text_field = Display_GetMenuTextFieldForSelection();
        if (text_field != DISPLAY_MENU_TEXT_FIELD_NONE)
        {
            Display_MenuTextEditEnter(text_field);
            return 1U;
        }

        if (display_state.menu_device_edit_selection_index == 12U)
        {
            display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_DEVICE_INIT_CONFIRM;
            Display_MenuRefresh();
            return 1U;
        }

        return 0U;
    case DISPLAY_MENU_PAGE_DEVICES:
        display_state.menu_active_device_index = display_state.menu_device_selection_index;
        display_state.menu_device_edit_selection_index = 0U;
        display_state.menu_device_cc_field_index = 0U;
        display_state.menu_device_cc_field_edit_active = 0U;
        display_state.menu_page = (uint8_t)DISPLAY_MENU_PAGE_DEVICE_EDIT;
        break;
    default:
        return 0U;
    }

    Display_MenuRefresh();
    return 1U;
}

uint8_t Display_MenuCanToggleLearn(void)
{
    if (!display_state.menu_mode_active)
        return 0U;

    if ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_DEVICE_EDIT)
        return Display_MenuDeviceCcRowIsSelected();

    if ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_EXPRESSION)
        return (display_state.menu_expression_selection_index >= 1U) ? 1U : 0U;

    return 0U;
}

uint8_t Display_MenuToggleLearn(void)
{
    if (!Display_MenuCanToggleLearn())
        return 0U;

    if ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_DEVICE_EDIT)
        display_state.menu_device_cc_learn_armed = display_state.menu_device_cc_learn_armed ? 0U : 1U;
    else
        display_state.menu_expression_learn_armed = display_state.menu_expression_learn_armed ? 0U : 1U;

    Display_DrawFootbar();
    return 1U;
}

void Display_MenuApplyMidiLearnIfPending(void)
{
    RuntimeConfigDevice_t *device;
    RuntimeConfigGlobal_t *global;
    uint8_t channel;
    uint8_t cc;
    uint8_t value;

    if (!display_state.menu_mode_active)
        return;

    if (!(display_state.menu_device_cc_learn_armed || display_state.menu_expression_learn_armed))
        return;

    if (!MidiMonitor_TryGetLatestControlChangeAnySource(&channel, &cc, &value))
        return;

    if (display_state.menu_device_cc_learn_armed
        && (DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_DEVICE_EDIT)
    {
        MidiCC_t *selected_cc;

        device = RuntimeConfig_GetMutableDevice(display_state.menu_active_device_index);
        if (!device)
            return;

        selected_cc = Display_GetSelectedDeviceCc(device);
        if (!selected_cc)
            return;

        selected_cc->cc = cc;
        selected_cc->value = value;
        display_state.menu_device_cc_learn_armed = 0U;
        RuntimeConfig_MarkDirty();
        Display_DrawFootbar();
        Display_MenuRedrawCurrentItem();
        (void)channel;
        return;
    }

    if (display_state.menu_expression_learn_armed
        && (DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_EXPRESSION
        && display_state.menu_expression_selection_index >= 1U
        && display_state.menu_expression_selection_index <= RUNTIME_CONFIG_EXPRESSION_PEDAL_CC_SLOT_COUNT)
    {
        RuntimeConfigExpressionPedalCcSlot_t *slot;

        global = RuntimeConfig_GetMutableGlobal();
        if (!global)
            return;

        slot = &global->expression_pedal_cc_slots[display_state.menu_expression_selection_index - 1U];
        slot->cc = cc;
        if (display_state.menu_expression_field_index == 1U)
            slot->heel_value = value;
        else if (display_state.menu_expression_field_index == 2U)
            slot->toe_value = value;

        display_state.menu_expression_learn_armed = 0U;
        RuntimeConfig_MarkDirty();
        Display_DrawFootbar();
        Display_MenuRedrawCurrentItem();
        (void)channel;
    }
}
