#ifndef DISPLAY_THEME_H
#define DISPLAY_THEME_H

#include <stdint.h>

#include "fonts.h"
#include "runtime_config.h"
#include "st7796.h"

/* Complete palette contract for one display theme.
 *
 * Every entry in display_theme.c must provide every field here because the UI
 * reaches the palette only through these names/macros. When adding a new field,
 * update all existing theme specs as part of the same change. */
typedef struct
{
	uint16_t display_bg_colour; /* whole-screen background outside explicit badges/popups */
	uint16_t main_footbar_color; /* footer/status bar fill */
	uint16_t main_footbar_text_colour; /* footer/status bar text */
	uint16_t main_info_text_colour; /* normal main-info and menu text */
	uint16_t main_info_edit_cursor_text_colour; /* text drawn inside the active edit row/badge */
	uint16_t main_info_edit_cursor_bg_colour; /* active edit row/badge background */
	uint16_t main_info_edit_cursor_shared_bg_colour; /* alternate highlight used for shared/paired fields */
	uint16_t main_saving_popup_bg_colour; /* saving popup fill */
	uint16_t main_saving_popup_text_colour; /* saving popup text */
	uint16_t main_saving_popup_border_colour; /* saving popup border */
	uint16_t main_mode_header_colour; /* LIVE/MENU header text */
	uint16_t main_mode_header_edit_colour; /* EDIT badge text */
	uint16_t main_mode_header_edit_bg_colour; /* EDIT badge background */
	uint16_t main_preset_colour; /* large preset-name line */
	uint16_t main_bank_colour; /* bank-name line */
	uint16_t main_bank_wet_dry_colour; /* W/D badge beside the bank line */
	uint16_t main_special_function_button_active_colour; /* active special-button text */
	uint16_t main_special_function_button_inactive_colour; /* inactive special-button text */
	uint16_t main_special_function_button_active_bg; /* active special-button badge fill */
	uint16_t main_alert_badge_text_colour; /* text inside alert/confirm badges */
	uint16_t bpm_internal_colour; /* internally generated BPM text */
	uint16_t ext_bpm_colour; /* external MIDI-clock BPM text */
} DisplayTheme_t;

/* Most callers should use the macros below rather than caching raw palette
 * pointers, so future theme refactors stay localized to this interface. */
const DisplayTheme_t *Display_GetTheme(void);
/* Theme names are user-facing strings used by the GLOBAL page; the lookup also
 * normalizes out-of-range persisted ids before the label reaches the screen. */
const char *Display_GetThemeName(RuntimeConfigDisplayMode_t display_mode);
uint8_t Display_IsStarlightMode(RuntimeConfigDisplayMode_t display_mode);
uint8_t Display_ThemeUsesStarlightBackground(void);
const FontDef32 *Display_GetThemeFootbarFont(void);
const FontDef32 *Display_GetThemeInfoFont(void);
const FontDef32 *Display_GetThemePresetFont(void);

#define DISPLAY_BG_COLOUR                          (Display_GetTheme()->display_bg_colour)

#define MAIN_FOOTBAR_COLOR                         (Display_GetTheme()->main_footbar_color)
#define MAIN_FOOTBAR_TEXT_COLOUR                   (Display_GetTheme()->main_footbar_text_colour)

#define MAIN_INFO_TEXT_COLOUR                      (Display_GetTheme()->main_info_text_colour)
#define MAIN_INFO_TEXT_BG_COLOUR                   DISPLAY_BG_COLOUR
#define MAIN_INFO_SHARED_TEXT_COLOUR               (Display_ThemeUsesStarlightBackground() ? MAIN_INFO_TEXT_COLOUR : DISPLAY_BG_COLOUR)
#define MAIN_INFO_SHARED_BG_COLOUR                 (Display_ThemeUsesStarlightBackground() ? MAIN_INFO_TEXT_BG_COLOUR : MAIN_INFO_TEXT_COLOUR)
#define MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR          (Display_GetTheme()->main_info_edit_cursor_text_colour)
#define MAIN_INFO_EDIT_CURSOR_BG_COLOUR            (Display_GetTheme()->main_info_edit_cursor_bg_colour)
#define MAIN_INFO_EDIT_CURSOR_SHARED_BG_COLOUR     (Display_GetTheme()->main_info_edit_cursor_shared_bg_colour)

#define MAIN_SAVING_POPUP_BG_COLOUR                (Display_GetTheme()->main_saving_popup_bg_colour)
#define MAIN_SAVING_POPUP_TEXT_COLOUR              (Display_GetTheme()->main_saving_popup_text_colour)
#define MAIN_SAVING_POPUP_BORDER_COLOUR            (Display_GetTheme()->main_saving_popup_border_colour)
#define MAIN_SCROLL_INDICATOR_COLOUR               MAIN_INFO_TEXT_COLOUR

#define MAIN_MODE_HEADER_COLOUR                    (Display_GetTheme()->main_mode_header_colour)
#define MAIN_MODE_HEADER_EDIT_COLOUR               (Display_GetTheme()->main_mode_header_edit_colour)
#define MAIN_MODE_HEADER_EDIT_BG_COLOUR            (Display_GetTheme()->main_mode_header_edit_bg_colour)

#define MAIN_PRESET_COLOUR                         (Display_GetTheme()->main_preset_colour)
#define MAIN_PRESET_BG_COLOUR                      DISPLAY_BG_COLOUR

#define MAIN_BANK_COLOUR                           (Display_GetTheme()->main_bank_colour)
#define MAIN_BANK_BG_COLOUR                        DISPLAY_BG_COLOUR
#define MAIN_BANK_WET_DRY_COLOUR                   (Display_GetTheme()->main_bank_wet_dry_colour)

#define MAIN_SPECIAL_FUNCTION_BUTTON_ACTIVE_COLOUR   (Display_GetTheme()->main_special_function_button_active_colour)
#define MAIN_SPECIAL_FUNCTION_BUTTON_INACTIVE_COLOUR (Display_GetTheme()->main_special_function_button_inactive_colour)
#define MAIN_SPECIAL_FUNCTION_BUTTON_ACTIVE_BG       (Display_GetTheme()->main_special_function_button_active_bg)
#define MAIN_ALERT_BADGE_TEXT_COLOUR                (Display_GetTheme()->main_alert_badge_text_colour)
#define MAIN_SPECIAL_FUNCTION_BUTTON_INACTIVE_BG     DISPLAY_BG_COLOUR
#define MAIN_SPECIAL_FUNCTION_BUTTON_PREFIX_COLOUR   MAIN_INFO_TEXT_COLOUR
#define MAIN_SPECIAL_FUNCTION_BUTTON_PREFIX_BG       MAIN_INFO_TEXT_BG_COLOUR
#define MAIN_SPECIAL_FUNCTION_BUTTON_BORDER_COLOUR   DISPLAY_BG_COLOUR

#define BPM_INTERNAL_COLOUR                       (Display_GetTheme()->bpm_internal_colour)
#define BPM_BG_COLOUR                             DISPLAY_BG_COLOUR
#define EXT_BPM_COLOUR                            (Display_GetTheme()->ext_bpm_colour)
#define BPM_SYNC_LOST_COLOUR                      RED
#define TRANSPORT_BARBEAT_TEXT_COLOUR             MAIN_PRESET_COLOUR

#endif /* DISPLAY_THEME_H */
