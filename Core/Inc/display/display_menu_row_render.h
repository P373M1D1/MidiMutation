#ifndef DISPLAY_MENU_ROW_RENDER_H
#define DISPLAY_MENU_ROW_RENDER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Display_DrawMenuRowByIndex(uint8_t row_index,
						const char *label,
						const char *value,
						uint8_t selected);
void Display_DrawMenuRowValueOnlyByIndex(uint8_t row_index,
							 const char *label,
							 const char *value,
							 uint8_t selected);
void Display_DrawMenuTextEditRowByIndex(uint8_t row_index,
						 const char *label,
						 const char *text,
						 uint8_t cell_count);
void Display_DrawMenuCenteredBadgeRowByIndex(uint8_t row_index,
						      const char *text,
						      uint16_t text_colour,
						      uint16_t badge_colour);
uint16_t Display_GetMenuRowYByIndex(uint8_t row_index);
void Display_ClearStandardMenuRow(uint8_t row_index);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MENU_ROW_RENDER_H */