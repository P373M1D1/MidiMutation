#include <stdio.h>

#include "display_functions.h"
#include "display/display_internal.h"
#include "display/display_menu_page_function_button.h"
#include "display/display_menu_page_function_button_compare.h"
#include "display/display_menu_row_render.h"
#include "display/display_row_compose.h"
#include "display/display_layout.h"
#include "display/display_theme.h"
#include "runtime_config.h"
#include "st7796.h"

/* FUNCTION_BUTTON compare-table renderer.
 *
 * Active/inactive program and CC messages use a wider, denser layout than the
 * standard menu rows, so their window math and row drawing live here. If the
 * function-button editor ever gains more message columns, this is the file that
 * defines how those rows are paged and painted. */

#define MENU_ITEM_X 24U

static uint8_t Display_GetVisibleWindowStart(uint8_t item_count,
                                             uint8_t selected_index,
                                             uint8_t visible_count)
{
    if (visible_count == 0U || item_count <= visible_count)
        return 0U;

    if (selected_index >= item_count)
        selected_index = (uint8_t)(item_count - 1U);

    if (selected_index < visible_count)
        return 0U;

    /* Once the selection scrolls past the visible window, keep it pinned on
     * the last visible row so the page advances one logical item at a time. */
    return (uint8_t)(selected_index - (visible_count - 1U));
}

uint8_t Display_GetFunctionButtonLayoutSignature(uint8_t selection_index, uint8_t *window_start)
{
    uint8_t message_selection_index;

    if (window_start)
        *window_start = 0U;

    if (selection_index < MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX)
        return 0U;

    /* Message rows are split into a program section followed by a CC section,
     * and each section occupies a different fixed screen layout. */
    message_selection_index = (uint8_t)(selection_index - MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX);

    if (message_selection_index < RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT)
    {
        if (message_selection_index == 0U)
            return 1U;

        if (message_selection_index == (uint8_t)(RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT - 1U))
            return 2U;

        if (window_start)
            *window_start = (uint8_t)(message_selection_index - 1U);

        return 3U;
    }

    message_selection_index = (uint8_t)(message_selection_index - RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT);

    if (message_selection_index == 0U)
        return 4U;

    if (window_start)
        *window_start = (uint8_t)(message_selection_index - 1U);

    return 5U;
}

uint8_t Display_GetFunctionButtonSelectionRow(uint8_t selection_index, uint8_t *row_index)
{
    uint8_t window_start = 0U;
    uint8_t signature;
    uint8_t message_selection_index;

    if (!row_index)
        return 0U;

    signature = Display_GetFunctionButtonLayoutSignature(selection_index, &window_start);
    if (signature == 0U)
    {
        if (selection_index >= MENU_VISIBLE_ROW_COUNT)
            return 0U;

        *row_index = selection_index;
        return 1U;
    }

    message_selection_index = (uint8_t)(selection_index - MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX);

    switch (signature)
    {
    case 1U:
    case 2U:
        *row_index = (uint8_t)(MENU_VISIBLE_ROW_COUNT - 1U);
        return 1U;

    case 3U:
        if (message_selection_index < window_start
         || message_selection_index >= (uint8_t)(window_start + MENU_VISIBLE_ROW_COUNT - 1U))
            return 0U;

        *row_index = (uint8_t)(1U + message_selection_index - window_start);
        return 1U;

    case 4U:
        *row_index = (uint8_t)(MENU_VISIBLE_ROW_COUNT - 1U);
        return 1U;

    case 5U:
    {
        uint8_t cc_selection_index = (uint8_t)(message_selection_index - RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT);

        if (cc_selection_index < window_start
         || cc_selection_index >= (uint8_t)(window_start + MENU_VISIBLE_ROW_COUNT - 2U))
            return 0U;

        *row_index = (uint8_t)(2U + cc_selection_index - window_start);
        return 1U;
    }

    default:
        return 0U;
    }
}

uint8_t Display_GetFunctionButtonMessageSelectionIndex(void)
{
    if (display_state.menu_function_button_selection_index < MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX)
        return 0U;

    return (uint8_t)(display_state.menu_function_button_selection_index - MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX);
}

