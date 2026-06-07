#include <stdio.h>
#include <string.h>

#include "display/display_internal.h"
#include "display/display_layout.h"
#include "display/display_menu_row_render.h"
#include "display/display_row_compose.h"

/* Standard menu-row surface renderer.
 *
 * This module owns the four visible row Y positions, left/right alignment, and
 * selected-row colour handling used by the common menu shell. Page modules hand
 * it labels/values; it decides how one row is actually painted on screen. */

#define MENU_ITEM_X 24U
#define MENU_ITEM_W (ST7796_WIDTH - (MENU_ITEM_X * 2U))

static const uint16_t menu_row_y[MENU_VISIBLE_ROW_COUNT] = {85U, 127U, 169U, 211U};

static uint16_t Display_GetMenuRowBackgroundColour(uint8_t selected)
{
    return selected ? MAIN_INFO_EDIT_CURSOR_BG_COLOUR : DISPLAY_BG_COLOUR;
}

static uint16_t Display_GetMenuRowForegroundColour(uint8_t selected)
{
    return selected ? MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR : MAIN_INFO_TEXT_COLOUR;
}

static uint16_t Display_GetMenuRightAlignedValueX(const char *value)
{
    size_t value_length = value ? strlen(value) : 0U;

    return (uint16_t)(ST7796_WIDTH - MENU_ITEM_X - ((uint16_t)value_length * MAIN_INFO_FONT.width));
}

static void Display_DrawMenuRowComposed(uint16_t row_y,
                                        const char *label,
                                        const char *value,
                                        uint8_t selected)
{
    uint16_t value_x = MENU_ITEM_X;
    uint8_t highlight_value = (selected && value && value[0] != '\0') ? 1U : 0U;
    uint8_t highlight_label = (selected && !highlight_value) ? 1U : 0U;

    /* Standard rows highlight the value when one exists; rows without a value
     * badge instead highlight the label so selection is still visible. */
    Display_MenuRowComposeSetTargetY(row_y);
    Display_MenuRowComposeClear(DISPLAY_BG_COLOUR);

    if (label && label[0] != '\0')
    {
        Display_MenuRowComposeTextSegment32(MENU_ITEM_X,
                                            label,
                                            Display_GetMenuRowForegroundColour(highlight_label),
                                            Display_GetMenuRowBackgroundColour(highlight_label));
    }

    if (value && value[0] != '\0')
    {
        value_x = Display_GetMenuRightAlignedValueX(value);
        Display_MenuRowComposeTextSegment32(value_x,
                                            value,
                                            Display_GetMenuRowForegroundColour(highlight_value),
                                            Display_GetMenuRowBackgroundColour(highlight_value));
    }

    Display_MenuRowComposeBlit(row_y);
}

static void Display_ComposeCenteredBadgeRow(const char *text,
                                            uint16_t foreground,
                                            uint16_t background)
{
    char badge_text[32];
    size_t badge_length;
    uint16_t text_x;

    if (!text || text[0] == '\0')
        return;

    Display_MenuRowComposeClear(DISPLAY_BG_COLOUR);

    (void)snprintf(badge_text, sizeof(badge_text), " %s ", text);
    badge_length = strlen(badge_text);

    if ((uint16_t)(badge_length * MAIN_INFO_FONT.width) >= MENU_ITEM_W)
        text_x = MENU_ITEM_X;
    else
        text_x = (uint16_t)(MENU_ITEM_X + ((MENU_ITEM_W - ((uint16_t)badge_length * MAIN_INFO_FONT.width)) / 2U));

    Display_MenuRowComposeTextSegment32(text_x,
                                        badge_text,
                                        foreground,
                                        background);
}

static void Display_DrawMenuCenteredBadgeRow(uint16_t row_y,
                                             const char *text,
                                             uint16_t foreground,
                                             uint16_t background)
{
    if (!text || text[0] == '\0')
        return;

    Display_MenuRowComposeSetTargetY(row_y);
    Display_ComposeCenteredBadgeRow(text, foreground, background);
    Display_MenuRowComposeBlit(row_y);
}

