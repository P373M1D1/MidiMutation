#include <string.h>

#include "display_functions.h"
#include "display/display_internal.h"
#include "runtime_config.h"

static const char menu_text_edit_charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 ";

DisplayMenuTextField_t Display_GetMenuTextFieldForSelection(void)
{
    if (display_state.menu_page == (uint8_t)DISPLAY_MENU_PAGE_BANK_EDIT)
    {
        if (display_state.menu_bank_edit_selection_index == 0U)
            return DISPLAY_MENU_TEXT_FIELD_BANK_NAME;

        return DISPLAY_MENU_TEXT_FIELD_NONE;
    }

    if (display_state.menu_page == (uint8_t)DISPLAY_MENU_PAGE_FUNCTION_BUTTON)
    {
        switch (display_state.menu_function_button_selection_index)
        {
        case 0U:
            return DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_NAME;
        case 1U:
            return DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_ACTIVE_LABEL;
        case 2U:
            return DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_INACTIVE_LABEL;
        default:
            return DISPLAY_MENU_TEXT_FIELD_NONE;
        }
    }

    if (display_state.menu_page == (uint8_t)DISPLAY_MENU_PAGE_DEVICE_EDIT)
    {
        if (display_state.menu_device_edit_selection_index == 0U)
            return DISPLAY_MENU_TEXT_FIELD_DEVICE_NAME;
    }

    return DISPLAY_MENU_TEXT_FIELD_NONE;
}

uint8_t Display_GetMenuTextFieldLength(DisplayMenuTextField_t field)
{
    switch (field)
    {
    case DISPLAY_MENU_TEXT_FIELD_BANK_NAME:
        return RUNTIME_CONFIG_BANK_NAME_LENGTH;
    case DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_NAME:
        return RUNTIME_CONFIG_FUNCTION_BUTTON_NAME_LENGTH;
    case DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_ACTIVE_LABEL:
    case DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_INACTIVE_LABEL:
        return RUNTIME_CONFIG_FUNCTION_BUTTON_LABEL_LENGTH;
    case DISPLAY_MENU_TEXT_FIELD_DEVICE_NAME:
        return RUNTIME_CONFIG_DEVICE_NAME_LENGTH;
    default:
        return 0U;
    }
}

size_t Display_GetMenuTextFieldCapacity(DisplayMenuTextField_t field)
{
    return (size_t)Display_GetMenuTextFieldLength(field) + 1U;
}

char *Display_GetMenuTextFieldPointer(DisplayMenuTextField_t field)
{
    RuntimeConfigBank_t *bank = RuntimeConfig_GetMutableBank(display_state.menu_active_bank_index);
    RuntimeConfigFunctionButton_t *function_button = RuntimeConfig_GetMutableFunctionButton(display_state.menu_active_bank_index);
    RuntimeConfigDevice_t *device = RuntimeConfig_GetMutableDevice(display_state.menu_active_device_index);

    switch (field)
    {
    case DISPLAY_MENU_TEXT_FIELD_BANK_NAME:
        return bank ? bank->name : NULL;
    case DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_NAME:
        return function_button ? function_button->name : NULL;
    case DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_ACTIVE_LABEL:
        return function_button ? function_button->active_label : NULL;
    case DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_INACTIVE_LABEL:
        return function_button ? function_button->inactive_label : NULL;
    case DISPLAY_MENU_TEXT_FIELD_DEVICE_NAME:
        return device ? device->name : NULL;
    default:
        return NULL;
    }
}

void Display_LoadMenuTextCells(const char *source, uint8_t cell_count, char *cells)
{
    size_t text_length;

    memset(cells, ' ', cell_count);

    if (!source)
        return;

    text_length = strnlen(source, cell_count);
    memcpy(cells, source, text_length);
}

void Display_StoreMenuTextCells(char *destination,
                                size_t destination_size,
                                const char *cells,
                                uint8_t cell_count)
{
    int16_t last_non_space_index;

    if (!destination || destination_size == 0U || !cells)
        return;

    memset(destination, 0, destination_size);

    for (last_non_space_index = (int16_t)cell_count - 1; last_non_space_index >= 0; --last_non_space_index)
    {
        if (cells[last_non_space_index] != ' ')
            break;
    }

    if (last_non_space_index < 0)
        return;

    memcpy(destination, cells, (size_t)last_non_space_index + 1U);
    destination[last_non_space_index + 1] = '\0';
}

