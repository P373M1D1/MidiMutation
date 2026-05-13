#ifndef DISPLAY_MENU_PAGE_FUNCTION_BUTTON_COMPARE_H
#define DISPLAY_MENU_PAGE_FUNCTION_BUTTON_COMPARE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Internal FUNCTION_BUTTON compare helpers shared by redraw modules. */
/* The layout signature encodes which special four-row arrangement is active:
 * text rows, program header/window, or CC header/window. Redraw code uses it
 * to keep the compare table stable while selection moves. */
uint8_t Display_GetFunctionButtonLayoutSignature(uint8_t selection_index, uint8_t *window_start);
uint8_t Display_GetFunctionButtonSelectionRow(uint8_t selection_index, uint8_t *row_index);
uint8_t Display_GetFunctionButtonMessageSelectionIndex(void);
void Display_FormatFunctionButtonCcCompareRow(uint8_t cc_index,
					 char *buffer,
					 size_t buffer_size);
void Display_DrawMenuFunctionButtonProgramCompareEditRowCore(uint16_t row_y,
						     uint8_t program_index,
						     uint8_t selected,
						     uint8_t clear_row,
						     uint8_t redraw_number);
void Display_DrawMenuFunctionButtonCcCompareEditRowNoClear(uint16_t row_y, uint8_t cc_index);
void Display_DrawMenuFunctionButtonProgramWindowRow(uint8_t row_index, uint8_t program_index);
void Display_DrawMenuFunctionButtonCcWindowRow(uint8_t row_index, uint8_t cc_index);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MENU_PAGE_FUNCTION_BUTTON_COMPARE_H */