static void Display_DrawMenuFunctionButtonCompareHeaderRow(uint16_t row_y)
{
    Display_MenuRowComposeSetTargetY(row_y);
    Display_MenuRowComposeClear(DISPLAY_BG_COLOUR);
    Display_MenuRowComposeTextSegment32(MENU_ITEM_X,
                                        "   Active        Inactive",
                                        MAIN_INFO_TEXT_COLOUR,
                                        DISPLAY_BG_COLOUR);
    Display_MenuRowComposeBlit(row_y);
}

static void Display_DrawMenuFunctionButtonCcHeaderRow(uint16_t row_y)
{
    Display_MenuRowComposeSetTargetY(row_y);
    Display_MenuRowComposeClear(DISPLAY_BG_COLOUR);
    Display_MenuRowComposeTextSegment32(MENU_ITEM_X,
                                        "  Ch Cc  Val    Ch Cc  Val",
                                        MAIN_INFO_TEXT_COLOUR,
                                        DISPLAY_BG_COLOUR);
    Display_MenuRowComposeBlit(row_y);
}

void Display_DrawMenuFunctionButtonProgramCompareEditRowCore(uint16_t row_y,
                                                             uint8_t program_index,
                                                             uint8_t selected,
                                                             uint8_t clear_row,
                                                             uint8_t redraw_number)
{
    const RuntimeConfigFunctionButton_t *function_button = Presets_GetFunctionButton(display_state.menu_active_preset_index);
    char active_channel_text[4];
    char active_program_text[4];
    char inactive_channel_text[4];
    char inactive_program_text[4];
    uint16_t active_channel_x = (uint16_t)(MENU_ITEM_X + (5U * MAIN_INFO_FONT.width));
    uint16_t active_program_x = (uint16_t)(MENU_ITEM_X + (11U * MAIN_INFO_FONT.width));
    uint16_t inactive_channel_x = (uint16_t)(MENU_ITEM_X + (19U * MAIN_INFO_FONT.width));
    uint16_t inactive_program_x = (uint16_t)(MENU_ITEM_X + (25U * MAIN_INFO_FONT.width));
    char row_number_text[4];

    (void)clear_row;
    (void)redraw_number;

    if (!function_button || program_index >= RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT)
        return;

    Display_MenuRowComposeSetTargetY(row_y);
    Display_MenuRowComposeClear(DISPLAY_BG_COLOUR);

    Display_FormatMenuOptionalField(active_channel_text,
                                    sizeof(active_channel_text),
                                    function_button->active_programs[program_index].channel,
                                    PRESET_CC_CHANNEL_UNUSED,
                                    2U,
                                    0U);
    Display_FormatMenuOptionalField(active_program_text,
                                    sizeof(active_program_text),
                                    function_button->active_programs[program_index].program,
                                    PRESET_PROGRAM_NONE,
                                    3U,
                                    0U);
    Display_FormatMenuOptionalField(inactive_channel_text,
                                    sizeof(inactive_channel_text),
                                    function_button->inactive_programs[program_index].channel,
                                    PRESET_CC_CHANNEL_UNUSED,
                                    2U,
                                    0U);
    Display_FormatMenuOptionalField(inactive_program_text,
                                    sizeof(inactive_program_text),
                                    function_button->inactive_programs[program_index].program,
                                    PRESET_PROGRAM_NONE,
                                    3U,
                                    0U);

    (void)snprintf(row_number_text, sizeof(row_number_text), "%u", (uint8_t)(program_index + 1U));

    Display_MenuRowComposeTextSegment32(MENU_ITEM_X,
                                        row_number_text,
                                        MAIN_INFO_TEXT_COLOUR,
                                        DISPLAY_BG_COLOUR);
    Display_MenuRowComposeTextSegment32((uint16_t)(MENU_ITEM_X + (1U * MAIN_INFO_FONT.width)),
                                        " Ch:",
                                        MAIN_INFO_TEXT_COLOUR,
                                        DISPLAY_BG_COLOUR);
    Display_MenuRowComposeTextSegment32((uint16_t)(MENU_ITEM_X + (7U * MAIN_INFO_FONT.width)),
                                        " Pg:",
                                        MAIN_INFO_TEXT_COLOUR,
                                        DISPLAY_BG_COLOUR);
    Display_MenuRowComposeTextSegment32((uint16_t)(MENU_ITEM_X + (14U * MAIN_INFO_FONT.width)),
                                        "  Ch:",
                                        MAIN_INFO_TEXT_COLOUR,
                                        DISPLAY_BG_COLOUR);
    Display_MenuRowComposeTextSegment32((uint16_t)(MENU_ITEM_X + (21U * MAIN_INFO_FONT.width)),
                                        " Pg:",
                                        MAIN_INFO_TEXT_COLOUR,
                                        DISPLAY_BG_COLOUR);

    Display_MenuRowComposeTextSegment32(active_channel_x,
                                        active_channel_text,
                                        (selected && display_state.menu_function_button_message_field_index == 0U)
                                            ? MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR
                                            : MAIN_INFO_TEXT_COLOUR,
                                        (selected && display_state.menu_function_button_message_field_index == 0U)
                                            ? MAIN_INFO_EDIT_CURSOR_BG_COLOUR
                                            : DISPLAY_BG_COLOUR);
    Display_MenuRowComposeTextSegment32(active_program_x,
                                        active_program_text,
                                        (selected && display_state.menu_function_button_message_field_index == 1U)
                                            ? MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR
                                            : MAIN_INFO_TEXT_COLOUR,
                                        (selected && display_state.menu_function_button_message_field_index == 1U)
                                            ? MAIN_INFO_EDIT_CURSOR_BG_COLOUR
                                            : DISPLAY_BG_COLOUR);
    Display_MenuRowComposeTextSegment32(inactive_channel_x,
                                        inactive_channel_text,
                                        (selected && display_state.menu_function_button_message_field_index == 2U)
                                            ? MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR
                                            : MAIN_INFO_TEXT_COLOUR,
                                        (selected && display_state.menu_function_button_message_field_index == 2U)
                                            ? MAIN_INFO_EDIT_CURSOR_BG_COLOUR
                                            : DISPLAY_BG_COLOUR);
    Display_MenuRowComposeTextSegment32(inactive_program_x,
                                        inactive_program_text,
                                        (selected && display_state.menu_function_button_message_field_index == 3U)
                                            ? MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR
                                            : MAIN_INFO_TEXT_COLOUR,
                                        (selected && display_state.menu_function_button_message_field_index == 3U)
                                            ? MAIN_INFO_EDIT_CURSOR_BG_COLOUR
                                            : DISPLAY_BG_COLOUR);
    Display_MenuRowComposeBlit(row_y);
}

