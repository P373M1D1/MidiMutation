#include <stdio.h>

#include "display_functions.h"
#include "display/display_internal.h"
#include "display/display_menu_page_function_button_compare.h"
#include "display/display_menu_row_render.h"
#include "runtime_config.h"
#include "display/display_menu_page_function_button.h"

/* Targeted redraw helpers for the FUNCTION_BUTTON page family.
 *
 * The function-button editor has several special-case row types, so its redraw
 * logic is split out of the generic menu redraw path. This file answers the
 * question "which exact row needs repainting now?" for that page family. */

void Display_RedrawMenuFunctionButtonSelectionItem(uint8_t item_index, uint8_t selected)
{
    static const char * const menu_function_button_labels[MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT] = {
        "Name",
        "Active Label",
        "Inactive Label",
    };
    uint8_t row_index;
    uint8_t message_selection_index;
    char value_text[32];

    if (!Display_GetFunctionButtonSelectionRow(item_index, &row_index))
        return;

    if (item_index < MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT)
    {
        Display_FormatFunctionButtonValue(item_index, value_text, sizeof(value_text));
        Display_DrawMenuRowValueOnlyByIndex(row_index,
                                            menu_function_button_labels[item_index],
                                            value_text,
                                            selected);
        return;
    }

    message_selection_index = (uint8_t)(item_index - MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX);
    if (message_selection_index < RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT)
    {
        /* Program rows can redraw in-place with their dense compare layout even
         * when only selection state changed. */
        Display_DrawMenuFunctionButtonProgramCompareEditRowCore(Display_GetMenuRowYByIndex(row_index),
                                                                message_selection_index,
                                                                selected,
                                                                0U,
                                                                0U);
        return;
    }

    message_selection_index = (uint8_t)(message_selection_index - RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT);
    if (selected)
        Display_DrawMenuFunctionButtonCcCompareEditRowNoClear(Display_GetMenuRowYByIndex(row_index), message_selection_index);
    else
    {
        /* Non-selected CC rows fall back to a compact one-line summary instead
         * of the multi-highlight edit layout used by the active row. */
        Display_FormatFunctionButtonCcCompareRow(message_selection_index, value_text, sizeof(value_text));
        Display_DrawMenuRowByIndex(row_index, value_text, "", 0U);
    }
}

uint8_t Display_RedrawMenuFunctionButtonCurrentValueItem(void)
{
    const RuntimeConfigFunctionButton_t *function_button;
    const char *text_value = "";
    uint8_t item_index;
    uint8_t cell_count = 0U;
    uint8_t row_index;
    const char *label = "Name";

    if ((DisplayMenuTextField_t)display_state.menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_NONE)
        return 0U;

    item_index = display_state.menu_function_button_selection_index;
    if (item_index >= MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT)
        return 0U;

    if (!Display_GetFunctionButtonSelectionRow(item_index, &row_index))
        return 0U;

    function_button = Presets_GetFunctionButton(display_state.menu_active_preset_index);

    switch (item_index)
    {
    case 0U:
        label = "Name";
        text_value = function_button ? function_button->name : "";
        cell_count = RUNTIME_CONFIG_FUNCTION_BUTTON_NAME_LENGTH;
        break;
    case 1U:
        label = "Active Label";
        text_value = function_button ? function_button->active_label : "";
        cell_count = RUNTIME_CONFIG_FUNCTION_BUTTON_LABEL_LENGTH;
        break;
    case 2U:
        label = "Inactive Label";
        text_value = function_button ? function_button->inactive_label : "";
        cell_count = RUNTIME_CONFIG_FUNCTION_BUTTON_LABEL_LENGTH;
        break;
    default:
        return 0U;
    }

    Display_DrawMenuTextEditRowByIndex(row_index,
                                       label,
                                       text_value,
                                       cell_count);
    return 1U;
}
