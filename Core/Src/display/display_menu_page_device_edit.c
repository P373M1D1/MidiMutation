#include <stdio.h>
#include <string.h>

#include "display_functions.h"
#include "display/display_layout.h"
#include "display/display_internal.h"
#include "display/display_menu_page_device_edit.h"
#include "display/display_menu_page_devices.h"
#include "display/display_menu_redraw_utils.h"
#include "display/display_menu_row_render.h"
#include "display/display_row_compose.h"
#include "runtime_config.h"
#include "st7796.h"

/* DEVICE_EDIT page renderer.
 *
 * Device editing has denser row layouts than a normal label/value menu because
 * CC rows expose multiple fields on the same line. Those custom row layouts and
 * value labels are kept here so the generic row renderer can stay simple. */

#define MENU_DEVICE_INIT_TEXT "INIT DEVICE"
#define MENU_ITEM_X 24U

static uint8_t Display_DeviceEditItemHasCcValue(uint8_t item_index)
{
    return (item_index >= 3U && item_index <= 5U) ? 1U : 0U;
}

static void Display_FormatMenuNumericFieldLocal(char *buffer,
                                                size_t buffer_size,
                                                uint8_t value,
                                                uint8_t digits)
{
    char field_text[5];

    if (!buffer || buffer_size == 0U || digits == 0U || digits >= sizeof(field_text))
        return;

    (void)snprintf(field_text, sizeof(field_text), "%*u", digits, value);
    (void)snprintf(buffer, buffer_size, "%s", field_text);
}

static uint16_t Display_GetMenuRightAlignedValueXLocal(const char *value)
{
    size_t value_length = value ? strlen(value) : 0U;

    return (uint16_t)(ST7796_WIDTH - MENU_ITEM_X - ((uint16_t)value_length * MAIN_INFO_FONT.width));
}

static void Display_DrawMenuDeviceCcEditRowByIndex(uint8_t row_index,
                                                   uint8_t item_index,
                                                   const char *label,
                                                   const MidiCC_t *cc)
{
    char cc_number_text[4];
    char value_text[4];
    uint16_t value_x;
    uint16_t row_y;
    uint8_t has_value_field = Display_DeviceEditItemHasCcValue(item_index);

    if (row_index >= MENU_VISIBLE_ROW_COUNT || !cc)
        return;

    row_y = Display_GetMenuRowYByIndex(row_index);
    Display_MenuRowComposeSetTargetY(row_y);
    Display_MenuRowComposeClear(DISPLAY_BG_COLOUR);

    if (label && label[0] != '\0')
    {
        Display_MenuRowComposeTextSegment32(MENU_ITEM_X,
                                            label,
                                            MAIN_INFO_TEXT_COLOUR,
                                            DISPLAY_BG_COLOUR);
    }

    Display_FormatMenuOptionalField(cc_number_text,
                                    sizeof(cc_number_text),
                                    cc->cc,
                                    PRESET_CC_NUMBER_UNUSED,
                                    3U,
                                    0U);
    if (has_value_field)
    {
        Display_FormatMenuNumericFieldLocal(value_text,
                                            sizeof(value_text),
                                            cc->value,
                                            3U);
    }

    /* Build the dense CC edit row from a fixed template so all sub-fields stay
     * column-aligned while only the active segment receives highlight colours. */
    value_x = Display_GetMenuRightAlignedValueXLocal(has_value_field ? "CC:--- VAL:---" : "CC:---");
    value_x = Display_MenuRowComposeValueSegment32(value_x, "CC:", 0U);
    value_x = Display_MenuRowComposeValueSegment32(value_x,
                                                   cc_number_text,
                                                   (display_state.menu_device_cc_field_index == 0U) ? 1U : 0U);

    if (has_value_field)
    {
        value_x = Display_MenuRowComposeValueSegment32(value_x, " VAL:", 0U);
        (void)Display_MenuRowComposeValueSegment32(value_x,
                                                   value_text,
                                                   (display_state.menu_device_cc_field_index == 1U) ? 1U : 0U);
    }

    Display_MenuRowComposeBlit(row_y);
}

