#ifndef DISPLAY_MENU_PAGE_FUNCTION_BUTTON_H
#define DISPLAY_MENU_PAGE_FUNCTION_BUTTON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Internal FUNCTION_BUTTON page helpers shared by compare, redraw, and page dispatch. */
void Display_FormatFunctionButtonValue(uint8_t item_index, char *buffer, size_t buffer_size);
void Display_DrawMenuFunctionButtonTextItemAtRow(uint8_t item_index, uint8_t row_index);
void Display_DrawMenuFunctionButton(void);
void Display_DrawMenuFunctionButtonItem(uint8_t item_index);
void Display_RedrawMenuFunctionButtonSelectionItem(uint8_t item_index, uint8_t selected);
uint8_t Display_RedrawMenuFunctionButtonCurrentValueItem(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MENU_PAGE_FUNCTION_BUTTON_H */