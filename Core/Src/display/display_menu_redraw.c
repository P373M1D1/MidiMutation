#include "display/display_internal.h"
#include "display/display_compose_helpers.h"
#include "display/display_layout.h"
#include "display/display_menu_page_banks.h"
#include "display/display_menu_page_devices.h"
#include "display/display_menu_page_function_button_compare.h"
#include "display/display_menu_pages.h"
#include "display/display_menu_redraw_utils.h"
#include "display/display_menu_row_render.h"

/* Incremental menu redraw orchestration.
 *
 * This file turns selection/value/page changes into targeted row redraws so the
 * menu can update without repainting the full body every time. When debugging a
 * stale row, highlight bug, or overdraw artifact, this is usually the first
 * file to inspect. */

typedef enum
{
    DISPLAY_MENU_DIRTY_ROW_ACTION_NONE = 0,
    DISPLAY_MENU_DIRTY_ROW_ACTION_SELECTION,
    DISPLAY_MENU_DIRTY_ROW_ACTION_CURRENT_VALUE,
    DISPLAY_MENU_DIRTY_ROW_ACTION_BANK_WINDOW,
    DISPLAY_MENU_DIRTY_ROW_ACTION_DEVICE_WINDOW,
    DISPLAY_MENU_DIRTY_ROW_ACTION_FUNCTION_BUTTON_PROGRAM_WINDOW,
    DISPLAY_MENU_DIRTY_ROW_ACTION_FUNCTION_BUTTON_CC_WINDOW,
    DISPLAY_MENU_DIRTY_ROW_ACTION_CLEAR,
} DisplayMenuDirtyRowAction_t;

static void Display_MenuQueueDirtyRowAction(uint8_t row_index,
                                            uint8_t item_index,
                                            DisplayMenuDirtyRowAction_t action,
                                            uint8_t *dirty_row_mask,
                                            uint8_t dirty_row_items[MENU_VISIBLE_ROW_COUNT],
                                            DisplayMenuDirtyRowAction_t dirty_row_actions[MENU_VISIBLE_ROW_COUNT])
{
    if (!dirty_row_mask || !dirty_row_items || !dirty_row_actions || row_index >= MENU_VISIBLE_ROW_COUNT)
        return;

    /* Last request wins for a row within one redraw pass; that keeps the queue
     * compact and avoids painting the same row multiple times in one update. */
    *dirty_row_mask = (uint8_t)(*dirty_row_mask | (uint8_t)(1U << row_index));
    dirty_row_items[row_index] = item_index;
    dirty_row_actions[row_index] = action;
}

static void Display_MenuQueueDirtySelectionItem(DisplayMenuPage_t page,
                                                uint8_t item_index,
                                                uint8_t *dirty_row_mask,
                                                uint8_t dirty_row_items[MENU_VISIBLE_ROW_COUNT],
                                                DisplayMenuDirtyRowAction_t dirty_row_actions[MENU_VISIBLE_ROW_COUNT])
{
    uint8_t row_index;

    if (!dirty_row_mask || !dirty_row_items || !dirty_row_actions)
        return;

    if (!Display_GetMenuVisibleRowIndex(page, item_index, &row_index) || row_index >= MENU_VISIBLE_ROW_COUNT)
        return;

    Display_MenuQueueDirtyRowAction(row_index,
                                    item_index,
                                    DISPLAY_MENU_DIRTY_ROW_ACTION_SELECTION,
                                    dirty_row_mask,
                                    dirty_row_items,
                                    dirty_row_actions);
}

static void Display_MenuQueueDirtyWindowRows(DisplayMenuPage_t page,
                                             uint8_t first_visible_index,
                                             uint8_t *dirty_row_mask,
                                             uint8_t dirty_row_items[MENU_VISIBLE_ROW_COUNT],
                                             DisplayMenuDirtyRowAction_t dirty_row_actions[MENU_VISIBLE_ROW_COUNT])
{
    uint8_t item_count;
    DisplayMenuDirtyRowAction_t row_action;

    switch (page)
    {
    case DISPLAY_MENU_PAGE_BANKS:
        item_count = PRESET_BANK_COUNT;
        row_action = DISPLAY_MENU_DIRTY_ROW_ACTION_BANK_WINDOW;
        break;
    case DISPLAY_MENU_PAGE_DEVICES:
        item_count = MIDI_DEVICE_COUNT;
        row_action = DISPLAY_MENU_DIRTY_ROW_ACTION_DEVICE_WINDOW;
        break;
    default:
        return;
    }

    for (uint8_t row_index = 0U; row_index < MENU_VISIBLE_ROW_COUNT; ++row_index)
    {
        uint8_t item_index = (uint8_t)(first_visible_index + row_index);

        if (item_index >= item_count)
        {
            Display_MenuQueueDirtyRowAction(row_index,
                                            0U,
                                            DISPLAY_MENU_DIRTY_ROW_ACTION_CLEAR,
                                            dirty_row_mask,
                                            dirty_row_items,
                                            dirty_row_actions);
            continue;
        }

        Display_MenuQueueDirtyRowAction(row_index,
                                        item_index,
                                        row_action,
                                        dirty_row_mask,
                                        dirty_row_items,
                                        dirty_row_actions);
    }
}

