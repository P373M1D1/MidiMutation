#ifndef DISPLAY_ROW_COMPOSE_H
#define DISPLAY_ROW_COMPOSE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Low-level row-buffer compose helpers.
 *
 * These are below the page/menu renderer level: they operate on one menu-row
 * compose surface and are shared by the denser row renderers that cannot use a
 * simple label/value layout. */
/* Optional-field formatting is used by compare/CC editors to show sentinel
 * values as dashes without duplicating that policy in each page module. */
void Display_FormatMenuOptionalField(char *buffer,
				     size_t buffer_size,
				     uint8_t value,
				     uint8_t unused_value,
				     uint8_t digits,
				     uint8_t highlighted);
void Display_MenuRowComposeClear(uint16_t colour);
void Display_MenuRowComposeTextSegment32(uint16_t x,
					 const char *text,
					 uint16_t foreground,
					 uint16_t background);
/* Returns the next X position after drawing, which is why dense row builders
 * can chain multiple calls while staying aligned. */
uint16_t Display_MenuRowComposeValueSegment32(uint16_t x,
					      const char *text,
					      uint8_t highlighted);
void Display_MenuRowComposeBlit(uint16_t row_y);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_ROW_COMPOSE_H */