static void Display_DrawMenuFunctionButtonProgramCompareEditRow(uint16_t row_y, uint8_t program_index)
{
    Display_DrawMenuFunctionButtonProgramCompareEditRowCore(row_y, program_index, 1U, 1U, 0U);
}

static void Display_DrawMenuFunctionButtonProgramCompareEditRowWindowUpdate(uint16_t row_y,
                                                                            uint8_t program_index)
{
    Display_DrawMenuFunctionButtonProgramCompareEditRowCore(row_y, program_index, 1U, 0U, 1U);
}

static void Display_DrawMenuFunctionButtonCcCompareEditRowCore(uint16_t row_y,
                                                               uint8_t cc_index,
                                                               uint8_t clear_row)
{
    const RuntimeConfigFunctionButton_t *function_button = Presets_GetFunctionButton(display_state.menu_active_preset_index);
    char row_number_text[3];
    char active_channel_text[4];
    char active_cc_text[4];
    char active_value_text[4];
    char inactive_channel_text[4];
    char inactive_cc_text[4];
    char inactive_value_text[4];
    uint16_t draw_x = MENU_ITEM_X;

    (void)clear_row;

    if (!function_button || cc_index >= RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT)
        return;

    Display_FormatMenuOptionalField(active_channel_text,
                                    sizeof(active_channel_text),
                                    function_button->active_cc[cc_index].channel,
                                    PRESET_CC_CHANNEL_UNUSED,
                                    2U,
                                    0U);
    Display_FormatMenuOptionalField(active_cc_text,
                                    sizeof(active_cc_text),
                                    function_button->active_cc[cc_index].cc_number,
                                    PRESET_CC_NUMBER_UNUSED,
                                    3U,
                                    0U);
    Display_FormatMenuOptionalField(active_value_text,
                                    sizeof(active_value_text),
                                    function_button->active_cc[cc_index].value,
                                    PRESET_CC_VALUE_UNUSED,
                                    3U,
                                    0U);
    Display_FormatMenuOptionalField(inactive_channel_text,
                                    sizeof(inactive_channel_text),
                                    function_button->inactive_cc[cc_index].channel,
                                    PRESET_CC_CHANNEL_UNUSED,
                                    2U,
                                    0U);
    Display_FormatMenuOptionalField(inactive_cc_text,
                                    sizeof(inactive_cc_text),
                                    function_button->inactive_cc[cc_index].cc_number,
                                    PRESET_CC_NUMBER_UNUSED,
                                    3U,
                                    0U);
    Display_FormatMenuOptionalField(inactive_value_text,
                                    sizeof(inactive_value_text),
                                    function_button->inactive_cc[cc_index].value,
                                    PRESET_CC_VALUE_UNUSED,
                                    3U,
                                    0U);

    Display_MenuRowComposeSetTargetY(row_y);
    Display_MenuRowComposeClear(DISPLAY_BG_COLOUR);

    (void)snprintf(row_number_text, sizeof(row_number_text), "%u", (uint8_t)(cc_index + 1U));
    draw_x = Display_MenuRowComposeValueSegment32(draw_x, row_number_text, 0U);
    draw_x = Display_MenuRowComposeValueSegment32(draw_x, " ", 0U);
    draw_x = Display_MenuRowComposeValueSegment32(draw_x,
                                                  active_channel_text,
                                                  (display_state.menu_function_button_message_field_index == 0U) ? 1U : 0U);
    draw_x = Display_MenuRowComposeValueSegment32(draw_x, " ", 0U);
    draw_x = Display_MenuRowComposeValueSegment32(draw_x,
                                                  active_cc_text,
                                                  (display_state.menu_function_button_message_field_index == 1U) ? 1U : 0U);
    draw_x = Display_MenuRowComposeValueSegment32(draw_x, " ", 0U);
    draw_x = Display_MenuRowComposeValueSegment32(draw_x,
                                                  active_value_text,
                                                  (display_state.menu_function_button_message_field_index == 2U) ? 1U : 0U);
    draw_x = Display_MenuRowComposeValueSegment32(draw_x, "    ", 0U);
    draw_x = Display_MenuRowComposeValueSegment32(draw_x,
                                                  inactive_channel_text,
                                                  (display_state.menu_function_button_message_field_index == 3U) ? 1U : 0U);
    draw_x = Display_MenuRowComposeValueSegment32(draw_x, " ", 0U);
    draw_x = Display_MenuRowComposeValueSegment32(draw_x,
                                                  inactive_cc_text,
                                                  (display_state.menu_function_button_message_field_index == 4U) ? 1U : 0U);
    draw_x = Display_MenuRowComposeValueSegment32(draw_x, " ", 0U);
    (void)Display_MenuRowComposeValueSegment32(draw_x,
                                               inactive_value_text,
                                               (display_state.menu_function_button_message_field_index == 5U) ? 1U : 0U);
    Display_MenuRowComposeBlit(row_y);
}