static void Display_MenuQueueDirtyFunctionButtonWindowRows(uint8_t layout_signature,
                                                           uint8_t window_start,
                                                           uint8_t *dirty_row_mask,
                                                           uint8_t dirty_row_items[MENU_VISIBLE_ROW_COUNT],
                                                           DisplayMenuDirtyRowAction_t dirty_row_actions[MENU_VISIBLE_ROW_COUNT])
{
    if (layout_signature == 3U)
    {
        for (uint8_t row_offset = 0U; row_offset < (MENU_VISIBLE_ROW_COUNT - 1U); ++row_offset)
        {
            Display_MenuQueueDirtyRowAction((uint8_t)(row_offset + 1U),
                                            (uint8_t)(window_start + row_offset),
                                            DISPLAY_MENU_DIRTY_ROW_ACTION_FUNCTION_BUTTON_PROGRAM_WINDOW,
                                            dirty_row_mask,
                                            dirty_row_items,
                                            dirty_row_actions);
        }

        return;
    }

    if (layout_signature == 5U)
    {
        for (uint8_t row_offset = 0U; row_offset < (MENU_VISIBLE_ROW_COUNT - 2U); ++row_offset)
        {
            Display_MenuQueueDirtyRowAction((uint8_t)(row_offset + 2U),
                                            (uint8_t)(window_start + row_offset),
                                            DISPLAY_MENU_DIRTY_ROW_ACTION_FUNCTION_BUTTON_CC_WINDOW,
                                            dirty_row_mask,
                                            dirty_row_items,
                                            dirty_row_actions);
        }
    }
}

static void Display_MenuFlushDirtyRows(DisplayMenuPage_t page,
                                       uint8_t dirty_row_mask,
                                       const uint8_t dirty_row_items[MENU_VISIBLE_ROW_COUNT],
                                       const DisplayMenuDirtyRowAction_t dirty_row_actions[MENU_VISIBLE_ROW_COUNT])
{
    uint8_t current_selection = Display_GetMenuSelectionIndexForPage(page);

    if (!dirty_row_items || !dirty_row_actions)
        return;

    for (uint8_t row_index = 0U; row_index < MENU_VISIBLE_ROW_COUNT; ++row_index)
    {
        if ((dirty_row_mask & (uint8_t)(1U << row_index)) == 0U)
            continue;

        switch (dirty_row_actions[row_index])
        {
        case DISPLAY_MENU_DIRTY_ROW_ACTION_SELECTION:
            Display_RedrawMenuSelectionItem(page,
                                            dirty_row_items[row_index],
                                            (dirty_row_items[row_index] == current_selection) ? 1U : 0U);
            break;

        case DISPLAY_MENU_DIRTY_ROW_ACTION_CURRENT_VALUE:
            Display_RedrawMenuCurrentValueItem(page, dirty_row_items[row_index]);
            break;

        case DISPLAY_MENU_DIRTY_ROW_ACTION_BANK_WINDOW:
            Display_DrawMenuBankWindowRow(row_index, dirty_row_items[row_index]);
            break;

        case DISPLAY_MENU_DIRTY_ROW_ACTION_DEVICE_WINDOW:
            Display_DrawMenuDeviceWindowRow(row_index, dirty_row_items[row_index]);
            break;

        case DISPLAY_MENU_DIRTY_ROW_ACTION_FUNCTION_BUTTON_PROGRAM_WINDOW:
            Display_DrawMenuFunctionButtonProgramWindowRow(row_index, dirty_row_items[row_index]);
            break;

        case DISPLAY_MENU_DIRTY_ROW_ACTION_FUNCTION_BUTTON_CC_WINDOW:
            Display_DrawMenuFunctionButtonCcWindowRow(row_index, dirty_row_items[row_index]);
            break;

        case DISPLAY_MENU_DIRTY_ROW_ACTION_CLEAR:
            Display_ClearStandardMenuRow(row_index);
            break;

        case DISPLAY_MENU_DIRTY_ROW_ACTION_NONE:
        default:
            break;
        }
    }
}

