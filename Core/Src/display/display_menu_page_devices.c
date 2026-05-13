#include <stdio.h>

#include "display_functions.h"
#include "display/display_internal.h"
#include "display/display_menu_page_devices.h"
#include "display/display_menu_redraw_utils.h"
#include "display/display_menu_row_render.h"
#include "midi_devices.h"
#include "runtime_config.h"

/* DEVICES list-page renderer.
 *
 * This module formats the summary rows for the device list page. It is the
 * display-side counterpart to the device table/config model and keeps the
 * generic menu row renderer free of device-specific wording. */

#define MENU_DEVICE_LABEL_PREFIX "Device "

void Display_FormatMenuDeviceLabel(uint8_t device_index, char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0U)
        return;

    (void)snprintf(buffer, buffer_size, MENU_DEVICE_LABEL_PREFIX "%u", (uint8_t)(device_index + 1U));
}

void Display_FormatMenuDeviceListValue(const RuntimeConfigDevice_t *device,
                                       char *buffer,
                                       size_t buffer_size)
{
    if (!buffer || buffer_size == 0U)
        return;

    if (device && device->name[0] != '\0')
        (void)snprintf(buffer, buffer_size, "%s", device->name);
    else if (device)
        /* Unnamed devices still need a stable list entry; channel is the least
         * surprising fallback because it is always configured. */
        (void)snprintf(buffer, buffer_size, "CH %u", device->channel);
    else
        buffer[0] = '\0';
}

void Display_DrawMenuDeviceWindowRow(uint8_t row_index, uint8_t device_index)
{
    char label_text[14];
    char value_text[12];
    const RuntimeConfigDevice_t *device;

    if (row_index >= MENU_VISIBLE_ROW_COUNT)
        return;

    if (device_index >= MIDI_DEVICE_COUNT)
    {
        Display_ClearStandardMenuRow(row_index);
        return;
    }

    device = RuntimeConfig_GetDevice(device_index);
    Display_FormatMenuDeviceLabel(device_index, label_text, sizeof(label_text));
    Display_FormatMenuDeviceListValue(device, value_text, sizeof(value_text));
    Display_DrawMenuRowByIndex(row_index,
                               label_text,
                               value_text,
                               (device_index == display_state.menu_device_selection_index) ? 1U : 0U);
}

void Display_DrawMenuDeviceItem(uint8_t device_index)
{
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MIDI_DEVICE_COUNT,
                                                                   display_state.menu_device_selection_index);
    uint8_t row_index;

    if (device_index < first_visible_index || device_index >= (uint8_t)(first_visible_index + MENU_VISIBLE_ROW_COUNT))
        return;

    row_index = (uint8_t)(device_index - first_visible_index);
    Display_DrawMenuDeviceWindowRow(row_index, device_index);
}

void Display_DrawMenuDevices(void)
{
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MIDI_DEVICE_COUNT,
                                                                   display_state.menu_device_selection_index);

    for (uint8_t row_index = 0U; row_index < MENU_VISIBLE_ROW_COUNT; ++row_index)
    {
        uint8_t device_index = (uint8_t)(first_visible_index + row_index);

        if (device_index >= MIDI_DEVICE_COUNT)
        {
            /* Clear spare rows when the window reaches the end of the device
             * list so old text from previous pages is not left behind. */
            Display_ClearStandardMenuRow(row_index);
            continue;
        }

        Display_DrawMenuDeviceWindowRow(row_index, device_index);
    }
}