void Display_DrawMenuFunctionButtonCcCompareEditRowNoClear(uint16_t row_y, uint8_t cc_index)
{
    Display_DrawMenuFunctionButtonCcCompareEditRowCore(row_y, cc_index, 0U);
}

static void Display_DrawMenuFunctionButtonCcCompareEditRow(uint16_t row_y, uint8_t cc_index)
{
    Display_DrawMenuFunctionButtonCcCompareEditRowCore(row_y, cc_index, 1U);
}

void Display_FormatFunctionButtonCcCompareRow(uint8_t cc_index,
                                              char *buffer,
                                              size_t buffer_size)
{
    const RuntimeConfigFunctionButton_t *function_button = Presets_GetFunctionButton(display_state.menu_active_preset_index);
    char active_channel_text[4];
    char active_cc_text[4];
    char active_value_text[4];
    char inactive_channel_text[4];
    char inactive_cc_text[4];
    char inactive_value_text[4];

    if (!buffer || buffer_size == 0U || !function_button || cc_index >= RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT)
        return;

    Display_FormatMenuOptionalField(active_channel_text,
                                    sizeof(active_channel_text),
                                    function_button->active_cc[cc_index].channel,
                                    PRESET_CC_CHANNEL_UNUSED,
                                    2U,
                                    0U);
    Display_FormatMenuOptionalField(active_cc_text,
                                    sizeof(active_cc_text),
                                    function_button->active_cc[cc_index].cc_number,
                                    PRESET_CC_NUMBER_UNUSED,
                                    3U,
                                    0U);
    Display_FormatMenuOptionalField(active_value_text,
                                    sizeof(active_value_text),
                                    function_button->active_cc[cc_index].value,
                                    PRESET_CC_VALUE_UNUSED,
                                    3U,
                                    0U);
    Display_FormatMenuOptionalField(inactive_channel_text,
                                    sizeof(inactive_channel_text),
                                    function_button->inactive_cc[cc_index].channel,
                                    PRESET_CC_CHANNEL_UNUSED,
                                    2U,
                                    0U);
    Display_FormatMenuOptionalField(inactive_cc_text,
                                    sizeof(inactive_cc_text),
                                    function_button->inactive_cc[cc_index].cc_number,
                                    PRESET_CC_NUMBER_UNUSED,
                                    3U,
                                    0U);
    Display_FormatMenuOptionalField(inactive_value_text,
                                    sizeof(inactive_value_text),
                                    function_button->inactive_cc[cc_index].value,
                                    PRESET_CC_VALUE_UNUSED,
                                    3U,
                                    0U);
    (void)snprintf(buffer,
                   buffer_size,
                   "%u %s %s %s    %s %s %s",
                   (uint8_t)(cc_index + 1U),
                   active_channel_text,
                   active_cc_text,
                   active_value_text,
                   inactive_channel_text,
                   inactive_cc_text,
                   inactive_value_text);
}