static void Display_DrawMenuTextEditRow(uint16_t row_y,
                                        const char *label,
                                        const char *source,
                                        uint8_t cell_count)
{
    char cells[RUNTIME_CONFIG_BANK_NAME_LENGTH];
    uint16_t value_x;

    if (cell_count == 0U || cell_count > sizeof(cells))
        return;

    /* Text-edit rows are rendered as fixed-width cells so the cursor highlight
     * does not shift when trailing spaces or short names are edited. */
    Display_LoadMenuTextCells(source, cell_count, cells);
    value_x = (uint16_t)(ST7796_WIDTH - MENU_ITEM_X - ((uint16_t)cell_count * MAIN_INFO_FONT.width));

    Display_MenuRowComposeSetTargetY(row_y);
    Display_MenuRowComposeClear(DISPLAY_BG_COLOUR);

    if (label && label[0] != '\0')
    {
        Display_MenuRowComposeTextSegment32(MENU_ITEM_X,
                                            label,
                                            MAIN_INFO_TEXT_COLOUR,
                                            DISPLAY_BG_COLOUR);
    }

    for (uint8_t index = 0U; index < cell_count; ++index)
    {
        char cell_text[2] = { cells[index], '\0' };
        uint16_t char_x = (uint16_t)(value_x + (index * MAIN_INFO_FONT.width));
        uint8_t highlighted = (index == display_state.menu_text_edit_cursor_index) ? 1U : 0U;
        uint16_t foreground = highlighted ? MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR : MAIN_INFO_TEXT_COLOUR;
        uint16_t background = highlighted ? MAIN_INFO_EDIT_CURSOR_BG_COLOUR : DISPLAY_BG_COLOUR;

        Display_MenuRowComposeTextSegment32(char_x,
                                            cell_text,
                                            foreground,
                                            background);
    }

    Display_MenuRowComposeBlit(row_y);
}

void Display_DrawMenuRowByIndex(uint8_t row_index,
                                const char *label,
                                const char *value,
                                uint8_t selected)
{
    if (row_index >= MENU_VISIBLE_ROW_COUNT)
        return;

    Display_DrawMenuRowComposed(menu_row_y[row_index], label, value, selected);
}

uint16_t Display_GetMenuRowYByIndex(uint8_t row_index)
{
    if (row_index >= MENU_VISIBLE_ROW_COUNT)
        return 0U;

    return menu_row_y[row_index];
}

void Display_DrawMenuRowValueOnlyByIndex(uint8_t row_index,
                                         const char *label,
                                         const char *value,
                                         uint8_t selected)
{
    if (row_index >= MENU_VISIBLE_ROW_COUNT)
        return;

    Display_DrawMenuRowComposed(menu_row_y[row_index], label, value, selected);
}

void Display_DrawMenuTextEditRowByIndex(uint8_t row_index,
                                        const char *label,
                                        const char *text,
                                        uint8_t cell_count)
{
    if (row_index >= MENU_VISIBLE_ROW_COUNT)
        return;

    Display_DrawMenuTextEditRow(menu_row_y[row_index], label, text, cell_count);
}

void Display_DrawMenuCenteredBadgeRowByIndex(uint8_t row_index,
                                             const char *text,
                                             uint16_t text_colour,
                                             uint16_t badge_colour)
{
    if (row_index >= MENU_VISIBLE_ROW_COUNT)
        return;

    Display_DrawMenuCenteredBadgeRow(menu_row_y[row_index], text, text_colour, badge_colour);
}

void Display_ClearStandardMenuRow(uint8_t row_index)
{
    uint16_t row_y;

    if (row_index >= MENU_VISIBLE_ROW_COUNT)
        return;

    row_y = menu_row_y[row_index];
    Display_MenuRowComposeSetTargetY(row_y);
    Display_MenuRowComposeClear(DISPLAY_BG_COLOUR);
    Display_MenuRowComposeBlit(row_y);
}