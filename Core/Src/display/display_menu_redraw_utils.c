#include "display/display_layout.h"
#include "display/display_menu_page_function_button_compare.h"
#include "display/display_menu_redraw_utils.h"
#include "midi_devices.h"
#include "presets.h"
#include "runtime_config.h"

/* Shared menu selection/window math.
 *
 * These helpers are intentionally pure utility code: they translate page state
 * into visible-row indices, first-visible offsets, and selection mapping without
 * drawing anything. Reuse them whenever a menu view needs the same windowing
 * rule so redraw and controller logic stay aligned. */

#ifndef MENU_FUNCTION_BUTTON_MESSAGE_ROW_COUNT
#define MENU_FUNCTION_BUTTON_MESSAGE_ROW_COUNT (RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT + RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT)
#endif

uint8_t Display_GetMenuFirstVisibleIndex(uint8_t item_count, uint8_t selected_index)
{
    if (item_count <= MENU_VISIBLE_ROW_COUNT)
        return 0U;

    /* Clamp stale selections first so callers can reuse this helper while the
     * underlying item count changes without manually sanitizing selection. */
    if (selected_index >= item_count)
        selected_index = (uint8_t)(item_count - 1U);

    if (selected_index < MENU_VISIBLE_ROW_COUNT)
        return 0U;

    return (uint8_t)(selected_index - (MENU_VISIBLE_ROW_COUNT - 1U));
}

uint8_t Display_GetMenuSelectionIndexForPage(DisplayMenuPage_t page)
{
    switch (page)
    {
    case DISPLAY_MENU_PAGE_ROOT:
        return display_state.menu_root_selection_index;
    case DISPLAY_MENU_PAGE_BANKS:
        return display_state.menu_bank_selection_index;
    case DISPLAY_MENU_PAGE_BANK_EDIT:
        return display_state.menu_bank_edit_selection_index;
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES:
        return display_state.menu_function_button_selection_index;
    case DISPLAY_MENU_PAGE_DEVICES:
        return display_state.menu_device_selection_index;
    case DISPLAY_MENU_PAGE_DEVICE_EDIT:
        return display_state.menu_device_edit_selection_index;
    case DISPLAY_MENU_PAGE_GLOBAL:
        return display_state.menu_global_selection_index;
    default:
        return 0U;
    }
}

uint8_t Display_GetMenuFirstVisibleIndexForPage(DisplayMenuPage_t page, uint8_t selection_index)
{
    switch (page)
    {
    case DISPLAY_MENU_PAGE_GLOBAL:
        return Display_GetMenuFirstVisibleIndex(MENU_GLOBAL_ITEM_COUNT, selection_index);
    case DISPLAY_MENU_PAGE_BANKS:
        return Display_GetMenuFirstVisibleIndex(PRESET_BANK_COUNT, selection_index);
    case DISPLAY_MENU_PAGE_BANK_EDIT:
        return Display_GetMenuFirstVisibleIndex(MENU_BANK_EDIT_ITEM_COUNT, selection_index);
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON:
        return Display_GetMenuFirstVisibleIndex(MENU_FUNCTION_BUTTON_ITEM_COUNT, selection_index);
    case DISPLAY_MENU_PAGE_DEVICES:
        return Display_GetMenuFirstVisibleIndex(MIDI_DEVICE_COUNT, selection_index);
    case DISPLAY_MENU_PAGE_DEVICE_EDIT:
        return Display_GetMenuFirstVisibleIndex(MENU_DEVICE_EDIT_ITEM_COUNT, selection_index);
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES:
        return Display_GetMenuFirstVisibleIndex(MENU_FUNCTION_BUTTON_MESSAGE_ROW_COUNT, selection_index);
    default:
        return 0U;
    }
}

uint8_t Display_GetMenuVisibleRowIndex(DisplayMenuPage_t page, uint8_t item_index, uint8_t *row_index)
{
    uint8_t current_selection = Display_GetMenuSelectionIndexForPage(page);
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndexForPage(page, current_selection);

    if (!row_index)
        return 0U;

    switch (page)
    {
    case DISPLAY_MENU_PAGE_ROOT:
        if (item_index >= MENU_VISIBLE_ROW_COUNT)
            return 0U;

        *row_index = item_index;
        return 1U;

    case DISPLAY_MENU_PAGE_GLOBAL:
    case DISPLAY_MENU_PAGE_BANKS:
    case DISPLAY_MENU_PAGE_BANK_EDIT:
    case DISPLAY_MENU_PAGE_DEVICES:
    case DISPLAY_MENU_PAGE_DEVICE_EDIT:
        if (item_index < first_visible_index || item_index >= (uint8_t)(first_visible_index + MENU_VISIBLE_ROW_COUNT))
            return 0U;

        *row_index = (uint8_t)(item_index - first_visible_index);
        return 1U;

    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES:
        /* FUNCTION_BUTTON uses custom header rows and mixed-height windows, so
         * row mapping comes from the compare-layout helper instead of standard
         * first-visible-index subtraction. */
        return Display_GetFunctionButtonSelectionRow(item_index, row_index);

    default:
        return 0U;
    }
}