const char *Display_GetMenuDeviceEditLabel(uint8_t item_index,
                                           char *buffer,
                                           size_t buffer_size)
{
    static const char * const menu_device_edit_labels[MENU_DEVICE_EDIT_ITEM_COUNT] = {
        "Name",
        "Max Preset",
        "Channel",
        "Active CC",
        "Bypass CC",
        "Tap CC",
        "Volume1 CC",
        "Volume2 CC",
        "Mix1 CC",
        "Mix2 CC",
        "Decay1 CC",
        "Decay2 CC",
        MENU_DEVICE_INIT_TEXT,
    };

    if (!buffer || buffer_size == 0U || item_index >= MENU_DEVICE_EDIT_ITEM_COUNT)
        return "";

    if (item_index == 0U)
    {
        /* The first row label includes the device number so the editor still
         * has context when opened from a scrolled DEVICES list. */
        Display_FormatMenuDeviceLabel(display_state.menu_active_device_index, buffer, buffer_size);
        return buffer;
    }

    (void)snprintf(buffer, buffer_size, "%s", menu_device_edit_labels[item_index]);
    return buffer;
}

MidiCC_t *Display_GetDeviceCcForMenuItem(RuntimeConfigDevice_t *device, uint8_t item_index)
{
    if (!device)
        return NULL;

    switch (item_index)
    {
    case 3U:
        return &device->active;
    case 4U:
        return &device->bypass;
    case 5U:
        return &device->tap_tempo;
    case 6U:
        return &device->volume1;
    case 7U:
        return &device->volume2;
    case 8U:
        return &device->mix1;
    case 9U:
        return &device->mix2;
    case 10U:
        return &device->decay1;
    case 11U:
        return &device->decay2;
    default:
        return NULL;
    }
}

uint8_t Display_MenuDeviceCcRowIsSelected(void)
{
    return ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_DEVICE_EDIT
         && display_state.menu_device_edit_selection_index >= 3U
            && display_state.menu_device_edit_selection_index <= 11U) ? 1U : 0U;
}

MidiCC_t *Display_GetSelectedDeviceCc(RuntimeConfigDevice_t *device)
{
    return Display_GetDeviceCcForMenuItem(device,
                                          display_state.menu_device_edit_selection_index);
}

static void Display_FormatDeviceCcValue(const MidiCC_t *cc, char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0U)
        return;

    if (!cc || cc->cc == PRESET_CC_NUMBER_UNUSED)
    {
        (void)snprintf(buffer, buffer_size, "CC:--- VAL:---");
        return;
    }

    (void)snprintf(buffer, buffer_size, "CC:%3u VAL:%3u", cc->cc, cc->value);
}

static void Display_FormatDeviceCcNumberOnlyValue(const MidiCC_t *cc,
                                                  char *buffer,
                                                  size_t buffer_size)
{
    if (!buffer || buffer_size == 0U)
        return;

    if (!cc || cc->cc == PRESET_CC_NUMBER_UNUSED)
    {
        (void)snprintf(buffer, buffer_size, "CC:---");
        return;
    }

    (void)snprintf(buffer, buffer_size, "CC:%3u", cc->cc);
}

void Display_FormatDeviceEditValue(uint8_t item_index, char *buffer, size_t buffer_size)
{
    const RuntimeConfigDevice_t *device = RuntimeConfig_GetDevice(display_state.menu_active_device_index);

    if (!buffer || buffer_size == 0U)
        return;

    buffer[0] = '\0';

    if (!device)
        return;

    switch (item_index)
    {
    case 0U:
        if (device->name[0] != '\0')
            (void)snprintf(buffer, buffer_size, "%s", device->name);
        else
            (void)snprintf(buffer, buffer_size, "Ch %u", device->channel);
        break;
    case 1U:
        (void)snprintf(buffer, buffer_size, "%u", device->max_preset);
        break;
    case 2U:
        (void)snprintf(buffer, buffer_size, "%u", device->channel);
        break;
    case 3U:
        Display_FormatDeviceCcValue(&device->active, buffer, buffer_size);
        break;
    case 4U:
        Display_FormatDeviceCcValue(&device->bypass, buffer, buffer_size);
        break;
    case 5U:
        Display_FormatDeviceCcValue(&device->tap_tempo, buffer, buffer_size);
        break;
    case 6U:
        Display_FormatDeviceCcNumberOnlyValue(&device->volume1, buffer, buffer_size);
        break;
    case 7U:
        Display_FormatDeviceCcNumberOnlyValue(&device->volume2, buffer, buffer_size);
        break;
    case 8U:
        Display_FormatDeviceCcNumberOnlyValue(&device->mix1, buffer, buffer_size);
        break;
    case 9U:
        Display_FormatDeviceCcNumberOnlyValue(&device->mix2, buffer, buffer_size);
        break;
    case 10U:
        Display_FormatDeviceCcNumberOnlyValue(&device->decay1, buffer, buffer_size);
        break;
    case 11U:
        Display_FormatDeviceCcNumberOnlyValue(&device->decay2, buffer, buffer_size);
        break;
    case 12U:
        buffer[0] = '\0';
        break;
    default:
        buffer[0] = '\0';
        break;
    }
}

