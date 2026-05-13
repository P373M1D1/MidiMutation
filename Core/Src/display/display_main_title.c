#include <string.h>

#include "display_functions.h"
#include "display/display_compose_helpers.h"
#include "display/display_internal.h"
#include "display/display_layout.h"
#include "display/display_main_title.h"

/* Main-title renderer for the large preset line and the bank-name line.
 *
 * This file owns the centering math and cursor helpers for preset-name edits.
 * If title alignment, bank-line badges, or preset-name cursor limits ever look
 * wrong, start here before editing the larger display monolith. */

static uint8_t Display_GetPresetNameLength(const Preset_t *preset)
{
    if (!preset)
        return 0U;

    return (uint8_t)strnlen(preset->name, PRESET_NAME_LENGTH);
}

static uint8_t Display_GetPresetNameRenderLength(const Preset_t *preset)
{
    uint8_t name_length = Display_GetPresetNameLength(preset);

    /* Even an empty preset keeps one render cell so edit highlighting still has
     * a stable target instead of collapsing the centered title math to zero. */
    return (name_length > 0U) ? name_length : 1U;
}

static uint8_t Display_GetPresetNamePadLeft(const Preset_t *preset)
{
    return (uint8_t)((PRESET_NAME_LENGTH - Display_GetPresetNameRenderLength(preset)) / 2U);
}

uint8_t Display_GetPresetNameEditMaxIndex(const Preset_t *preset)
{
    return (uint8_t)(PRESET_NAME_LENGTH - Display_GetPresetNamePadLeft(preset) - 1U);
}

static uint16_t Display_GetPresetNameBaseX(void)
{
    return (uint16_t)((ST7796_WIDTH - (PRESET_NAME_LENGTH * MAIN_PRESET_FONT.width)) / 2U);
}

static void Display_PresetNameComposeFillRect(uint16_t x,
                                              uint16_t y,
                                              uint16_t w,
                                              uint16_t h,
                                              uint16_t colour)
{
    Display_ComposeFillRect(MAIN_PRESET_ROW_BUFFER_WIDTH,
                            MAIN_PRESET_FONT_CELL_HEIGHT,
                            x,
                            y,
                            w,
                            h,
                            colour);
}

static void Display_PresetNameComposeChar32(uint16_t x,
                                            uint16_t y,
                                            char ch,
                                            uint16_t colour,
                                            uint16_t background)
{
    Display_ComposeChar32(MAIN_PRESET_ROW_BUFFER_WIDTH,
                          MAIN_PRESET_FONT_CELL_HEIGHT,
                          x,
                          y,
                          ch,
                          MAIN_PRESET_FONT,
                          colour,
                          background);
}

void Display_DrawCurrentBankNameLine(void)
{
    const char *bank_name = Presets_GetBankName(current_bank);
    const RuntimeConfigBank_t *bank = RuntimeConfig_GetBank(current_bank);
    uint16_t group_width;
    uint16_t badge_x;
    uint16_t draw_x;
    uint16_t badge_width;
    uint16_t gap_width = MAIN_BANK_FONT.width;
    size_t bank_name_length;

    if (!bank_name)
        bank_name = "";

    bank_name_length = strnlen(bank_name, MAIN_BANK_TEXT_CHARS);
    group_width = (uint16_t)(bank_name_length * MAIN_BANK_FONT.width);
    badge_width = 0U;

    if (bank && bank->wet_dry_enabled)
    {
        /* Center the bank name and wet/dry badge as one visual group so adding
         * the badge does not shove the bank name off-center on its own. */
        badge_width = (uint16_t)(strlen(MAIN_BANK_WET_DRY_BADGE_TEXT) * MAIN_BANK_FONT.width);
        group_width = (uint16_t)(group_width + gap_width + badge_width);
    }

    Display_ComposeClear(ST7796_WIDTH,
                         MAIN_BANK_FONT.height,
                         MAIN_BANK_BG_COLOUR);

    if (group_width > ST7796_WIDTH)
        group_width = ST7796_WIDTH;

    draw_x = (uint16_t)((ST7796_WIDTH - group_width) / 2U);

    Display_ComposeString32(ST7796_WIDTH,
                            MAIN_BANK_FONT.height,
                            draw_x,
                            0U,
                            bank_name,
                            MAIN_BANK_FONT,
                            MAIN_BANK_COLOUR,
                            MAIN_BANK_BG_COLOUR);

    if (badge_width > 0U)
    {
        badge_x = (uint16_t)(draw_x + (bank_name_length * MAIN_BANK_FONT.width) + gap_width);
        Display_ComposeString32(ST7796_WIDTH,
                                MAIN_BANK_FONT.height,
                                badge_x,
                                0U,
                                MAIN_BANK_WET_DRY_BADGE_TEXT,
                                MAIN_BANK_FONT,
                                MAIN_BANK_WET_DRY_COLOUR,
                                MAIN_BANK_BG_COLOUR);
    }

    Display_ComposeBlit(0U,
                        MAIN_BANK_TEXT_Y,
                        ST7796_WIDTH,
                        MAIN_BANK_FONT.height);
}