void Display_MenuRefreshBodyOnly(void)
{
    uint8_t current_is_midi_monitor = ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_MIDI_MONITOR) ? 1U : 0U;
    uint8_t previous_is_midi_monitor = ((DisplayMenuPage_t)display_state.menu_last_drawn_page == DISPLAY_MENU_PAGE_MIDI_MONITOR) ? 1U : 0U;

    /* Freeform bodies and page transitions can rearrange headers/badges in ways
     * the row-diff path cannot express, so fall back to a full body clear. */
    if (!display_state.menu_draw_state_valid
     || ((Display_MenuPageUsesFreeformBody((DisplayMenuPage_t)display_state.menu_page)
       || Display_MenuPageUsesFreeformBody((DisplayMenuPage_t)display_state.menu_last_drawn_page))
      && !(current_is_midi_monitor && previous_is_midi_monitor)))
    {
        Display_ClearMenuBody();
    }

    Display_DrawCurrentMenuPageBody();
    display_state.menu_last_drawn_page = (uint8_t)display_state.menu_page;
    display_state.menu_draw_state_valid = 1U;
}

void Display_MenuRedrawCurrentPageRows(void)
{
    Display_DrawCurrentMenuPageBody();
    display_state.menu_last_drawn_page = (uint8_t)display_state.menu_page;
    display_state.menu_draw_state_valid = 1U;
}

void Display_MenuRedrawCurrentItem(void)
{
    if (display_state.menu_text_edit_field != (uint8_t)DISPLAY_MENU_TEXT_FIELD_NONE)
    {
        Display_DrawMenuPageItem((DisplayMenuPage_t)display_state.menu_page,
                                 Display_GetMenuSelectionIndexForPage((DisplayMenuPage_t)display_state.menu_page));
        return;
    }

    if (display_state.menu_page == (uint8_t)DISPLAY_MENU_PAGE_FUNCTION_BUTTON
     || display_state.menu_page == (uint8_t)DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES
     || display_state.menu_page == (uint8_t)DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES)
    {
        Display_RedrawMenuSelectionItem((DisplayMenuPage_t)display_state.menu_page,
                                        display_state.menu_function_button_selection_index,
                                        1U);
        return;
    }

    if (display_state.menu_page == (uint8_t)DISPLAY_MENU_PAGE_DEVICE_EDIT)
    {
        Display_RedrawMenuSelectionItem((DisplayMenuPage_t)display_state.menu_page,
                                        display_state.menu_device_edit_selection_index,
                                        1U);
        return;
    }

    Display_DrawMenuPageItem((DisplayMenuPage_t)display_state.menu_page,
                             Display_GetMenuSelectionIndexForPage((DisplayMenuPage_t)display_state.menu_page));
}

void Display_MenuRedrawCurrentValue(void)
{
    uint8_t item_index;

    if (!display_state.menu_mode_active || !display_state.menu_draw_state_valid)
    {
        Display_MenuRedrawCurrentItem();
        return;
    }

    item_index = Display_GetMenuSelectionIndexForPage((DisplayMenuPage_t)display_state.menu_page);
    Display_RedrawMenuCurrentValueItem((DisplayMenuPage_t)display_state.menu_page, item_index);
}

void Display_MenuRedrawSelectionChange(DisplayMenuPage_t page, uint8_t previous_selection)
{
    uint8_t current_selection = Display_GetMenuSelectionIndexForPage(page);
    uint8_t previous_first_visible = Display_GetMenuFirstVisibleIndexForPage(page, previous_selection);
    uint8_t current_first_visible = Display_GetMenuFirstVisibleIndexForPage(page, current_selection);
    uint8_t dirty_row_mask = 0U;
    uint8_t dirty_row_items[MENU_VISIBLE_ROW_COUNT] = { 0U };
    DisplayMenuDirtyRowAction_t dirty_row_actions[MENU_VISIBLE_ROW_COUNT] = { DISPLAY_MENU_DIRTY_ROW_ACTION_NONE };

    if (page == DISPLAY_MENU_PAGE_FUNCTION_BUTTON
     || page == DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES
     || page == DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES)
    {
        uint8_t previous_window_start = 0U;
        uint8_t current_window_start = 0U;
        uint8_t previous_signature = Display_GetFunctionButtonLayoutSignature(previous_selection, &previous_window_start);
        uint8_t current_signature = Display_GetFunctionButtonLayoutSignature(current_selection, &current_window_start);

        if (previous_signature != current_signature)
        {
            Display_MenuRedrawCurrentPageRows();
            return;
        }

        if (previous_window_start != current_window_start)
        {
            if (current_signature == 3U || current_signature == 5U)
            {
                Display_MenuQueueDirtyFunctionButtonWindowRows(current_signature,
                                                              current_window_start,
                                                              &dirty_row_mask,
                                                              dirty_row_items,
                                                              dirty_row_actions);
                Display_MenuFlushDirtyRows(page,
                                           dirty_row_mask,
                                           dirty_row_items,
                                           dirty_row_actions);
                return;
            }

            Display_MenuRedrawCurrentPageRows();
            return;
        }

        Display_MenuQueueDirtySelectionItem(page,
                                            previous_selection,
                                            &dirty_row_mask,
                                            dirty_row_items,
                                            dirty_row_actions);
        if (current_selection != previous_selection)
            Display_MenuQueueDirtySelectionItem(page,
                                                current_selection,
                                                &dirty_row_mask,
                                                dirty_row_items,
                                                dirty_row_actions);
        Display_MenuFlushDirtyRows(page, dirty_row_mask, dirty_row_items, dirty_row_actions);
        return;
    }

    if (previous_first_visible != current_first_visible)
    {
        if (page == DISPLAY_MENU_PAGE_BANKS || page == DISPLAY_MENU_PAGE_DEVICES)
        {
            Display_MenuQueueDirtyWindowRows(page,
                                             current_first_visible,
                                             &dirty_row_mask,
                                             dirty_row_items,
                                             dirty_row_actions);
            Display_MenuFlushDirtyRows(page,
                                       dirty_row_mask,
                                       dirty_row_items,
                                       dirty_row_actions);
            return;
        }

        Display_MenuRedrawCurrentPageRows();
        return;
    }

    Display_MenuQueueDirtySelectionItem(page,
                                        previous_selection,
                                        &dirty_row_mask,
                                        dirty_row_items,
                                        dirty_row_actions);
    if (current_selection != previous_selection)
        Display_MenuQueueDirtySelectionItem(page,
                                            current_selection,
                                            &dirty_row_mask,
                                            dirty_row_items,
                                            dirty_row_actions);
    Display_MenuFlushDirtyRows(page, dirty_row_mask, dirty_row_items, dirty_row_actions);
}