void Display_DrawMenuDeviceEdit(void)
{
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MENU_DEVICE_EDIT_ITEM_COUNT,
                                                                   display_state.menu_device_edit_selection_index);

    for (uint8_t row_index = 0U; row_index < MENU_VISIBLE_ROW_COUNT; ++row_index)
    {
        uint8_t item_index = (uint8_t)(first_visible_index + row_index);

        if (item_index >= MENU_DEVICE_EDIT_ITEM_COUNT)
        {
            Display_ClearStandardMenuRow(row_index);
            continue;
        }

        Display_DrawMenuDeviceEditItem(item_index);
    }
}

void Display_DrawMenuDeviceEditItem(uint8_t item_index)
{
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MENU_DEVICE_EDIT_ITEM_COUNT,
                                                                   display_state.menu_device_edit_selection_index);
    uint8_t row_index;
    uint8_t row_selected;
    char label_text[16];
    char value_text[20];
    const RuntimeConfigDevice_t *device = RuntimeConfig_GetDevice(display_state.menu_active_device_index);

    if (item_index < first_visible_index || item_index >= (uint8_t)(first_visible_index + MENU_VISIBLE_ROW_COUNT))
        return;

    row_index = (uint8_t)(item_index - first_visible_index);
    row_selected = (item_index == display_state.menu_device_edit_selection_index) ? 1U : 0U;
    if (row_selected && item_index == 0U
        && (DisplayMenuTextField_t)display_state.menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_DEVICE_NAME)
    {
        row_selected = 0U;
    }
    if (row_selected && item_index >= 3U && item_index <= 11U)
        /* CC edit rows draw their own per-field highlights, so suppress the
         * normal whole-row selection background in the generic row renderer. */
        row_selected = 0U;

    if (item_index == 0U
        && (DisplayMenuTextField_t)display_state.menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_DEVICE_NAME)
    {
        Display_DrawMenuTextEditRowByIndex(row_index,
                                           Display_GetMenuDeviceEditLabel(item_index, label_text, sizeof(label_text)),
                                           device ? device->name : "",
                                           RUNTIME_CONFIG_DEVICE_NAME_LENGTH);
        return;
    }

    if (item_index >= 3U
        && item_index <= 11U
        && item_index == display_state.menu_device_edit_selection_index)
    {
        Display_DrawMenuDeviceCcEditRowByIndex(row_index,
                                               item_index,
                                               Display_GetMenuDeviceEditLabel(item_index, label_text, sizeof(label_text)),
                                               Display_GetSelectedDeviceCc((RuntimeConfigDevice_t *)device));
        return;
    }

    if (item_index == 12U)
    {
        uint8_t init_selected = (item_index == display_state.menu_device_edit_selection_index) ? 1U : 0U;

        Display_DrawMenuCenteredBadgeRowByIndex(row_index,
                                                Display_GetMenuDeviceEditLabel(item_index, label_text, sizeof(label_text)),
                                                init_selected ? BLACK : RED,
                                                init_selected ? RED : DISPLAY_BG_COLOUR);
        return;
    }

    Display_FormatDeviceEditValue(item_index, value_text, sizeof(value_text));
    Display_DrawMenuRowByIndex(row_index,
                               Display_GetMenuDeviceEditLabel(item_index, label_text, sizeof(label_text)),
                               value_text,
                               row_selected);
}