int16_t Display_FindMenuTextCharsetIndex(char ch)
{
    for (uint8_t index = 0U; index < (sizeof(menu_text_edit_charset) - 1U); ++index)
    {
        if (menu_text_edit_charset[index] == ch)
            return (int16_t)index;
    }

    return 0;
}

void Display_MenuTextEditEnter(DisplayMenuTextField_t field)
{
    if (field == DISPLAY_MENU_TEXT_FIELD_NONE)
        return;

    display_state.menu_text_edit_field = (uint8_t)field;
    display_state.menu_text_edit_cursor_index = 0U;
    Display_MenuRedrawCurrentItem();
}

uint8_t Display_MenuAdjustTextCharacter(int8_t delta)
{
    char cells[RUNTIME_CONFIG_BANK_NAME_LENGTH];
    char *text_field;
    uint8_t cell_count;
    int16_t current_charset_index;
    int16_t next_charset_index;
    int16_t charset_length = (int16_t)(sizeof(menu_text_edit_charset) - 1U);

    if (delta == 0 || display_state.menu_text_edit_field == (uint8_t)DISPLAY_MENU_TEXT_FIELD_NONE)
        return 0U;

    text_field = Display_GetMenuTextFieldPointer((DisplayMenuTextField_t)display_state.menu_text_edit_field);
    cell_count = Display_GetMenuTextFieldLength((DisplayMenuTextField_t)display_state.menu_text_edit_field);
    if (!text_field
        || cell_count == 0U
        || cell_count > sizeof(cells)
        || display_state.menu_text_edit_cursor_index >= cell_count)
        return 0U;

    Display_LoadMenuTextCells(text_field, cell_count, cells);
    current_charset_index = Display_FindMenuTextCharsetIndex(cells[display_state.menu_text_edit_cursor_index]);
    next_charset_index = current_charset_index + (int16_t)delta;

    while (next_charset_index < 0)
        next_charset_index += charset_length;

    while (next_charset_index >= charset_length)
        next_charset_index -= charset_length;

    if (cells[display_state.menu_text_edit_cursor_index] == menu_text_edit_charset[next_charset_index])
        return 0U;

    cells[display_state.menu_text_edit_cursor_index] = menu_text_edit_charset[next_charset_index];
    Display_StoreMenuTextCells(text_field,
                               Display_GetMenuTextFieldCapacity((DisplayMenuTextField_t)display_state.menu_text_edit_field),
                               cells,
                               cell_count);
    return 1U;
}

uint8_t Display_MenuSubEditorIsActive(void)
{
    return (display_state.menu_text_edit_field != (uint8_t)DISPLAY_MENU_TEXT_FIELD_NONE) ? 1U : 0U;
}

uint8_t Display_MenuTextEditIsActive(void)
{
    return (display_state.menu_text_edit_field != (uint8_t)DISPLAY_MENU_TEXT_FIELD_NONE) ? 1U : 0U;
}

uint8_t Display_MenuTextEditMoveCursor(int8_t delta)
{
    int16_t next_index;
    uint8_t max_index;

    if (!display_state.menu_mode_active
        || delta == 0
        || display_state.menu_text_edit_field == (uint8_t)DISPLAY_MENU_TEXT_FIELD_NONE)
        return 0U;

    max_index = (uint8_t)(Display_GetMenuTextFieldLength((DisplayMenuTextField_t)display_state.menu_text_edit_field) - 1U);
    next_index = (int16_t)display_state.menu_text_edit_cursor_index + (int16_t)delta;
    if (next_index < 0)
        next_index = 0;
    else if (next_index > (int16_t)max_index)
        next_index = (int16_t)max_index;

    if ((uint8_t)next_index == display_state.menu_text_edit_cursor_index)
        return 0U;

    display_state.menu_text_edit_cursor_index = (uint8_t)next_index;
    Display_MenuRedrawCurrentValue();
    return 1U;
}
