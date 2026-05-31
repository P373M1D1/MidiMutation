#include <string.h>

#include "display/display_compose_helpers.h"
#include "display/display_internal.h"
#include "display/display_layout.h"
#include "display/display_menu_page_user_theme.h"
#include "display/display_menu_redraw_utils.h"
#include "display/display_menu_row_render.h"
#include "display/display_palette_registry.h"
#include "display/display_theme.h"
#include "runtime_config.h"
#include "st7796.h"

#define MENU_ITEM_X 24U
#define MENU_USER_THEME_VALUE_X 160U
#define MENU_USER_THEME_SWATCH_W 56U
#define MENU_USER_THEME_SWATCH_H 19U
#define MENU_USER_THEME_SWATCH_X (ST7796_WIDTH - MENU_ITEM_X - MENU_USER_THEME_SWATCH_W)
#define MENU_USER_THEME_SWATCH_GAP 10U

static const char * const menu_user_theme_labels[MENU_USER_THEME_ITEM_COUNT] = {
    "bg",
    "footbar",
    "foot_txt",
    "info_txt",
    "cursor_txt",
    "cursor_bg",
    "cursor_shbg",
    "popup_bg",
    "popup_txt",
    "popup_brdr",
    "header",
    "head_edit",
    "head_edbg",
    "preset",
    "bank",
    "wet_dry",
    "fn_on",
    "fn_off",
    "fn_on_bg",
    "alert_txt",
    "bpm_int",
    "ext_bpm",
};

static RuntimeConfigDisplayMode_t Display_GetActiveUserThemeMenuMode(void)
{
    RuntimeConfigDisplayMode_t mode = RuntimeConfig_NormalizeDisplayMode(display_state.menu_active_user_theme_mode);

    return RuntimeConfig_TryGetUserThemeIndex(mode, NULL) ? mode : RUNTIME_CONFIG_DISPLAY_MODE_USER;
}

static void Display_FormatUserThemePaletteName(uint16_t colour, char *buffer, size_t buffer_size)
{
    const char *palette_name;
    size_t max_chars;
    size_t copy_chars;

    if (!buffer || buffer_size == 0U)
        return;

    palette_name = DisplayPalette_GetName(DisplayPalette_FindIndexByValue(colour));
    max_chars = (size_t)((MENU_USER_THEME_SWATCH_X - MENU_USER_THEME_VALUE_X - MENU_USER_THEME_SWATCH_GAP) / MAIN_FOOTBAR_FONT.width);
    if (max_chars >= buffer_size)
        max_chars = buffer_size - 1U;

    copy_chars = strnlen(palette_name, max_chars);
    memcpy(buffer, palette_name, copy_chars);
    buffer[copy_chars] = '\0';
}

static void Display_DrawUserThemeRow(uint8_t row_index,
                                     const char *label,
                                     const char *palette_name,
                                     uint16_t swatch_colour,
                                     uint8_t selected)
{
    uint16_t row_y = Display_GetMenuRowYByIndex(row_index);
    uint16_t background = selected ? MAIN_INFO_EDIT_CURSOR_BG_COLOUR : DISPLAY_BG_COLOUR;
    uint16_t foreground = selected ? MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR : MAIN_INFO_TEXT_COLOUR;
    uint16_t text_y = (uint16_t)((MAIN_INFO_FONT_CELL_HEIGHT - MAIN_FOOTBAR_FONT.height) / 2U);
    uint16_t swatch_y = (uint16_t)((MAIN_INFO_FONT_CELL_HEIGHT - MENU_USER_THEME_SWATCH_H) / 2U);

    Display_ComposeClear(ST7796_WIDTH,
                         MAIN_INFO_FONT_CELL_HEIGHT,
                         background);
    Display_ComposeString32(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            MENU_ITEM_X,
                            text_y,
                            label,
                            MAIN_FOOTBAR_FONT,
                            foreground,
                            background);
    Display_ComposeString32(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            MENU_USER_THEME_VALUE_X,
                            text_y,
                            palette_name,
                            MAIN_FOOTBAR_FONT,
                            foreground,
                            background);

    Display_ComposeFillRect(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            MENU_USER_THEME_SWATCH_X,
                            swatch_y,
                            MENU_USER_THEME_SWATCH_W,
                            MENU_USER_THEME_SWATCH_H,
                            foreground);
    Display_ComposeFillRect(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            (uint16_t)(MENU_USER_THEME_SWATCH_X + 1U),
                            (uint16_t)(swatch_y + 1U),
                            (uint16_t)(MENU_USER_THEME_SWATCH_W - 2U),
                            (uint16_t)(MENU_USER_THEME_SWATCH_H - 2U),
                            swatch_colour);
    Display_ComposeBlit(0U,
                        row_y,
                        ST7796_WIDTH,
                        MAIN_INFO_FONT_CELL_HEIGHT);
}

void Display_DrawMenuUserTheme(void)
{
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MENU_USER_THEME_ITEM_COUNT,
                                                                   display_state.menu_user_theme_selection_index);

    for (uint8_t row_index = 0U; row_index < MENU_VISIBLE_ROW_COUNT; ++row_index)
    {
        uint8_t item_index = (uint8_t)(first_visible_index + row_index);

        if (item_index < MENU_USER_THEME_ITEM_COUNT)
            Display_DrawMenuUserThemeItem(item_index);
        else
            Display_ClearStandardMenuRow(row_index);
    }
}

void Display_DrawMenuUserThemeItem(uint8_t item_index)
{
    RuntimeConfigDisplayMode_t mode = Display_GetActiveUserThemeMenuMode();
    const RuntimeConfigUserTheme_t *user_theme = RuntimeConfig_GetUserTheme(mode);
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MENU_USER_THEME_ITEM_COUNT,
                                                                   display_state.menu_user_theme_selection_index);
    char palette_name[32];
    uint16_t swatch_colour;
    uint8_t row_index;

    if (!user_theme || item_index >= MENU_USER_THEME_ITEM_COUNT)
        return;

    if (item_index < first_visible_index || item_index >= (uint8_t)(first_visible_index + MENU_VISIBLE_ROW_COUNT))
        return;

    row_index = (uint8_t)(item_index - first_visible_index);
    swatch_colour = RuntimeConfig_GetUserThemeColour(user_theme, (RuntimeConfigUserThemeField_t)item_index);
    Display_FormatUserThemePaletteName(swatch_colour, palette_name, sizeof(palette_name));
    Display_DrawUserThemeRow(row_index,
                             menu_user_theme_labels[item_index],
                             palette_name,
                             swatch_colour,
                             (item_index == display_state.menu_user_theme_selection_index) ? 1U : 0U);
}