static void Display_DrawMenuFunctionButtonProgramCompareRowAtRow(uint8_t row_index,
                                                                 uint8_t program_index,
                                                                 uint8_t selected)
{
    if (program_index >= RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT)
    {
        Display_ClearStandardMenuRow(row_index);
        return;
    }

    if (selected)
    {
        Display_DrawMenuFunctionButtonProgramCompareEditRow(Display_GetMenuRowYByIndex(row_index), program_index);
        return;
    }

    Display_DrawMenuFunctionButtonProgramCompareEditRowCore(Display_GetMenuRowYByIndex(row_index), program_index, 0U, 1U, 0U);
}

static void Display_DrawMenuFunctionButtonCcCompareRowAtRow(uint8_t row_index,
                                                            uint8_t cc_index,
                                                            uint8_t selected)
{
    char value_text[32];

    if (cc_index >= RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT)
    {
        Display_ClearStandardMenuRow(row_index);
        return;
    }

    if (selected)
    {
        Display_DrawMenuFunctionButtonCcCompareEditRow(Display_GetMenuRowYByIndex(row_index), cc_index);
        return;
    }

    Display_FormatFunctionButtonCcCompareRow(cc_index, value_text, sizeof(value_text));
    Display_DrawMenuRowByIndex(row_index, value_text, "", selected);
}

void Display_DrawMenuFunctionButtonProgramWindowRow(uint8_t row_index, uint8_t program_index)
{
    uint8_t message_selection_index = Display_GetFunctionButtonMessageSelectionIndex();

    if (row_index >= MENU_VISIBLE_ROW_COUNT || program_index >= RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT)
    {
        if (row_index < MENU_VISIBLE_ROW_COUNT)
            Display_ClearStandardMenuRow(row_index);
        return;
    }

    if (program_index == message_selection_index)
    {
        Display_DrawMenuFunctionButtonProgramCompareEditRowWindowUpdate(Display_GetMenuRowYByIndex(row_index), program_index);
        return;
    }

    Display_DrawMenuFunctionButtonProgramCompareEditRowCore(Display_GetMenuRowYByIndex(row_index),
                                                            program_index,
                                                            0U,
                                                            0U,
                                                            1U);
}

void Display_DrawMenuFunctionButtonCcWindowRow(uint8_t row_index, uint8_t cc_index)
{
    uint8_t message_selection_index = Display_GetFunctionButtonMessageSelectionIndex();
    uint8_t cc_selection_index;

    if (row_index >= MENU_VISIBLE_ROW_COUNT || cc_index >= RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT)
    {
        if (row_index < MENU_VISIBLE_ROW_COUNT)
            Display_ClearStandardMenuRow(row_index);
        return;
    }

    if (message_selection_index < RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT)
        return;

    cc_selection_index = (uint8_t)(message_selection_index - RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT);
    Display_DrawMenuFunctionButtonCcCompareRowAtRow(row_index,
                                                    cc_index,
                                                    (cc_index == cc_selection_index) ? 1U : 0U);
}

