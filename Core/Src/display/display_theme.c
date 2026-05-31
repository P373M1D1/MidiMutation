#include "display/display_theme.h"

#include "runtime_config.h"

/* Theme registration table for the main UI.
 *
 * Maintenance checklist for adding/removing themes:
 * 1. Add or deprecate the enum value in RuntimeConfigDisplayMode_t.
 * 2. Keep persisted enum values stable whenever possible because flash stores
 *    the numeric mode id directly inside RuntimeConfig_t.
 * 3. Add a valid entry to display_theme_specs[] for every live enum value.
 * 4. If a theme needs unique fonts, add those assets/declarations separately.
 *
 * Colours here are literal for each mode; the compose layer no longer flips
 * black/white automatically in bright mode. */
typedef struct
{
    const char *name; /* user-visible label shown in the GLOBAL menu */
    const FontDef32 *footbar_font; /* footer/header font for this theme */
    const FontDef32 *info_font; /* menu/info-row font for this theme */
    const FontDef32 *preset_font; /* large preset-name font for this theme */
    DisplayTheme_t theme; /* colour palette consumed through display_theme.h macros */
} DisplayThemeSpec_t;

/* The array is indexed directly by RuntimeConfigDisplayMode_t. Every persisted
 * mode id therefore needs a valid entry here; a missing entry is not a soft
 * failure because the display code will later dereference fonts/colours. */
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
        .name = "USER1",
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
        /* Custom generated bitmap fonts keep the C64 theme legible at the same
         * geometry as the Consolas themes without changing layout constants. */
        .name = "C64",
        .footbar_font = &Font_C64_8x21,
        .info_font = &Font_C64_15x35,
        .preset_font = &Font_C64_23x49,
        .theme = {
            .display_bg_colour = DARK_JUNGLE_GREEN,
            .main_footbar_color = GREEN_WEB,
            .main_footbar_text_colour = EERIE_BLACK,
            .main_info_text_colour = GREEN_WEB,
            .main_info_edit_cursor_text_colour = DARK_JUNGLE_GREEN,
            .main_info_edit_cursor_bg_colour = GREEN_WEB,
            .main_info_edit_cursor_shared_bg_colour = DARK_PASTEL_GREEN,
            .main_saving_popup_bg_colour = GREEN_WEB,
            .main_saving_popup_text_colour = DARK_JUNGLE_GREEN,
            .main_saving_popup_border_colour = DARK_GREEN_X11,
            .main_mode_header_colour = GREEN_WEB,
            .main_mode_header_edit_colour = DARK_JUNGLE_GREEN,
            .main_mode_header_edit_bg_colour = GREEN_WEB,
            .main_preset_colour = GREEN_WEB,
            .main_bank_colour = GREEN_WEB,
            .main_bank_wet_dry_colour = GREEN_WEB,
            .main_special_function_button_active_colour = GREEN_WEB,
            .main_special_function_button_inactive_colour = DARK_PASTEL_GREEN,
            .main_special_function_button_active_bg = DARK_GREEN_X11,
            .main_alert_badge_text_colour = DARK_JUNGLE_GREEN,
            .bpm_internal_colour = GREEN_WEB,
            .ext_bpm_colour = GREEN_WEB,
        },
    },
    [RUNTIME_CONFIG_DISPLAY_MODE_BIOS] = {
        /* BIOS uses its own font trio for the same reason as C64: preserve the
         * established layout while changing the character style completely. */
        .name = "BIOS",
        .footbar_font = &Font_BIOS_8x21,
        .info_font = &Font_BIOS_15x35,
        .preset_font = &Font_BIOS_23x49,
        .theme = {
            .display_bg_colour = COBALT_BLUE,
            .main_footbar_color = DARK_BLUE,
            .main_footbar_text_colour = BABY_POWDER,
            .main_info_text_colour = BABY_POWDER,
            .main_info_edit_cursor_text_colour = BLACK,
            .main_info_edit_cursor_bg_colour = BABY_POWDER,
            .main_info_edit_cursor_shared_bg_colour = BEAU_BLUE,
            .main_saving_popup_bg_colour = DARK_BLUE,
            .main_saving_popup_text_colour = BABY_POWDER,
            .main_saving_popup_border_colour = WHITE,
            .main_mode_header_colour = WHITE,
            .main_mode_header_edit_colour = BLACK,
            .main_mode_header_edit_bg_colour = BABY_POWDER,
            .main_preset_colour = WHITE,
            .main_bank_colour = BEAU_BLUE,
            .main_bank_wet_dry_colour = BABY_POWDER,
            .main_special_function_button_active_colour = WHITE,
            .main_special_function_button_inactive_colour = BEAU_BLUE,
            .main_special_function_button_active_bg = RED,
            .main_alert_badge_text_colour = WHITE,
            .bpm_internal_colour = BABY_POWDER,
            .ext_bpm_colour = BEAU_BLUE,
        },
    },
};

