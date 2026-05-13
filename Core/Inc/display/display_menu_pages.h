#ifndef DISPLAY_MENU_PAGES_H
#define DISPLAY_MENU_PAGES_H

#include "display/display_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t Display_MenuPageUsesConfirmFootbar(DisplayMenuPage_t page);
uint8_t Display_MenuPageUsesFreeformBody(DisplayMenuPage_t page);
uint8_t Display_MenuHeaderChanged(DisplayMenuPage_t previous_page, DisplayMenuPage_t current_page);
void Display_DrawFootbar(void);
void Display_DrawMainModeHeader(void);
void Display_DrawPresetInitConfirmPrompt(void);
void Display_ClearMenuBody(void);
void Display_DrawCurrentMenuPageBody(void);
void Display_DrawMenuPageItem(DisplayMenuPage_t page, uint8_t item_index);
uint8_t *Display_GetMenuPageSelectionPointer(DisplayMenuPage_t page);
uint8_t Display_GetMenuPageItemCount(DisplayMenuPage_t page);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MENU_PAGES_H */