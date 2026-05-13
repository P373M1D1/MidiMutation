#ifndef DISPLAY_MENU_PAGE_DEVICE_EDIT_H
#define DISPLAY_MENU_PAGE_DEVICE_EDIT_H

#include <stddef.h>
#include <stdint.h>
#include "runtime_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* DEVICE_EDIT page-family surface.
 * These declarations cover both simple field formatting and the denser CC-row
 * editing helpers used only by the device editor. */
/* The CC helpers intentionally expose mutable MidiCC_t pointers because the
 * value-edit module changes sub-fields in place while this page just formats. */
const char *Display_GetMenuDeviceEditLabel(uint8_t item_index,
					   char *buffer,
					   size_t buffer_size);
MidiCC_t *Display_GetDeviceCcForMenuItem(RuntimeConfigDevice_t *device, uint8_t item_index);
uint8_t Display_MenuDeviceCcRowIsSelected(void);
MidiCC_t *Display_GetSelectedDeviceCc(RuntimeConfigDevice_t *device);
void Display_FormatDeviceEditValue(uint8_t item_index, char *buffer, size_t buffer_size);
void Display_DrawMenuDeviceEdit(void);
void Display_DrawMenuDeviceEditItem(uint8_t item_index);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MENU_PAGE_DEVICE_EDIT_H */