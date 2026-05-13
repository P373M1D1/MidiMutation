#ifndef DISPLAY_MENU_REDRAW_UTILS_H
#define DISPLAY_MENU_REDRAW_UTILS_H

#include "display/display_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t Display_GetMenuFirstVisibleIndex(uint8_t item_count, uint8_t selected_index);
uint8_t Display_GetMenuSelectionIndexForPage(DisplayMenuPage_t page);
uint8_t Display_GetMenuFirstVisibleIndexForPage(DisplayMenuPage_t page, uint8_t selection_index);
uint8_t Display_GetMenuVisibleRowIndex(DisplayMenuPage_t page, uint8_t item_index, uint8_t *row_index);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MENU_REDRAW_UTILS_H */