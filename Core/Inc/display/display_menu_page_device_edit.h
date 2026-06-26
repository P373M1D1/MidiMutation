#ifndef DISPLAY_MENU_PAGE_DEVICE_EDIT_H
#define DISPLAY_MENU_PAGE_DEVICE_EDIT_H

#include <stddef.h>
#include <stdint.h>
#include "runtime_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MENU_DEVICE_EDIT_ITEM_NAME              0U
#define MENU_DEVICE_EDIT_ITEM_MAX_PRESET        1U
#define MENU_DEVICE_EDIT_ITEM_CHANNEL           2U
#define MENU_DEVICE_EDIT_ITEM_ACTIVE_CC         3U
#define MENU_DEVICE_EDIT_ITEM_ACTIVE_AUTO_FIRST 4U
#define MENU_DEVICE_EDIT_ITEM_BYPASS_CC         (MENU_DEVICE_EDIT_ITEM_ACTIVE_AUTO_FIRST + RUNTIME_CONFIG_DEVICE_AUTO_CC_COUNT)
#define MENU_DEVICE_EDIT_ITEM_BYPASS_AUTO_FIRST (MENU_DEVICE_EDIT_ITEM_BYPASS_CC + 1U)
#define MENU_DEVICE_EDIT_ITEM_TAP_CC            (MENU_DEVICE_EDIT_ITEM_BYPASS_AUTO_FIRST + RUNTIME_CONFIG_DEVICE_AUTO_CC_COUNT)
#define MENU_DEVICE_EDIT_ITEM_VOLUME1_CC        (MENU_DEVICE_EDIT_ITEM_TAP_CC + 1U)
#define MENU_DEVICE_EDIT_ITEM_VOLUME2_CC        (MENU_DEVICE_EDIT_ITEM_VOLUME1_CC + 1U)
#define MENU_DEVICE_EDIT_ITEM_MIX1_CC           (MENU_DEVICE_EDIT_ITEM_VOLUME2_CC + 1U)
#define MENU_DEVICE_EDIT_ITEM_MIX2_CC           (MENU_DEVICE_EDIT_ITEM_MIX1_CC + 1U)
#define MENU_DEVICE_EDIT_ITEM_DECAY1_CC         (MENU_DEVICE_EDIT_ITEM_MIX2_CC + 1U)
#define MENU_DEVICE_EDIT_ITEM_DECAY2_CC         (MENU_DEVICE_EDIT_ITEM_DECAY1_CC + 1U)
#define MENU_DEVICE_EDIT_ITEM_INIT_DEVICE       (MENU_DEVICE_EDIT_ITEM_DECAY2_CC + 1U)

/* DEVICE_EDIT page-family surface.
 * These declarations cover both simple field formatting and the denser CC-row
 * editing helpers used only by the device editor. */
/* The CC helpers intentionally expose mutable MidiCC_t pointers because the
 * value-edit module changes sub-fields in place while this page just formats. */
const char *Display_GetMenuDeviceEditLabel(uint8_t item_index,
					   char *buffer,
					   size_t buffer_size);
MidiCC_t *Display_GetDeviceCcForMenuItem(RuntimeConfigDevice_t *device, uint8_t item_index);
PresetCCSlot_t *Display_GetDeviceAutoCcForMenuItem(RuntimeConfigDevice_t *device, uint8_t item_index);
uint8_t Display_GetMenuDeviceEditFocusFieldCount(uint8_t item_index);
uint8_t Display_MenuDeviceCcRowIsSelected(void);
uint8_t Display_MenuDeviceAutoCcRowIsSelected(void);
MidiCC_t *Display_GetSelectedDeviceCc(RuntimeConfigDevice_t *device);
PresetCCSlot_t *Display_GetSelectedDeviceAutoCc(RuntimeConfigDevice_t *device);
void Display_FormatDeviceEditValue(uint8_t item_index, char *buffer, size_t buffer_size);
void Display_DrawMenuDeviceEdit(void);
void Display_DrawMenuDeviceEditItem(uint8_t item_index);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MENU_PAGE_DEVICE_EDIT_H */