static RuntimeConfigDisplayMode_t Display_NormalizeThemeMode(RuntimeConfigDisplayMode_t display_mode)
{
    return RuntimeConfig_NormalizeDisplayMode((uint8_t)display_mode);
}

static void Display_LoadRuntimeUserTheme(DisplayTheme_t *destination,
                                         const RuntimeConfigUserTheme_t *source)
{
    if (!destination || !source)
        return;

    destination->display_bg_colour = source->display_bg_colour;
    destination->main_footbar_color = source->main_footbar_color;
    destination->main_footbar_text_colour = source->main_footbar_text_colour;
    destination->main_info_text_colour = source->main_info_text_colour;
    destination->main_info_edit_cursor_text_colour = source->main_info_edit_cursor_text_colour;
    destination->main_info_edit_cursor_bg_colour = source->main_info_edit_cursor_bg_colour;
    destination->main_info_edit_cursor_shared_bg_colour = source->main_info_edit_cursor_shared_bg_colour;
    destination->main_saving_popup_bg_colour = source->main_saving_popup_bg_colour;
    destination->main_saving_popup_text_colour = source->main_saving_popup_text_colour;
    destination->main_saving_popup_border_colour = source->main_saving_popup_border_colour;
    destination->main_mode_header_colour = source->main_mode_header_colour;
    destination->main_mode_header_edit_colour = source->main_mode_header_edit_colour;
    destination->main_mode_header_edit_bg_colour = source->main_mode_header_edit_bg_colour;
    destination->main_preset_colour = source->main_preset_colour;
    destination->main_bank_colour = source->main_bank_colour;
    destination->main_bank_wet_dry_colour = source->main_bank_wet_dry_colour;
    destination->main_special_function_button_active_colour = source->main_special_function_button_active_colour;
    destination->main_special_function_button_inactive_colour = source->main_special_function_button_inactive_colour;
    destination->main_special_function_button_active_bg = source->main_special_function_button_active_bg;
    destination->main_alert_badge_text_colour = source->main_alert_badge_text_colour;
    destination->bpm_internal_colour = source->bpm_internal_colour;
    destination->ext_bpm_colour = source->ext_bpm_colour;
}

static const DisplayThemeSpec_t *Display_GetThemeSpec(void)
{
    const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();
    RuntimeConfigDisplayMode_t mode = global ? global->display_mode : RUNTIME_CONFIG_DISPLAY_MODE_DARK;

    /* Resolve fonts and colours through one shared lookup so a theme change can
     * never update the palette without also updating the matching font set. */
    mode = Display_NormalizeThemeMode(mode);

    return &display_theme_specs[(uint8_t)mode];
}

const DisplayTheme_t *Display_GetTheme(void)
{
    static DisplayTheme_t runtime_user_theme;
    const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();
    RuntimeConfigDisplayMode_t mode = global ? global->display_mode : RUNTIME_CONFIG_DISPLAY_MODE_DARK;
    const RuntimeConfigUserTheme_t *user_theme;

    mode = Display_NormalizeThemeMode(mode);
    user_theme = RuntimeConfig_GetUserTheme(mode);
    if (!user_theme)
        return &display_theme_specs[(uint8_t)mode].theme;

    Display_LoadRuntimeUserTheme(&runtime_user_theme, user_theme);
    return &runtime_user_theme;
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
