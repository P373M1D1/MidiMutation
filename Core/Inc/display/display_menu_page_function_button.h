#ifndef DISPLAY_MENU_PAGE_FUNCTION_BUTTON_H
#define DISPLAY_MENU_PAGE_FUNCTION_BUTTON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FUNCTION_BUTTON page-family surface.
 * Split across multiple .c files, but grouped here because compare rendering,
 * text rows, and redraw all belong to the same logical editor. */
void Display_FormatFunctionButtonValue(uint8_t item_index, char *buffer, size_t buffer_size);
void Display_DrawMenuFunctionButtonTextItemAtRow(uint8_t item_index, uint8_t row_index);
/* The full page entry point redraws different row layouts depending on where
 * the current selection sits inside the text/program/CC sections. */
void Display_DrawMenuFunctionButton(void);
void Display_DrawMenuFunctionButtonItem(uint8_t item_index);
void Display_RedrawMenuFunctionButtonSelectionItem(uint8_t item_index, uint8_t selected);
uint8_t Display_RedrawMenuFunctionButtonCurrentValueItem(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MENU_PAGE_FUNCTION_BUTTON_H */