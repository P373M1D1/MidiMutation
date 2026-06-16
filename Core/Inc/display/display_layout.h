#ifndef DISPLAY_LAYOUT_H
#define DISPLAY_LAYOUT_H

#include "display/display_theme.h"
#include "fonts.h"
#include "presets.h"
#include "runtime_config.h"
#include "st7796.h"

/* Shared display geometry and text/layout constants.
 *
 * This is the single place for screen coordinates, row counts, badge widths,
 * and other layout math used by multiple display modules. Theme colours/fonts
 * come from display_theme.h; strings belong in display_strings.h. */

/* Main screen footer and info-row geometry. */
#define MAIN_FOOTBAR_Y                 298U
#define MAIN_FOOTBAR_H                 (ST7796_HEIGHT - MAIN_FOOTBAR_Y)
#define MAIN_FOOTBAR_FONT              (*Display_GetThemeFootbarFont())
#define MAIN_FOOTBAR_SECTION_COUNT     3U
#define MAIN_FOOTBAR_SECTION_WIDTH     (ST7796_WIDTH / MAIN_FOOTBAR_SECTION_COUNT)

#define MAIN_INFO_LEFT_X               30U
#define MAIN_INFO_RIGHT_X              235U
#define MAIN_INFO_FONT                 (*Display_GetThemeInfoFont())
#define MAIN_INFO_FONT_CELL_WIDTH      (MAIN_INFO_FONT.width)
#define MAIN_INFO_FONT_CELL_HEIGHT     (MAIN_INFO_FONT.height)
#define MAIN_INFO_ROW_COUNT            3U
#define MAIN_INFO_PROGRAM_DIGITS       3U
#define MAIN_INFO_CC_CHANNEL_DIGITS    2U
#define MAIN_INFO_CC_VALUE_DIGITS      3U
#define MAIN_INFO_CC_LABEL_DIGITS      ((PRESET_CC_SLOT_COUNT >= 100U) ? 3U : ((PRESET_CC_SLOT_COUNT >= 10U) ? 2U : 1U))
#define MAIN_INFO_CC_LABEL_CHARS       (2U + MAIN_INFO_CC_LABEL_DIGITS + 2U)
#define MAIN_INFO_PRESET_INIT_ROW_INDEX (PRESET_DEVICE_SLOTS + PRESET_CC_SLOT_COUNT)
#define MAIN_INFO_EDIT_FIELD_COUNT     (1U + PRESET_DEVICE_SLOTS + 1U + (PRESET_CC_SLOT_COUNT * 3U) + 1U)
#define MAIN_INFO_SHARED_PAD_CHARS     2U
#define MAIN_UNUSED_PROGRAM            0xFFU
#define MAIN_SAVING_POPUP_ROW_INDEX    1U
#define MAIN_SCROLL_INDICATOR_X        8U
#define MAIN_SCROLL_INDICATOR_W        9U
#define MAIN_SCROLL_INDICATOR_H        5U
#define MAIN_MODE_HEADER_TEXT_Y        10U
#define MAIN_MODE_HEADER_TEXT_CHARS    4U
#define MAIN_MODE_HEADER_EDIT_TEXT_CHARS 6U
#define MAIN_MODE_HEADER_MENU_PAD_CHARS 2U
#define MAIN_MODE_HEADER_MAX_TEXT_CHARS  12U
#define MAIN_MODE_HEADER_FONT          MAIN_FOOTBAR_FONT
#define MAIN_MODE_HEADER_CLEAR_W       (MAIN_MODE_HEADER_MAX_TEXT_CHARS * MAIN_MODE_HEADER_FONT.width)
#define MAIN_MODE_HEADER_CLEAR_X       ((uint16_t)((ST7796_WIDTH - MAIN_MODE_HEADER_CLEAR_W) / 2U))

/* Main title area and top BPM strip geometry. */
#define BPM_FONT                       MAIN_INFO_FONT
#define BPM_TEXT_Y                     7U
#define BPM_INTERNAL_X                 365U

#define MAIN_PRESET_TEXT_Y             85U
#define MAIN_PRESET_TEXT_CHARS         20U
#define MAIN_PRESET_FONT               (*Display_GetThemePresetFont())
#define MAIN_PRESET_FONT_CELL_WIDTH    (MAIN_PRESET_FONT.width)
#define MAIN_PRESET_FONT_CELL_HEIGHT   (MAIN_PRESET_FONT.height)
#define MAIN_PRESET_ROW_BUFFER_WIDTH   (PRESET_NAME_LENGTH * MAIN_PRESET_FONT_CELL_WIDTH)

#define MAIN_BANK_TEXT_Y               145U
#define MAIN_BANK_TEXT_CHARS           PRESET_BANK_NAME_MAXLEN
#define MAIN_BANK_FONT                 MAIN_INFO_FONT
#define MAIN_BANK_WET_DRY_BADGE_TEXT   "W/D"

