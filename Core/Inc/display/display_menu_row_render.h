#ifndef DISPLAY_MENU_ROW_RENDER_H
#define DISPLAY_MENU_ROW_RENDER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard menu-row renderer surface.
 *
 * Page modules provide labels/values and selected state; this API handles the
 * common four-row shell drawing and row-coordinate lookup. */

void Display_DrawMenuRowByIndex(uint8_t row_index,
						const char *label,
						const char *value,
						uint8_t selected);
/* Value-only redraw keeps the same row shell and alignment while skipping page-
 * specific formatting work when only the value/highlight state changed. */
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
/* Standard-row clear is the safe way to erase a row because it uses the same
 * compose/blit path as ordinary row drawing and therefore matches row height. */
void Display_ClearStandardMenuRow(uint8_t row_index);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MENU_ROW_RENDER_H */