#ifndef DISPLAY_THEME_H
#define DISPLAY_THEME_H

#include <stdint.h>

#include "runtime_config.h"
#include "st7796.h"

typedef struct
{
	uint16_t display_bg_colour;
	uint16_t main_footbar_color;
	uint16_t main_footbar_text_colour;
	uint16_t main_info_text_colour;
	uint16_t main_info_edit_cursor_text_colour;
	uint16_t main_info_edit_cursor_bg_colour;
	uint16_t main_info_edit_cursor_shared_bg_colour;
	uint16_t main_saving_popup_bg_colour;
	uint16_t main_saving_popup_text_colour;
	uint16_t main_saving_popup_border_colour;
	uint16_t main_mode_header_colour;
	uint16_t main_mode_header_edit_colour;
	uint16_t main_mode_header_edit_bg_colour;
	uint16_t main_preset_colour;
	uint16_t main_bank_colour;
	uint16_t main_bank_wet_dry_colour;
	uint16_t main_special_function_button_active_colour;
	uint16_t main_special_function_button_inactive_colour;
	uint16_t main_special_function_button_active_bg;
	uint16_t main_alert_badge_text_colour;
	uint16_t bpm_internal_colour;
	uint16_t ext_bpm_colour;
	uint16_t bpm_sync_lost_colour;
} DisplayTheme_t;

/* Edit the palette values in display_theme.c to tweak the UI appearance. */
const DisplayTheme_t *Display_GetTheme(void);
const char *Display_GetThemeName(RuntimeConfigDisplayMode_t display_mode);
const char *Display_GetThemePreviewName(RuntimeConfigDisplayMode_t display_mode);
uint16_t Display_GetThemePreviewColour(RuntimeConfigDisplayMode_t display_mode);

#define DISPLAY_BG_COLOUR                          (Display_GetTheme()->display_bg_colour)

#define MAIN_FOOTBAR_COLOR                         (Display_GetTheme()->main_footbar_color)
#define MAIN_FOOTBAR_TEXT_COLOUR                   (Display_GetTheme()->main_footbar_text_colour)

#define MAIN_INFO_TEXT_COLOUR                      (Display_GetTheme()->main_info_text_colour)
#define MAIN_INFO_TEXT_BG_COLOUR                   DISPLAY_BG_COLOUR
#define MAIN_INFO_SHARED_TEXT_COLOUR               DISPLAY_BG_COLOUR
#define MAIN_INFO_SHARED_BG_COLOUR                 MAIN_INFO_TEXT_COLOUR
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
#define BPM_SYNC_LOST_COLOUR                      (Display_GetTheme()->bpm_sync_lost_colour)
#define TRANSPORT_BARBEAT_TEXT_COLOUR             MAIN_PRESET_COLOUR

#endif /* DISPLAY_THEME_H */