#define BPM_DISPLAY_AREA_X              280U
#define BPM_DISPLAY_AREA_W              200U
#define BPM_SYNC_LOST_X                 280U
#define BPM_INTERNAL_VALUE_X            365U
#define BPM_INTERNAL_VALUE_W            (BPM_FONT.width * 3U)
#define BPM_INTERNAL_SUFFIX_X           (BPM_INTERNAL_VALUE_X + BPM_INTERNAL_VALUE_W)
#define BPM_EXT_PREFIX_X                305U
#define BPM_EXT_VALUE_X                 365U
#define BPM_EXT_VALUE_W                 (BPM_FONT.width * 3U)
#define BPM_EXT_SUFFIX_X                (BPM_EXT_VALUE_X + BPM_EXT_VALUE_W)
#define BPM_EXT_HYSTERESIS_MIN_X10      1U
#define BPM_EXT_HYSTERESIS_BPS          20U
#define BPM_EXT_UPDATE_MIN_INTERVAL_MS  750U
#define BPM_EXT_DISPLAY_MIN_X10         200U
#define BPM_EXT_DISPLAY_MAX_X10         2400U
#define BPM_INTERNAL_HEAD_TEXT_CHARS    7U
#define BPM_INTERNAL_SUFFIX_TEXT        " BPM"
#define BPM_INTERNAL_SUFFIX_TEXT_CHARS  4U
#define BPM_INTERNAL_SUFFIX_TEXT_X      ((uint16_t)(BPM_DISPLAY_AREA_X + BPM_DISPLAY_AREA_W - (BPM_INTERNAL_SUFFIX_TEXT_CHARS * BPM_FONT.width)))
#define BPM_INTERNAL_HEAD_TEXT_X        (BPM_INTERNAL_SUFFIX_TEXT_X - (BPM_INTERNAL_HEAD_TEXT_CHARS * BPM_FONT.width))
#define BPM_INTERNAL_HEAD_AREA_W        (BPM_INTERNAL_HEAD_TEXT_CHARS * BPM_FONT.width)
#define BPM_EXT_TEXT_CHARS              13U
#define BPM_EXT_TEXT_X                  ((uint16_t)(BPM_DISPLAY_AREA_X + BPM_DISPLAY_AREA_W - (BPM_EXT_TEXT_CHARS * BPM_FONT.width)))
#define BPM_EXT_HIGH_TEXT               "EXT HIGH"
#define BPM_EXT_LOW_TEXT                "EXT LOW"
#define BPM_SYNC_LOST_TEXT              "EXT SYNC LOST"

#define TRANSPORT_STATUS_AREA_X         7U
#define TRANSPORT_STATUS_AREA_Y         BPM_TEXT_Y
#define TRANSPORT_STATUS_AREA_W         (MAIN_MODE_HEADER_CLEAR_X - TRANSPORT_STATUS_AREA_X)
#define TRANSPORT_STATUS_AREA_H         MAIN_PRESET_FONT.height
#define TRANSPORT_STATUS_MESSAGE_FONT   BPM_FONT

#define TRANSPORT_BARBEAT_TEXT_X        TRANSPORT_STATUS_AREA_X
#define TRANSPORT_BARBEAT_TEXT_Y        TRANSPORT_STATUS_AREA_Y
#define TRANSPORT_BARBEAT_TEXT_CHARS    4U
#define TRANSPORT_BARBEAT_TEXT_W        (TRANSPORT_BARBEAT_TEXT_CHARS * MAIN_PRESET_FONT.width)

/* Startup/loading screen geometry; intentionally independent from runtime themes. */
#define LOADING_BAR_X                   10U
#define LOADING_BAR_Y                   262U
#define LOADING_BAR_W                   460U
#define LOADING_BAR_H                   28U
#define LOADING_BAR_TEXT_Y              246U
#define LOADING_BAR_TEXT_FONT           Font_7x10
#define LOADING_BAR_PHASE_DIVISOR       3U
#define LOADING_BAR_PHASE_HOLD_MS       1000U
#define LOADING_BAR_WAIT_TEXT           "... waiting for DNA match"
#define LOADING_BAR_MATCH_TEXT          "DNA match found"
#define LOADING_BAR_MARKERS_TEXT        "..accessing genetic markers"
#define LOADING_BAR_DONE_TEXT           "mutation complete"

/* Shared menu shell row counts and logical item totals. */
#define MENU_ROOT_ITEM_COUNT            5U
#define MENU_VISIBLE_ROW_COUNT          4U
#ifndef MENU_GLOBAL_ITEM_COUNT
#define MENU_GLOBAL_ITEM_COUNT          12U
#endif
#define MENU_USER_THEME_ITEM_COUNT      RUNTIME_CONFIG_USER_THEME_FIELD_COUNT
#define MENU_BANK_EDIT_ITEM_COUNT       4U
#define MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT 3U
#define MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT
#define MENU_FUNCTION_BUTTON_CC_FIRST_INDEX (MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX + RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT)
#define MENU_FUNCTION_BUTTON_ITEM_COUNT (MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT + MENU_FUNCTION_BUTTON_MESSAGE_ROW_COUNT)
#define MENU_DEVICE_EDIT_ITEM_COUNT     13U

#endif /* DISPLAY_LAYOUT_H */
