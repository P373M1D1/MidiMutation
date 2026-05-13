#ifndef DISPLAY_MENU_PAGE_GLOBAL_H
#define DISPLAY_MENU_PAGE_GLOBAL_H

#include <stddef.h>
#include <stdint.h>

#include "runtime_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* GLOBAL page renderer surface.
 * This page is where persisted global settings become user-facing strings. */
/* item_index matches the persisted global-setting order, so changing that order
 * in runtime_config.h should be mirrored here to keep labels/values aligned. */
void Display_FormatGlobalMenuValue(uint8_t item_index, char *buffer, size_t buffer_size);
void Display_DrawMenuGlobal(void);
void Display_DrawMenuGlobalItem(uint8_t item_index);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MENU_PAGE_GLOBAL_H */