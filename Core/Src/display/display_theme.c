#include "display/display_theme.h"

#include "runtime_config.h"

/* Theme palettes selected by the existing GLOBAL -> Display menu setting.
 * Colours here are literal for each mode; the compose layer no longer flips
 * black/white automatically in bright mode. */
typedef struct
{
    const char *name;
    const FontDef32 *footbar_font;
    const FontDef32 *info_font;
    const FontDef32 *preset_font;
    DisplayTheme_t theme;
} DisplayThemeSpec_t;

static const DisplayThemeSpec_t display_theme_specs[RUNTIME_CONFIG_DISPLAY_MODE_COUNT] = {
    [RUNTIME_CONFIG_DISPLAY_MODE_DARK] = {
        .name = "Dark",
        .footbar_font = &Font_Consolas8x21,
        .info_font = &Font_Consolas15x35,
        .preset_font = &Font_Consolas23x49,
        .theme = {
            .display_bg_colour = BLACK,
            .main_footbar_color = JET,
            .main_footbar_text_colour = WHITE,
            .main_info_text_colour = CHARCOAL,
            .main_info_edit_cursor_text_colour = BLACK,
            .main_info_edit_cursor_bg_colour = WHITE,
            .main_info_edit_cursor_shared_bg_colour = YELLOW,
            .main_saving_popup_bg_colour = WHITE,
            .main_saving_popup_text_colour = BLACK,
            .main_saving_popup_border_colour = BLACK,
            .main_mode_header_colour = WHITE,
            .main_mode_header_edit_colour = BLACK,
            .main_mode_header_edit_bg_colour = YELLOW,
            .main_preset_colour = WHITE,
            .main_bank_colour = CHARCOAL,
            .main_bank_wet_dry_colour = WHITE,
            .main_special_function_button_active_colour = WHITE,
            .main_special_function_button_inactive_colour = CHARCOAL,
            .main_special_function_button_active_bg = DARK_RED,
            .main_alert_badge_text_colour = BLACK,
            .bpm_internal_colour = GREEN_WEB,
            .ext_bpm_colour = COBALT_BLUE,
        },
    },
    [RUNTIME_CONFIG_DISPLAY_MODE_BRIGHT] = {
        .name = "Bright",
        .footbar_font = &Font_Consolas8x21,
        .info_font = &Font_Consolas15x35,
        .preset_font = &Font_Consolas23x49,
        .theme = {
            .display_bg_colour = WHITE,
            .main_footbar_color = JET,
            .main_footbar_text_colour = WHITE,
            .main_info_text_colour = CHARCOAL,
            .main_info_edit_cursor_text_colour = WHITE,
            .main_info_edit_cursor_bg_colour = BLACK,
            .main_info_edit_cursor_shared_bg_colour = DARK_RED,
            .main_saving_popup_bg_colour = BLACK,
            .main_saving_popup_text_colour = WHITE,
            .main_saving_popup_border_colour = WHITE,
            .main_mode_header_colour = BLACK,
            .main_mode_header_edit_colour = BLACK,
            .main_mode_header_edit_bg_colour = YELLOW,
            .main_preset_colour = BLACK,
            .main_bank_colour = CHARCOAL,
            .main_bank_wet_dry_colour = CHARCOAL,
            .main_special_function_button_active_colour = WHITE,
            .main_special_function_button_inactive_colour = CHARCOAL,
            .main_special_function_button_active_bg = DARK_RED,
            .main_alert_badge_text_colour = WHITE,
            .bpm_internal_colour = GREEN_WEB,
            .ext_bpm_colour = COBALT_BLUE,
        },
    },
    [RUNTIME_CONFIG_DISPLAY_MODE_USER] = {
        .name = "User",
        .footbar_font = &Font_Consolas8x21,
        .info_font = &Font_Consolas15x35,
        .preset_font = &Font_Consolas23x49,
        .theme = {
            .display_bg_colour = BLACK,
            .main_footbar_color = BLUE_SAPPHIRE,
            .main_footbar_text_colour = BABY_POWDER,
            .main_info_text_colour = AQUAMARINE,
            .main_info_edit_cursor_text_colour = BLACK,
            .main_info_edit_cursor_bg_colour = CANTALOUPE_MELON,
            .main_info_edit_cursor_shared_bg_colour = BRINK_PINK,
            .main_saving_popup_bg_colour = BABY_POWDER,
            .main_saving_popup_text_colour = BLUE_SAPPHIRE,
            .main_saving_popup_border_colour = CANTALOUPE_MELON,
            .main_mode_header_colour = BABY_POWDER,
            .main_mode_header_edit_colour = BLACK,
            .main_mode_header_edit_bg_colour = CANTALOUPE_MELON,
            .main_preset_colour = BABY_POWDER,
            .main_bank_colour = PALE_AQUA,
            .main_bank_wet_dry_colour = CANTALOUPE_MELON,
            .main_special_function_button_active_colour = BABY_POWDER,
            .main_special_function_button_inactive_colour = PALE_AQUA,
            .main_special_function_button_active_bg = BLUE_SAPPHIRE,
            .main_alert_badge_text_colour = BLACK,
            .bpm_internal_colour = AQUA,
            .ext_bpm_colour = CANTALOUPE_MELON,
        },
    },
    [RUNTIME_CONFIG_DISPLAY_MODE_WEED] = {
        .name = "Weed",
        .footbar_font = &Font_Consolas8x21,
        .info_font = &Font_Consolas15x35,
        .preset_font = &Font_Consolas23x49,
        .theme = {
            .display_bg_colour = DARK_JUNGLE_GREEN,
            .main_footbar_color = PAKISTAN_GREEN,
            .main_footbar_text_colour = LIGHT_GOLDENROD_YELLOW,
            .main_info_text_colour = MOSS_GREEN,
            .main_info_edit_cursor_text_colour = BLACK,
            .main_info_edit_cursor_bg_colour = LIGHT_GOLDENROD_YELLOW,
            .main_info_edit_cursor_shared_bg_colour = OLD_GOLD,
            .main_saving_popup_bg_colour = LIGHT_GOLDENROD_YELLOW,
            .main_saving_popup_text_colour = PAKISTAN_GREEN,
            .main_saving_popup_border_colour = HUNTER_GREEN,
            .main_mode_header_colour = LIGHT_GOLDENROD_YELLOW,
            .main_mode_header_edit_colour = BLACK,
            .main_mode_header_edit_bg_colour = OLD_GOLD,
            .main_preset_colour = BABY_POWDER,
            .main_bank_colour = MOSS_GREEN,
            .main_bank_wet_dry_colour = LIGHT_GOLDENROD_YELLOW,
            .main_special_function_button_active_colour = LIGHT_GOLDENROD_YELLOW,
            .main_special_function_button_inactive_colour = MOSS_GREEN,
            .main_special_function_button_active_bg = SAP_GREEN,
            .main_alert_badge_text_colour = BLACK,
            .bpm_internal_colour = SAP_GREEN,
            .ext_bpm_colour = OLD_GOLD,
        },
    },
    [RUNTIME_CONFIG_DISPLAY_MODE_BLUESCREEN] = {
        .name = "BLUESCREEN",
        .footbar_font = &Font_Consolas8x21,
        .info_font = &Font_Consolas15x35,
        .preset_font = &Font_Consolas23x49,
        .theme = {
            .display_bg_colour = DARK_BLUE,
            .main_footbar_color = NAVY_BLUE,
            .main_footbar_text_colour = BABY_POWDER,
            .main_info_text_colour = BEAU_BLUE,
            .main_info_edit_cursor_text_colour = BABY_POWDER,
            .main_info_edit_cursor_bg_colour = RUSSIAN_VIOLET,
            .main_info_edit_cursor_shared_bg_colour = DARK_SLATE_BLUE,
            .main_saving_popup_bg_colour = INDIGO_DYE,
            .main_saving_popup_text_colour = BABY_POWDER,
            .main_saving_popup_border_colour = BLUE_YONDER,
            .main_mode_header_colour = BLUE_YONDER,
            .main_mode_header_edit_colour = BABY_POWDER,
            .main_mode_header_edit_bg_colour = PERSIAN_INDIGO,
            .main_preset_colour = BABY_POWDER,
            .main_bank_colour = BLUE_YONDER,
            .main_bank_wet_dry_colour = BABY_POWDER,
            .main_special_function_button_active_colour = BABY_POWDER,
            .main_special_function_button_inactive_colour = DARK_BLUE_GRAY,
            .main_special_function_button_active_bg = RUSSIAN_VIOLET,
            .main_alert_badge_text_colour = BABY_POWDER,
            .bpm_internal_colour = BEAU_BLUE,
            .ext_bpm_colour = BLUE_YONDER,
        },
    },
    [RUNTIME_CONFIG_DISPLAY_MODE_MIDNIGHT] = {
        .name = "Midnight",
        .footbar_font = &Font_Consolas8x21,
        .info_font = &Font_Consolas15x35,
        .preset_font = &Font_Consolas23x49,
        .theme = {
            .display_bg_colour = NAVY_BLUE,
            .main_footbar_color = DARK_BLUE,
            .main_footbar_text_colour = BLUE_YONDER,
            .main_info_text_colour = BLUE_YONDER,
            .main_info_edit_cursor_text_colour = BABY_POWDER,
            .main_info_edit_cursor_bg_colour = MIDNIGHT_BLUE,
            .main_info_edit_cursor_shared_bg_colour = PERSIAN_INDIGO,
            .main_saving_popup_bg_colour = INDIGO_DYE,
            .main_saving_popup_text_colour = BEAU_BLUE,
            .main_saving_popup_border_colour = BLUE_SAPPHIRE,
            .main_mode_header_colour = BEAU_BLUE,
            .main_mode_header_edit_colour = BABY_POWDER,
            .main_mode_header_edit_bg_colour = MIDNIGHT_BLUE,
            .main_preset_colour = BEAU_BLUE,
            .main_bank_colour = BLUE_YONDER,
            .main_bank_wet_dry_colour = BEAU_BLUE,
            .main_special_function_button_active_colour = BABY_POWDER,
            .main_special_function_button_inactive_colour = BLUE_SAPPHIRE,
            .main_special_function_button_active_bg = MIDNIGHT_BLUE,
            .main_alert_badge_text_colour = BABY_POWDER,
            .bpm_internal_colour = BLUE_SAPPHIRE,
            .ext_bpm_colour = BLUE_YONDER,
        },
    },
    [RUNTIME_CONFIG_DISPLAY_MODE_TRIPPING] = {
        .name = "Tripping",
        .footbar_font = &Font_Consolas8x21,
        .info_font = &Font_Consolas15x35,
        .preset_font = &Font_Consolas23x49,
        .theme = {
            .display_bg_colour = BLACK,
            .main_footbar_color = PSYCHEDELIC_PURPLE,
            .main_footbar_text_colour = CYBER_YELLOW,
            .main_info_text_colour = CAPRI,
            .main_info_edit_cursor_text_colour = BLACK,
            .main_info_edit_cursor_bg_colour = ELECTRIC_LIME,
            .main_info_edit_cursor_shared_bg_colour = HOT_PINK,
            .main_saving_popup_bg_colour = CHARTREUSE_WEB,
            .main_saving_popup_text_colour = MAGENTA,
            .main_saving_popup_border_colour = CAPRI,
            .main_mode_header_colour = CYBER_YELLOW,
            .main_mode_header_edit_colour = BLACK,
            .main_mode_header_edit_bg_colour = HOT_PINK,
            .main_preset_colour = ELECTRIC_LIME,
            .main_bank_colour = HOT_MAGENTA,
            .main_bank_wet_dry_colour = CYBER_YELLOW,
            .main_special_function_button_active_colour = BLACK,
            .main_special_function_button_inactive_colour = CAPRI,
            .main_special_function_button_active_bg = HOT_PINK,
            .main_alert_badge_text_colour = CYBER_YELLOW,
            .bpm_internal_colour = CAPRI,
            .ext_bpm_colour = ELECTRIC_LIME,
        },
    },
    [RUNTIME_CONFIG_DISPLAY_MODE_USER2] = {
        .name = "USER2",
        .footbar_font = &Font_Consolas8x21,
        .info_font = &Font_Consolas15x35,
        .preset_font = &Font_Consolas23x49,
        .theme = {
            .display_bg_colour = BLACK,
            .main_footbar_color = DARK_GOLDENROD,
            .main_footbar_text_colour = LIGHT_GOLDENROD_YELLOW,
            .main_info_text_colour = AMBER,
            .main_info_edit_cursor_text_colour = BLACK,
            .main_info_edit_cursor_bg_colour = GOLDENROD,
            .main_info_edit_cursor_shared_bg_colour = ORANGE,
            .main_saving_popup_bg_colour = LIGHT_GOLDENROD_YELLOW,
            .main_saving_popup_text_colour = DARK_BROWN,
            .main_saving_popup_border_colour = GOLDEN_BROWN,
            .main_mode_header_colour = LIGHT_GOLDENROD_YELLOW,
            .main_mode_header_edit_colour = BLACK,
            .main_mode_header_edit_bg_colour = AMBER,
            .main_preset_colour = LIGHT_GOLDENROD_YELLOW,
            .main_bank_colour = GOLDENROD,
            .main_bank_wet_dry_colour = AMBER,
            .main_special_function_button_active_colour = LIGHT_GOLDENROD_YELLOW,
            .main_special_function_button_inactive_colour = GOLD_FUSION,
            .main_special_function_button_active_bg = BROWN,
            .main_alert_badge_text_colour = BLACK,
            .bpm_internal_colour = AMBER,
            .ext_bpm_colour = GOLDENROD,
        },
    },
    [RUNTIME_CONFIG_DISPLAY_MODE_USER3] = {
        .name = "USER3",
        .footbar_font = &Font_Consolas8x21,
        .info_font = &Font_Consolas15x35,
        .preset_font = &Font_Consolas23x49,
        .theme = {
            .display_bg_colour = BLACK,
            .main_footbar_color = TYRIAN_PURPLE,
            .main_footbar_text_colour = BABY_POWDER,
            .main_info_text_colour = CAPRI,
            .main_info_edit_cursor_text_colour = BLACK,
            .main_info_edit_cursor_bg_colour = CAPRI,
            .main_info_edit_cursor_shared_bg_colour = HOT_PINK,
            .main_saving_popup_bg_colour = BABY_POWDER,
            .main_saving_popup_text_colour = TYRIAN_PURPLE,
            .main_saving_popup_border_colour = CAPRI,
            .main_mode_header_colour = BABY_POWDER,
            .main_mode_header_edit_colour = BLACK,
            .main_mode_header_edit_bg_colour = CAPRI,
            .main_preset_colour = BABY_POWDER,
            .main_bank_colour = CAPRI,
            .main_bank_wet_dry_colour = CAPRI,
            .main_special_function_button_active_colour = BABY_POWDER,
            .main_special_function_button_inactive_colour = VIOLET_CRAYOLA,
            .main_special_function_button_active_bg = TYRIAN_PURPLE,
            .main_alert_badge_text_colour = BLACK,
            .bpm_internal_colour = CAPRI,
            .ext_bpm_colour = HOT_PINK,
        },
    },
    [RUNTIME_CONFIG_DISPLAY_MODE_C64] = {
        .name = "C64",
        .footbar_font = &Font_C64_8x21,
        .info_font = &Font_C64_15x35,
        .preset_font = &Font_C64_23x49,
        .theme = {
            .display_bg_colour = DARK_GREEN,
            .main_footbar_color = GREEN_WEB,
            .main_footbar_text_colour = EERIE_BLACK,
            .main_info_text_colour = GREEN_WEB,
            .main_info_edit_cursor_text_colour = RICH_BLACK,
            .main_info_edit_cursor_bg_colour = GREEN_WEB,
            .main_info_edit_cursor_shared_bg_colour = DARK_PASTEL_GREEN,
            .main_saving_popup_bg_colour = GREEN_WEB,
            .main_saving_popup_text_colour = RICH_BLACK,
            .main_saving_popup_border_colour = DARK_GREEN_X11,
            .main_mode_header_colour = GREEN_WEB,
            .main_mode_header_edit_colour = RICH_BLACK,
            .main_mode_header_edit_bg_colour = GREEN_WEB,
            .main_preset_colour = GREEN_WEB,
            .main_bank_colour = GREEN_WEB,
            .main_bank_wet_dry_colour = GREEN_WEB,
            .main_special_function_button_active_colour = GREEN_WEB,
            .main_special_function_button_inactive_colour = DARK_PASTEL_GREEN,
            .main_special_function_button_active_bg = DARK_GREEN_X11,
            .main_alert_badge_text_colour = RICH_BLACK,
            .bpm_internal_colour = GREEN_WEB,
            .ext_bpm_colour = GREEN_WEB,
        },
    },
};

