#include <stdio.h>

#include "display_functions.h"
#include "display/display_internal.h"
#include "display/display_menu_page_function_button.h"
#include "display/display_menu_row_render.h"
#include "runtime_config.h"

/* FUNCTION_BUTTON text-row renderer.
 *
 * The first rows of the function-button page are plain editable text fields
 * (name, active label, inactive label). This module handles only those rows;
 * the denser compare/message tables live in separate function-button files. */

void Display_FormatFunctionButtonValue(uint8_t item_index, char *buffer, size_t buffer_size)
{
    const RuntimeConfigFunctionButton_t *function_button = Presets_GetFunctionButton(display_state.menu_active_preset_index);

    if (!buffer || buffer_size == 0U || !function_button)
        return;

    switch (item_index)
    {
    case 0U:
        (void)snprintf(buffer, buffer_size, "%s", function_button->name);
        break;
    case 1U:
        (void)snprintf(buffer, buffer_size, "%s", function_button->active_label);
        break;
    case 2U:
        (void)snprintf(buffer, buffer_size, "%s", function_button->inactive_label);
        break;
    default:
        buffer[0] = '\0';
        break;
    }
}

void Display_DrawMenuFunctionButtonTextItemAtRow(uint8_t item_index, uint8_t row_index)
{
    static const char * const menu_function_button_labels[MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT] = {
        "Name",
        "Active Label",
        "Inactive Label",
    };
    const RuntimeConfigFunctionButton_t *function_button = Presets_GetFunctionButton(display_state.menu_active_preset_index);
    char value_text[20];

    if (item_index >= MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT || row_index >= MENU_VISIBLE_ROW_COUNT)
    {
        if (row_index < MENU_VISIBLE_ROW_COUNT)
            Display_ClearStandardMenuRow(row_index);
        return;
    }

    if ((item_index == 0U && (DisplayMenuTextField_t)display_state.menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_NAME)
     || (item_index == 1U && (DisplayMenuTextField_t)display_state.menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_ACTIVE_LABEL)
     || (item_index == 2U && (DisplayMenuTextField_t)display_state.menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_INACTIVE_LABEL))
    {
        const char *text_value = "";
        uint8_t cell_count = 0U;

        /* Once a text row enters edit mode, render it as fixed cells instead of
         * a normal value string so the cursor can move without layout jitter. */
        switch (item_index)
        {
        case 0U:
            text_value = function_button ? function_button->name : "";
            cell_count = RUNTIME_CONFIG_FUNCTION_BUTTON_NAME_LENGTH;
            break;
        case 1U:
            text_value = function_button ? function_button->active_label : "";
            cell_count = RUNTIME_CONFIG_FUNCTION_BUTTON_LABEL_LENGTH;
            break;
        case 2U:
            text_value = function_button ? function_button->inactive_label : "";
            cell_count = RUNTIME_CONFIG_FUNCTION_BUTTON_LABEL_LENGTH;
            break;
        default:
            break;
        }

        Display_DrawMenuTextEditRowByIndex(row_index,
                                           menu_function_button_labels[item_index],
                                           text_value,
                                           cell_count);
        return;
    }

    Display_FormatFunctionButtonValue(item_index, value_text, sizeof(value_text));
    Display_DrawMenuRowByIndex(row_index,
                               menu_function_button_labels[item_index],
                               value_text,
                               (item_index == display_state.menu_function_button_selection_index) ? 1U : 0U);
}