void Display_MenuRefresh(void)
{
    DisplayMenuPage_t current_page = (DisplayMenuPage_t)display_state.menu_page;
    DisplayMenuPage_t previous_page = (DisplayMenuPage_t)display_state.menu_last_drawn_page;
    uint8_t header_changed = 0U;
    uint8_t current_uses_monitor_chrome = (current_page == DISPLAY_MENU_PAGE_MIDI_MONITOR) ? 1U : 0U;
    uint8_t previous_uses_monitor_chrome = (previous_page == DISPLAY_MENU_PAGE_MIDI_MONITOR) ? 1U : 0U;

    if (!display_state.menu_mode_active)
        return;

    /* Gallery is fullscreen: bypass all chrome (header, footbar, body clear)
     * and let the gallery renderer own the entire 480×320 display. */
    if (current_page == DISPLAY_MENU_PAGE_GALLERY)
    {
        display_state.main_layout_dirty = 0U;
        Display_DrawCurrentMenuPageBody();
        display_state.menu_last_drawn_page = (uint8_t)display_state.menu_page;
        display_state.menu_draw_state_valid = 1U;
        return;
    }

    /* If we are returning from the gallery page, the entire screen must be
     * redrawn because the gallery wrote over the chrome areas. */
    if (previous_page == DISPLAY_MENU_PAGE_GALLERY)
        display_state.menu_draw_state_valid = 0U;

    display_state.main_layout_dirty = 0U;

    if (display_state.menu_draw_state_valid)
        header_changed = Display_MenuHeaderChanged(previous_page, current_page);

    if (!display_state.menu_draw_state_valid)
    {
        if (current_uses_monitor_chrome)
        {
            ST7796_DrawFilledRectangle(0U,
                                       0U,
                                       ST7796_WIDTH,
                                       MAIN_PRESET_TEXT_Y,
                                       BLACK);
        }
        else
        {
            Display_DrawThemeBackgroundBand(0U,
                                            MAIN_PRESET_TEXT_Y);
        }
        Display_DrawFootbar();
        Display_DrawMainModeHeader();
    }
    else
    {
        if (header_changed || current_uses_monitor_chrome || previous_uses_monitor_chrome)
        {
            if (current_uses_monitor_chrome)
            {
                ST7796_DrawFilledRectangle(0U,
                                           0U,
                                           ST7796_WIDTH,
                                           MAIN_PRESET_TEXT_Y,
                                           BLACK);
            }
            else
            {
                Display_DrawThemeBackgroundBand(0U,
                                                MAIN_PRESET_TEXT_Y);
            }
        }

        if (Display_MenuPageUsesConfirmFootbar((DisplayMenuPage_t)display_state.menu_last_drawn_page)
            != Display_MenuPageUsesConfirmFootbar(current_page)
         || current_uses_monitor_chrome
         || previous_uses_monitor_chrome)
            Display_DrawFootbar();

        if (header_changed || current_uses_monitor_chrome || previous_uses_monitor_chrome)
            Display_DrawMainModeHeader();
    }

    Display_MenuRefreshBodyOnly();
}