void Display_DrawMenuFunctionButton(void)
{
    uint8_t message_selection_index;

    if (display_state.menu_function_button_selection_index < MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX)
    {
        /* Top-of-page state: show the three plain text rows and only the first
         * compare header, because selection has not entered message rows yet. */
        for (uint8_t row_index = 0U; row_index < MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT; ++row_index)
            Display_DrawMenuFunctionButtonTextItemAtRow(row_index, row_index);

        Display_DrawMenuFunctionButtonCompareHeaderRow(Display_GetMenuRowYByIndex(MENU_VISIBLE_ROW_COUNT - 1U));
        return;
    }

    message_selection_index = Display_GetFunctionButtonMessageSelectionIndex();

    if (message_selection_index < RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT)
    {
        if (message_selection_index == 0U)
        {
            /* Transition layout from text rows into the compare table: keep the
             * last two text rows visible above the first program message row. */
            Display_DrawMenuFunctionButtonTextItemAtRow(1U, 0U);
            Display_DrawMenuFunctionButtonTextItemAtRow(2U, 1U);
            Display_DrawMenuFunctionButtonCompareHeaderRow(Display_GetMenuRowYByIndex(2U));
            Display_DrawMenuFunctionButtonProgramCompareRowAtRow(3U, 0U, 1U);
            return;
        }

        if (message_selection_index == 1U)
        {
            Display_DrawMenuFunctionButtonTextItemAtRow(2U, 0U);
            Display_DrawMenuFunctionButtonCompareHeaderRow(Display_GetMenuRowYByIndex(1U));
            Display_DrawMenuFunctionButtonProgramCompareRowAtRow(2U, 0U, 0U);
            Display_DrawMenuFunctionButtonProgramCompareRowAtRow(3U, 1U, 1U);
            return;
        }

        Display_DrawMenuFunctionButtonCompareHeaderRow(Display_GetMenuRowYByIndex(0U));
        {
            uint8_t first_program_index = Display_GetVisibleWindowStart(RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT,
                                                                        message_selection_index,
                                                                        (uint8_t)(MENU_VISIBLE_ROW_COUNT - 1U));

            for (uint8_t row_offset = 0U; row_offset < (MENU_VISIBLE_ROW_COUNT - 1U); ++row_offset)
            {
                uint8_t program_index = (uint8_t)(first_program_index + row_offset);

                Display_DrawMenuFunctionButtonProgramCompareRowAtRow((uint8_t)(row_offset + 1U),
                                                                     program_index,
                                                                     (program_index == message_selection_index) ? 1U : 0U);
            }
        }

        return;
    }

    Display_DrawMenuFunctionButtonCompareHeaderRow(Display_GetMenuRowYByIndex(0U));
    Display_DrawMenuFunctionButtonCcHeaderRow(Display_GetMenuRowYByIndex(1U));
    {
        uint8_t cc_selection_index = (uint8_t)(message_selection_index - RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT);

        if (cc_selection_index == 0U)
        {
            /* First CC row keeps the final program row visible above it so the
             * user can still read the boundary between program and CC sections. */
            Display_DrawMenuFunctionButtonProgramCompareRowAtRow(1U,
                                                                 (uint8_t)(RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT - 1U),
                                                                 0U);
            Display_DrawMenuFunctionButtonCcHeaderRow(Display_GetMenuRowYByIndex(2U));
            Display_DrawMenuFunctionButtonCcCompareRowAtRow(3U, 0U, 1U);
            return;
        }

        {
            uint8_t first_cc_index = Display_GetVisibleWindowStart(RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT,
                                                                   cc_selection_index,
                                                                   (uint8_t)(MENU_VISIBLE_ROW_COUNT - 2U));

            for (uint8_t row_offset = 0U; row_offset < (MENU_VISIBLE_ROW_COUNT - 2U); ++row_offset)
            {
                uint8_t cc_index = (uint8_t)(first_cc_index + row_offset);

                Display_DrawMenuFunctionButtonCcCompareRowAtRow((uint8_t)(row_offset + 2U),
                                                                cc_index,
                                                                (cc_index == cc_selection_index) ? 1U : 0U);
            }
        }
    }
}

void Display_DrawMenuFunctionButtonItem(uint8_t item_index)
{
    (void)item_index;
    Display_DrawMenuFunctionButton();
}
