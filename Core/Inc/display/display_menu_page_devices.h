#ifndef DISPLAY_MENU_PAGE_DEVICES_H
#define DISPLAY_MENU_PAGE_DEVICES_H

#include <stddef.h>
#include <stdint.h>
#include "runtime_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internal DEVICES page helpers shared by redraw, page dispatch, and device edit. */
void Display_FormatMenuDeviceLabel(uint8_t device_index, char *buffer, size_t buffer_size);
void Display_FormatMenuDeviceListValue(const RuntimeConfigDevice_t *device,
					       char *buffer,
					       size_t buffer_size);
void Display_DrawMenuDevices(void);
void Display_DrawMenuDeviceItem(uint8_t item_index);
void Display_DrawMenuDeviceWindowRow(uint8_t row_index, uint8_t device_index);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MENU_PAGE_DEVICES_H */