static RuntimeConfigDisplayMode_t Display_NormalizeThemeMode(RuntimeConfigDisplayMode_t display_mode)
{
    if ((uint8_t)display_mode >= (uint8_t)RUNTIME_CONFIG_DISPLAY_MODE_COUNT)
        return RUNTIME_CONFIG_DISPLAY_MODE_DARK;

    return display_mode;
}

static const DisplayThemeSpec_t *Display_GetThemeSpec(void)
{
    const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();
    RuntimeConfigDisplayMode_t mode = global ? global->display_mode : RUNTIME_CONFIG_DISPLAY_MODE_DARK;

    mode = Display_NormalizeThemeMode(mode);

    return &display_theme_specs[(uint8_t)mode];
}

const DisplayTheme_t *Display_GetTheme(void)
{
    return &Display_GetThemeSpec()->theme;
}

const char *Display_GetThemeName(RuntimeConfigDisplayMode_t display_mode)
{
    display_mode = Display_NormalizeThemeMode(display_mode);

    return display_theme_specs[(uint8_t)display_mode].name;
}

const FontDef32 *Display_GetThemeFootbarFont(void)
{
    return Display_GetThemeSpec()->footbar_font;
}

const FontDef32 *Display_GetThemeInfoFont(void)
{
    return Display_GetThemeSpec()->info_font;
}

const FontDef32 *Display_GetThemePresetFont(void)
{
    return Display_GetThemeSpec()->preset_font;
}