void Display_DrawPresetName(const Preset_t *preset)
{
    DisplayPresetEditField_t edit_field;
    uint8_t name_field_selected;
    uint8_t name_char_edit_active;
    uint8_t name_length;
    uint8_t render_length;
    uint8_t pad_left;
    uint16_t base_x = Display_GetPresetNameBaseX();

    if (!preset)
        return;

    edit_field = Display_PresetEditGetField();
    name_field_selected = (edit_field.type == DISPLAY_PRESET_EDIT_FIELD_NAME
        && display_state.preset_edit_mode_active
        && !display_state.preset_name_edit_active) ? 1U : 0U;
    name_char_edit_active = (edit_field.type == DISPLAY_PRESET_EDIT_FIELD_NAME
        && display_state.preset_edit_mode_active
        && display_state.preset_name_edit_active) ? 1U : 0U;
    name_length = Display_GetPresetNameLength(preset);
    render_length = Display_GetPresetNameRenderLength(preset);
    pad_left = Display_GetPresetNamePadLeft(preset);

    for (uint8_t cell_index = 0U; cell_index < PRESET_NAME_LENGTH; ++cell_index)
    {
        int16_t logical_index = (int16_t)cell_index - (int16_t)pad_left;
        uint16_t char_x = (uint16_t)(cell_index * MAIN_PRESET_FONT.width);
        uint16_t foreground = MAIN_PRESET_COLOUR;
        uint16_t background = MAIN_PRESET_BG_COLOUR;
        char ch = ' ';

        if (logical_index >= 0 && logical_index < (int16_t)name_length)
            ch = preset->name[logical_index];

        if (name_field_selected
            && logical_index >= 0
            && logical_index < (int16_t)render_length)
        {
            /* Whole-field selection uses a broad highlight, while the separate
             * char-edit mode below narrows that to the active character cell. */
            foreground = MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR;
            background = MAIN_INFO_EDIT_CURSOR_BG_COLOUR;
        }

        if (name_char_edit_active && logical_index == (int16_t)display_state.preset_name_edit_cursor_index)
        {
            foreground = MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR;
            background = MAIN_INFO_EDIT_CURSOR_BG_COLOUR;
        }

        Display_PresetNameComposeFillRect(char_x,
                                          0U,
                                          MAIN_PRESET_FONT.width,
                                          MAIN_PRESET_FONT.height,
                                          background);

        if (ch != ' ')
        {
            Display_PresetNameComposeChar32(char_x,
                                            0U,
                                            ch,
                                            foreground,
                                            background);
        }
    }

    Display_ComposeBlit(base_x,
                        MAIN_PRESET_TEXT_Y,
                        MAIN_PRESET_ROW_BUFFER_WIDTH,
                        MAIN_PRESET_FONT_CELL_HEIGHT);
}