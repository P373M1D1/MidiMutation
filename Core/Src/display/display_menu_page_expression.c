#include <stdio.h>
#include <string.h>

#include "display_functions.h"
#include "display/display_internal.h"
#include "display/display_layout.h"
#include "display/display_menu_page_expression.h"
#include "display/display_menu_redraw_utils.h"
#include "display/display_menu_row_render.h"
#include "display/display_row_compose.h"
#include "runtime_config.h"
#include "st7796.h"

#define MENU_ITEM_X 24U

static void Display_FormatExpressionSlotLabel(uint8_t slot_index, char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0U)
        return;

    if (slot_index >= RUNTIME_CONFIG_EXPRESSION_PEDAL_CC_SLOT_COUNT)
    {
        buffer[0] = '\0';
        return;
    }

    (void)snprintf(buffer, buffer_size, "CC%u", (unsigned)(slot_index + 1U));
}

static uint16_t Display_GetMenuRightAlignedExpressionValueX(const char *value)
{
    size_t value_length = value ? strlen(value) : 0U;

    return (uint16_t)(ST7796_WIDTH - MENU_ITEM_X - ((uint16_t)value_length * MAIN_INFO_FONT.width));
}

static void Display_DrawExpressionCcEditRowByIndex(uint8_t row_index,
                                                   uint8_t slot_index,
                                                   const RuntimeConfigExpressionPedalCcSlot_t *slot)
{
    char cc_text[4];
    char heel_text[4];
    char toe_text[4];
    char slot_label[5];
    char slot_number_text[2];
    uint16_t value_x;

    if (row_index >= MENU_VISIBLE_ROW_COUNT || !slot || slot_index >= RUNTIME_CONFIG_EXPRESSION_PEDAL_CC_SLOT_COUNT)
        return;

    Display_FormatExpressionSlotLabel(slot_index, slot_label, sizeof(slot_label));
    (void)snprintf(slot_number_text, sizeof(slot_number_text), "%u", (unsigned)(slot_index + 1U));

    Display_MenuRowComposeClear(DISPLAY_BG_COLOUR);
    Display_MenuRowComposeTextSegment32(MENU_ITEM_X,
                                        slot_label,
                                        MAIN_INFO_TEXT_COLOUR,
                                        DISPLAY_BG_COLOUR);

    if (slot->cc == PRESET_CC_NUMBER_UNUSED)
        (void)snprintf(cc_text, sizeof(cc_text), "---");
    else
        (void)snprintf(cc_text, sizeof(cc_text), "%3u", slot->cc);

    (void)snprintf(heel_text, sizeof(heel_text), "%3u", slot->heel_value);
    (void)snprintf(toe_text, sizeof(toe_text), "%3u", slot->toe_value);

    value_x = Display_GetMenuRightAlignedExpressionValueX("#8 CC:--- H:--- T:---");
    value_x = Display_MenuRowComposeValueSegment32(value_x, "#", 0U);
    value_x = Display_MenuRowComposeValueSegment32(value_x,
                                                   slot_number_text,
                                                   0U);
    value_x = Display_MenuRowComposeValueSegment32(value_x, " CC:", 0U);
    value_x = Display_MenuRowComposeValueSegment32(value_x,
                                                   cc_text,
                                                   (display_state.menu_expression_field_index == 0U) ? 1U : 0U);
    value_x = Display_MenuRowComposeValueSegment32(value_x, " H:", 0U);
    value_x = Display_MenuRowComposeValueSegment32(value_x,
                                                   heel_text,
                                                   (display_state.menu_expression_field_index == 1U) ? 1U : 0U);
    value_x = Display_MenuRowComposeValueSegment32(value_x, " T:", 0U);
    (void)Display_MenuRowComposeValueSegment32(value_x,
                                               toe_text,
                                               (display_state.menu_expression_field_index == 2U) ? 1U : 0U);

    Display_MenuRowComposeBlit(Display_GetMenuRowYByIndex(row_index));
}

void Display_FormatExpressionValue(uint8_t item_index, char *buffer, size_t buffer_size)
{
    const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();

    if (!buffer || buffer_size == 0U || !global)
        return;

    if (item_index == 0U)
    {
        (void)snprintf(buffer,
                       buffer_size,
                       "%s",
                       (global->expression_pedal_mode == RUNTIME_CONFIG_EXPRESSION_PEDAL_MODE_TIMEBEND)
                           ? "Active"
                           : "Inactive");
        return;
    }

    if (item_index >= MENU_EXPRESSION_ITEM_COUNT)
    {
        buffer[0] = '\0';
        return;
    }

    {
        const RuntimeConfigExpressionPedalCcSlot_t *slot = &global->expression_pedal_cc_slots[item_index - 1U];

        if (slot->cc == PRESET_CC_NUMBER_UNUSED)
            (void)snprintf(buffer, buffer_size, "CC:--- H:%3u T:%3u", slot->heel_value, slot->toe_value);
        else
            (void)snprintf(buffer, buffer_size, "CC:%3u H:%3u T:%3u", slot->cc, slot->heel_value, slot->toe_value);
    }
}

void Display_DrawMenuExpression(void)
{
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MENU_EXPRESSION_ITEM_COUNT,
                                                                   display_state.menu_expression_selection_index);

    for (uint8_t row_index = 0U; row_index < MENU_VISIBLE_ROW_COUNT; ++row_index)
    {
        uint8_t item_index = (uint8_t)(first_visible_index + row_index);

        if (item_index < MENU_EXPRESSION_ITEM_COUNT)
            Display_DrawMenuExpressionItem(item_index);
        else
            Display_ClearStandardMenuRow(row_index);
    }
}

void Display_DrawMenuExpressionItem(uint8_t item_index)
{
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MENU_EXPRESSION_ITEM_COUNT,
                                                                   display_state.menu_expression_selection_index);
    uint8_t row_index;
    uint8_t row_selected;
    char value_text[24];
    char label_text[9];
    const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();

    if (item_index < first_visible_index || item_index >= (uint8_t)(first_visible_index + MENU_VISIBLE_ROW_COUNT))
        return;

    row_index = (uint8_t)(item_index - first_visible_index);
    row_selected = (item_index == display_state.menu_expression_selection_index) ? 1U : 0U;

    if (item_index >= 1U && row_selected)
    {
        if (global)
            Display_DrawExpressionCcEditRowByIndex(row_index,
                                                   (uint8_t)(item_index - 1U),
                                                   &global->expression_pedal_cc_slots[item_index - 1U]);
        else
            Display_ClearStandardMenuRow(row_index);

        return;
    }

    if (item_index == 0U)
        (void)snprintf(label_text, sizeof(label_text), "Timebend");
    else
        Display_FormatExpressionSlotLabel((uint8_t)(item_index - 1U), label_text, sizeof(label_text));

    Display_FormatExpressionValue(item_index, value_text, sizeof(value_text));
    Display_DrawMenuRowByIndex(row_index,
                               label_text,
                               value_text,
                               row_selected);
}
