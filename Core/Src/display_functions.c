#include "display_functions.h"
#include "display/display_internal.h"
#include "display/display_compose_helpers.h"
#include "display/display_main_title.h"
#include "display/display_menu_page_bank_edit.h"
#include "display/display_menu_page_banks.h"
#include "display/display_menu_page_device_edit.h"
#include "display/display_menu_page_devices.h"
#include "display/display_menu_page_function_button.h"
#include "display/display_menu_page_global.h"
#include "display/display_menu_pages.h"
#include "display/display_menu_row_render.h"
#include "display/display_row_compose.h"
#include "display/display_theme.h"
#include "display/display_value_helpers.h"
#include "app/app_special_functions.h"
#include "midi/midi_monitor.h"
#include "midi_devices.h"
#include "runtime_config.h"
#include "st7796.h"
#include "fonts.h"
#include <stdio.h>
#include <string.h>

/* Main display orchestration module.
 *
 * The display subsystem has been split into focused helpers under Core/Src/display,
 * but this file still owns the remaining shared state, the main-screen layout
 * constants, and the glue that ties the extracted modules together.
 *
 * When revisiting display code later, check the extracted modules first:
 *   - display_theme.c            theme registry and palette/font selection
 *   - display_loading_bar.c      startup progress UI
 *   - display_status_strip.c     BPM and transport strip
 *   - display_backlight.c        DAC backlight control
 *   - display_screensaver.c      inactivity dim/wake logic
 *   - display_menu_*.c           menu navigation, rendering, and value edits
 *   - display_main_title.c       preset and bank title lines
 *
 * Edit this file when you need to change shared display_state ownership,
 * remaining main-screen composition glue, or constants that several modules
 * still intentionally share. Theme colours should come from display_theme.h
 * macros rather than new hard-coded literals. */

/* ?????? Screen layout constants ????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */
#define MAIN_FOOTBAR_Y                 298U                  // top edge of the footer/status bar
#define MAIN_FOOTBAR_H                 (ST7796_HEIGHT - MAIN_FOOTBAR_Y) // footer height from its top edge to screen bottom
#define MAIN_FOOTBAR_FONT              (*Display_GetThemeFootbarFont())    // font used for the footer caption
#define MAIN_FOOTBAR_SECTION_COUNT     3U                    // footer is conceptually split into three unlabeled regions
#define MAIN_FOOTBAR_SECTION_WIDTH     (ST7796_WIDTH / MAIN_FOOTBAR_SECTION_COUNT) // width of one footer region
#define MAIN_FOOTBAR_LEFT_TEXT         "SCROLL / EDIT"              // label for the left footer region during normal operation
#define MAIN_FOOTBAR_CENTER_TEXT       "PRESET / BANK"               // label for the middle footer region during normal operation
#define MAIN_FOOTBAR_RIGHT_TEXT        "TEMPO / MENU"               // label for the right footer region during normal operation
#define MAIN_FOOTBAR_EDIT_LEFT_TEXT    "SELECT / ENTER"                    // label for the left footer region while preset edit mode is active
#define MAIN_FOOTBAR_EDIT_CENTER_TEXT  "LEARN"                      // label for the middle footer region while preset edit mode is active
#define MAIN_FOOTBAR_EDIT_RIGHT_TEXT   "VALUE / EXIT"              // label for the right footer region while preset edit mode is active
#define MAIN_FOOTBAR_MENU_LEFT_TEXT    "NAV / BACK"               // label for the left footer region while menu mode is active
#define MAIN_FOOTBAR_MENU_CENTER_TEXT  "HOME"                     // label for the middle footer region while menu mode is active
#define MAIN_FOOTBAR_MENU_RIGHT_TEXT   "VALUE / ENTER"            // label for the right footer region while menu mode is active
#define MAIN_FOOTBAR_CONFIRM_LEFT_TEXT  ""                        // left footer label while a confirm page is active (unused)
#define MAIN_FOOTBAR_CONFIRM_CENTER_TEXT "YES"                    // center footer label while a confirm page is active
#define MAIN_FOOTBAR_CONFIRM_RIGHT_TEXT "NO"                      // right footer label while a confirm page is active
#define MAIN_INFO_LEFT_X               30U                  // x origin of the left info column (MIDI programs)
#define MAIN_INFO_RIGHT_X              235U                 // x origin of the right info column (relay / special state), shifted right by one glyph cell
#define MAIN_INFO_FONT                 (*Display_GetThemeInfoFont())   // font used for bank text, BPM text, and info rows
#define MAIN_INFO_FONT_CELL_WIDTH      (MAIN_INFO_FONT.width)                  // compile-time width of MAIN_INFO_FONT glyph cells for row-buffer composition
#define MAIN_INFO_FONT_CELL_HEIGHT     (MAIN_INFO_FONT.height)                  // compile-time height of MAIN_INFO_FONT glyph cells for row-buffer composition
#define MAIN_INFO_ROW_COUNT            3U                   // number of vertically stacked info rows currently visible on the main screen
#define MAIN_INFO_PROGRAM_DIGITS       3U                   // fixed width of the displayed MIDI program number
#define MAIN_INFO_CC_CHANNEL_DIGITS    2U                   // fixed width of the displayed MIDI CC channel number
#define MAIN_INFO_CC_VALUE_DIGITS      3U                   // fixed width of the displayed MIDI CC number/value fields
#define MAIN_INFO_CC_LABEL_DIGITS      ((PRESET_CC_SLOT_COUNT >= 100U) ? 3U : ((PRESET_CC_SLOT_COUNT >= 10U) ? 2U : 1U)) // digit width reserved for labels like "CC 8"
#define MAIN_INFO_CC_LABEL_CHARS       (2U + MAIN_INFO_CC_LABEL_DIGITS + 2U) // padded width of labels like "CC 8 "
#define MAIN_INFO_CC_CHANNEL_PREFIX    "CH: "              // label shown ahead of the CC channel value
#define MAIN_INFO_CC_NUMBER_PREFIX     " CC: "             // label shown ahead of the CC number value
#define MAIN_INFO_CC_VALUE_PREFIX      " Value: "          // label shown ahead of the CC value
#define MAIN_INFO_PRESET_INIT_ROW_INDEX (PRESET_DEVICE_SLOTS + PRESET_CC_SLOT_COUNT) // left-column row index of the preset reset action beneath the CC rows
#define MAIN_INFO_PRESET_INIT_TEXT     "INIT PRESET"      // action label shown after the CC rows in preset edit mode
#define MAIN_INFO_PRESET_INIT_CONFIRM_TEXT "INIT PRESET?" // confirmation prompt shown after selecting the preset reset action
#define MAIN_INFO_EDIT_FIELD_COUNT     (1U + PRESET_DEVICE_SLOTS + PRESET_RELAY_COUNT + 1U + (PRESET_CC_SLOT_COUNT * 3U) + 1U) // number of editable fields in preset edit mode, including the preset name, function button row, and preset reset action
#define MAIN_INFO_SHARED_PAD_CHARS     2U                   // extra chars cleared when special-function text shrinks
#define MAIN_UNUSED_PROGRAM            0xFFU                // sentinel meaning no MIDI program is assigned to that slot
#define MAIN_PROGRAM_WET_VALUE         127U                 // incoming Mix1/Mix2 CC value that represents 100% wet
#define MAIN_EMPTY_RIGHT_INFO_TEXT     "                "   // blank filler used to clear an unused right-side row
#define MAIN_SAVING_POPUP_TEXT         " SAVING "          // temporary overlay shown while preset edits are being committed to flash
#define MAIN_SAVING_POPUP_ROW_INDEX    1U                   // center the saving overlay on the middle info row
#define MAIN_TIMEBEND_POPUP_TEXT       " TIMEBEND ACTIVE " // live overlay shown when ENC2 controls outbound timebend
#define MAIN_TIMEBEND_POPUP_ROW_INDEX  1U                   // center the timebend overlay on the middle info row
#define MAIN_LEARNING_POPUP_TEXT       " LEARNING "        // transient overlay shown while preset-edit learn capture is active
#define MAIN_LEARNING_POPUP_ROW_INDEX  1U                   // center the learning overlay on the middle info row
#define MAIN_TIMEBEND_POPUP_TEXT_COLOUR BLACK               // fixed black text per UX requirement
#define MAIN_TIMEBEND_POPUP_BG_COLOUR   WHITE               // fixed white background per UX requirement
#define MAIN_TIMEBEND_POPUP_BORDER_COLOUR BLACK             // dark border to frame the white badge
#define MAIN_SCROLL_INDICATOR_X        8U                   // x position of the device-list scroll indicator triangles
#define MAIN_SCROLL_INDICATOR_W        9U                   // width of the scroll indicator triangles
#define MAIN_SCROLL_INDICATOR_H        5U                   // height of the scroll indicator triangles
#define MAIN_MODE_HEADER_TEXT_Y        10U                  // y position of the centered LIVE/EDIT mode label at the top of the screen
#define MAIN_MODE_HEADER_TEXT_CHARS    4U                   // width reserved for the centered top mode label
#define MAIN_MODE_HEADER_EDIT_TEXT_CHARS 6U                 // wider badge width for EDIT so the yellow background has one padded cell on each side
#define MAIN_MODE_HEADER_MENU_PAD_CHARS 2U                  // padded cells added to menu header badges so they match the EDIT badge style
#define MAIN_MODE_HEADER_MAX_TEXT_CHARS  10U                // widest header badge footprint that must be cleared between mode changes
#define MAIN_MODE_HEADER_FONT          MAIN_FOOTBAR_FONT    // font used for the top mode label
#define MAIN_PRESET_TEXT_Y             85U                  // y position of the large preset name line
#define MAIN_PRESET_TEXT_CHARS         20U                  // fixed character width used when centering preset names
#define MAIN_PRESET_FONT               (*Display_GetThemePresetFont())   // large font for the preset name
#define MAIN_PRESET_FONT_CELL_WIDTH    (MAIN_PRESET_FONT.width)                  // compile-time width of MAIN_PRESET_FONT glyph cells for row-buffer composition
#define MAIN_PRESET_FONT_CELL_HEIGHT   (MAIN_PRESET_FONT.height)                  // compile-time height of MAIN_PRESET_FONT glyph cells for row-buffer composition
#define MAIN_PRESET_ROW_BUFFER_WIDTH   (PRESET_NAME_LENGTH * MAIN_PRESET_FONT_CELL_WIDTH) // total pixel width of the preset-name row buffer
#define MAIN_BANK_TEXT_Y               145U                 // y position of the bank name line
#define MAIN_BANK_TEXT_CHARS           PRESET_BANK_NAME_MAXLEN // fixed character width used when centering bank names
#define MAIN_BANK_FONT                 MAIN_INFO_FONT   // font for the bank name line
#define MAIN_BANK_WET_DRY_BADGE_TEXT   "W/D"               // badge shown after the bank name when Wet/Dry mode is enabled for that bank
#define MAIN_SPECIAL_FUNCTION_BUTTON_DEFAULT_NAME       "SpcBtn" // fallback label shown ahead of the special-function-button state
#define MAIN_SPECIAL_FUNCTION_BUTTON_DEFAULT_ACTIVE_TEXT "active" // fallback text shown when the special-function button mode is active
#define MAIN_SPECIAL_FUNCTION_BUTTON_DEFAULT_INACTIVE_TEXT "bypass"   // fallback text shown when the special-function button mode is inactive
#define MAIN_INFO_HIGHLIGHT_BORDER_H   2U                   // thickness of the top and bottom highlight bars around active state text

#define MENU_ROOT_ITEM_COUNT            5U                   // number of top-level entries currently shown in the menu shell
#define MENU_VISIBLE_ROW_COUNT          4U                   // number of menu rows visible at one time in the current shell layout
#define MENU_BANK_EDIT_ITEM_COUNT       4U                   // number of items on the bank edit page
#define MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT 3U              // number of editable text rows before the compare table starts
#define MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT // first logical row index of the compare table
#define MENU_FUNCTION_BUTTON_CC_FIRST_INDEX (MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX + RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT) // first logical row index of the CC compare section
#define MENU_FUNCTION_BUTTON_ITEM_COUNT (MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT + MENU_FUNCTION_BUTTON_MESSAGE_ROW_COUNT) // total rows in the combined function-button editor page
#define MENU_FUNCTION_BUTTON_MESSAGE_ROW_COUNT (RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT + RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT) // total rows shown on the dense function-button message pages
#define MENU_DEVICE_EDIT_ITEM_COUNT     13U                  // number of items on the device edit page
#define MENU_ITEM_X                     24U                  // left edge of the menu row content area
#define MENU_ITEM_W                     (ST7796_WIDTH - (MENU_ITEM_X * 2U)) // width of the menu row content area
#define MENU_PLACEHOLDER_TEXT           "COMING SOON"       // placeholder body text for menu branches not implemented yet

static const uint16_t main_info_row_y[MAIN_INFO_ROW_COUNT] = {184U, 220U, 256U};

#define BPM_FONT                       MAIN_INFO_FONT   // font used for all BPM display text
#define BPM_TEXT_Y                     7U                   // y position of the BPM line at the top of the screen
#define BPM_INTERNAL_X                 365U                 // legacy anchor for internal BPM placement

DisplayState display_state = {
    .main_layout_dirty = 1U,
    .menu_page = (uint8_t)DISPLAY_MENU_PAGE_ROOT,
    .menu_last_drawn_page = (uint8_t)DISPLAY_MENU_PAGE_ROOT,
    .menu_text_edit_field = (uint8_t)DISPLAY_MENU_TEXT_FIELD_NONE,
};

static uint8_t timebend_popup_visible = 0U;
static uint8_t learning_popup_visible = 0U;

/* Legacy compatibility aliases.
 *
 * The extracted display modules still read/write pieces of display_state using
 * the original short names, so these macros keep the old call sites readable
 * while the state remains centrally owned here. */
#define main_layout_dirty                             (display_state.main_layout_dirty)
#define bpm_display_valid                             (display_state.bpm_display_valid)
#define bpm_display_external                          (display_state.bpm_display_external)
#define bpm_display_sync_lost                         (display_state.bpm_display_sync_lost)
#define bpm_display_value_x10                         (display_state.bpm_display_value_x10)
#define bpm_display_external_update_tick              (display_state.bpm_display_external_update_tick)
#define transport_barbeat_text                        (display_state.transport_barbeat_text)
#define main_info_first_slot                          (display_state.main_info_first_slot)
#define preset_edit_mode_active                       (display_state.preset_edit_mode_active)
#define preset_edit_cursor_index                      (display_state.preset_edit_cursor_index)
#define preset_name_edit_active                       (display_state.preset_name_edit_active)
#define preset_name_edit_cursor_index                 (display_state.preset_name_edit_cursor_index)
#define saving_popup_visible                          (display_state.saving_popup_visible)
#define menu_mode_active                              (display_state.menu_mode_active)
#define menu_preview_active                           (display_state.menu_preview_active)
#define preset_init_confirm_active                    (display_state.preset_init_confirm_active)
#define menu_root_selection_index                     (display_state.menu_root_selection_index)
#define menu_bank_selection_index                     (display_state.menu_bank_selection_index)
#define menu_active_bank_index                        (display_state.menu_active_bank_index)
#define menu_bank_edit_selection_index                (display_state.menu_bank_edit_selection_index)
#define menu_function_button_selection_index          (display_state.menu_function_button_selection_index)
#define menu_device_selection_index                   (display_state.menu_device_selection_index)
#define menu_active_device_index                      (display_state.menu_active_device_index)
#define menu_device_edit_selection_index              (display_state.menu_device_edit_selection_index)
#define menu_device_cc_field_index                    (display_state.menu_device_cc_field_index)
#define menu_device_cc_field_edit_active              (display_state.menu_device_cc_field_edit_active)
#define menu_global_selection_index                   (display_state.menu_global_selection_index)
#define menu_function_button_message_selection_index  (display_state.menu_function_button_message_selection_index)
#define menu_function_button_message_field_index      (display_state.menu_function_button_message_field_index)
#define menu_function_button_message_field_edit_active (display_state.menu_function_button_message_field_edit_active)
#define menu_page                                     display_state.menu_page
#define menu_last_drawn_page                          display_state.menu_last_drawn_page
#define menu_text_edit_field                          display_state.menu_text_edit_field
#define menu_text_edit_cursor_index                   (display_state.menu_text_edit_cursor_index)
#define menu_draw_state_valid                         (display_state.menu_draw_state_valid)

void Display_ClearMainLayoutDirty(void)
{
    main_layout_dirty = 0U;
}

#define BPM_DISPLAY_AREA_X              280U                 // left edge of the rectangle reserved for BPM text updates
#define BPM_DISPLAY_AREA_W              200U                 // width of the rectangle reserved for BPM text updates
#define BPM_SYNC_LOST_X                 280U                 // x position of the EXT SYNC LOST message
#define BPM_INTERNAL_VALUE_X            365U                 // x position of the internal BPM number block
#define BPM_INTERNAL_VALUE_W            (BPM_FONT.width * 3U) // width reserved for the 3-digit internal BPM number
#define BPM_INTERNAL_SUFFIX_X           (BPM_INTERNAL_VALUE_X + BPM_INTERNAL_VALUE_W) // x position where the internal BPM suffix would begin
#define BPM_EXT_PREFIX_X                305U                 // legacy anchor for the external BPM prefix
#define BPM_EXT_VALUE_X                 365U                 // legacy anchor for the external BPM number block
#define BPM_EXT_VALUE_W                 (BPM_FONT.width * 3U) // width reserved for the external BPM number block
#define BPM_EXT_SUFFIX_X                (BPM_EXT_VALUE_X + BPM_EXT_VALUE_W) // x position where the external BPM suffix would begin
#define BPM_EXT_HYSTERESIS_MIN_X10      1U                   // minimum external BPM deadband in tenths of BPM
#define BPM_EXT_HYSTERESIS_BPS          20U                  // external BPM deadband as basis points of the current reading
#define BPM_EXT_UPDATE_MIN_INTERVAL_MS  500U                 // minimum time between small external BPM redraws
#define BPM_EXT_FORCE_UPDATE_DELTA_X10  5U                   // delta in tenths that forces an external BPM update
#define BPM_EXT_SLEW_STEP_X10           1U                   // maximum smoothing step per update in tenths of BPM
#define BPM_INTERNAL_HEAD_TEXT_CHARS    7U                   // width reserved for internal prefix+value, e.g. "INT 120"
#define BPM_INTERNAL_SUFFIX_TEXT        " BPM"              // fixed suffix for internal BPM display
#define BPM_INTERNAL_SUFFIX_TEXT_CHARS  4U                   // width of the fixed internal suffix text block
#define BPM_INTERNAL_SUFFIX_TEXT_X      ((uint16_t)(BPM_DISPLAY_AREA_X + BPM_DISPLAY_AREA_W - (BPM_INTERNAL_SUFFIX_TEXT_CHARS * BPM_FONT.width))) // right-aligned x position of the fixed internal suffix
#define BPM_INTERNAL_HEAD_TEXT_X        (BPM_INTERNAL_SUFFIX_TEXT_X - (BPM_INTERNAL_HEAD_TEXT_CHARS * BPM_FONT.width)) // left edge of the full internal prefix+value area
#define BPM_INTERNAL_HEAD_AREA_W        (BPM_INTERNAL_HEAD_TEXT_CHARS * BPM_FONT.width) // pixel width of the full internal prefix+value area
#define BPM_EXT_TEXT_CHARS              13U                  // padded text width for external BPM strings like "EXT 120.0 BPM"
#define BPM_EXT_TEXT_X                  ((uint16_t)(BPM_DISPLAY_AREA_X + BPM_DISPLAY_AREA_W - (BPM_EXT_TEXT_CHARS * BPM_FONT.width))) // right-aligned x position of the external BPM string
#define BPM_SYNC_LOST_TEXT              "EXT SYNC LOST"      // message shown when external MIDI clock times out

#define TRANSPORT_BARBEAT_TEXT_X        10U                  // top-left x position for bar.beat transport readout
#define TRANSPORT_BARBEAT_TEXT_Y        7U                   // y position for the bar.beat transport readout
#define TRANSPORT_BARBEAT_TEXT_CHARS    4U                   // fixed width for values like "64.4" or "-.-"
#define TRANSPORT_BARBEAT_TEXT_W        (TRANSPORT_BARBEAT_TEXT_CHARS * MAIN_PRESET_FONT.width) // clear/update width of bar.beat readout

#define LOADING_BAR_X                   10U                  // left edge of the startup loading bar
#define LOADING_BAR_Y                   262U                 // top edge of the startup loading bar
#define LOADING_BAR_W                   460U                 // total drawable width of the startup loading bar
#define LOADING_BAR_H                   28U                  // height of the startup loading bar
#define LOADING_BAR_TEXT_Y              246U                // y position of the loading-bar status text line
#define LOADING_BAR_TEXT_FONT           Font_7x10           // font used for loading-bar status text
#define LOADING_BAR_PHASE_DIVISOR       3U                  // point where the first status-text phase change triggers
#define LOADING_BAR_PHASE_HOLD_MS       1000U               // time each loading-bar status message is held on screen
#define LOADING_BAR_WAIT_TEXT           "... waiting for DNA match" // first startup loading-bar message
#define LOADING_BAR_MATCH_TEXT          "DNA match found"  // second startup loading-bar message
#define LOADING_BAR_MARKERS_TEXT        "..accessing genetic markers" // third startup loading-bar message
#define LOADING_BAR_DONE_TEXT           "mutation complete" // final message shown when startup loading completes

static void Display_FormatSpecialFunctionPrefix(char *buffer, size_t buffer_size)
{
    const RuntimeConfigFunctionButton_t *function_button = Presets_GetActiveFunctionButton();
    const char *name = MAIN_SPECIAL_FUNCTION_BUTTON_DEFAULT_NAME;

    if (function_button && function_button->name[0] != '\0')
        name = function_button->name;

    if (buffer_size == 0U)
        return;

    (void)snprintf(buffer, buffer_size, "%s: ", name);
}

static const char *Display_GetSpecialFunctionStateLabel(uint8_t state_active)
{
    const RuntimeConfigFunctionButton_t *function_button = Presets_GetActiveFunctionButton();

    if (!function_button)
        return state_active ? MAIN_SPECIAL_FUNCTION_BUTTON_DEFAULT_ACTIVE_TEXT
                            : MAIN_SPECIAL_FUNCTION_BUTTON_DEFAULT_INACTIVE_TEXT;

    if (state_active)
        return (function_button->active_label[0] != '\0')
            ? function_button->active_label
            : MAIN_SPECIAL_FUNCTION_BUTTON_DEFAULT_ACTIVE_TEXT;

    return (function_button->inactive_label[0] != '\0')
        ? function_button->inactive_label
        : MAIN_SPECIAL_FUNCTION_BUTTON_DEFAULT_INACTIVE_TEXT;
}

static DisplayPresetEditField_t Display_GetPresetEditFieldForCursor(uint8_t cursor_index)
{
    DisplayPresetEditField_t field = { DISPLAY_PRESET_EDIT_FIELD_NONE, 0U };

    if (cursor_index == 0U)
    {
        field.type = DISPLAY_PRESET_EDIT_FIELD_NAME;
        return field;
    }

    cursor_index = (uint8_t)(cursor_index - 1U);
    if (cursor_index < PRESET_DEVICE_SLOTS)
    {
        field.type = DISPLAY_PRESET_EDIT_FIELD_PROGRAM;
        field.itemIndex = cursor_index;
        return field;
    }

    cursor_index = (uint8_t)(cursor_index - PRESET_DEVICE_SLOTS);
    if (cursor_index < PRESET_RELAY_COUNT)
    {
        field.type = DISPLAY_PRESET_EDIT_FIELD_RELAY;
        field.itemIndex = cursor_index;
        return field;
    }

    cursor_index = (uint8_t)(cursor_index - PRESET_RELAY_COUNT);
    if (cursor_index == 0U)
    {
        field.type = DISPLAY_PRESET_EDIT_FIELD_FUNCTION_BUTTON;
        return field;
    }

    cursor_index = (uint8_t)(cursor_index - 1U);
    if (cursor_index >= (PRESET_CC_SLOT_COUNT * 3U))
    {
        field.type = DISPLAY_PRESET_EDIT_FIELD_INIT;
        return field;
    }

    field.itemIndex = (uint8_t)(cursor_index / 3U);

    switch (cursor_index % 3U)
    {
    case 0U:
        field.type = DISPLAY_PRESET_EDIT_FIELD_CC_CHANNEL;
        break;
    case 1U:
        field.type = DISPLAY_PRESET_EDIT_FIELD_CC_NUMBER;
        break;
    default:
        field.type = DISPLAY_PRESET_EDIT_FIELD_CC_VALUE;
        break;
    }

    return field;
}

static uint8_t Display_GetPresetEditScrollFirstSlot(uint8_t cursor_index)
{
    DisplayPresetEditField_t field = Display_GetPresetEditFieldForCursor(cursor_index);

    if (field.type == DISPLAY_PRESET_EDIT_FIELD_NAME)
        return 0U;

    if (field.type == DISPLAY_PRESET_EDIT_FIELD_PROGRAM)
    {
        if (field.itemIndex < MAIN_INFO_ROW_COUNT)
            return 0U;

        return (uint8_t)(field.itemIndex - (MAIN_INFO_ROW_COUNT - 1U));
    }

    if (field.type == DISPLAY_PRESET_EDIT_FIELD_RELAY)
    {
        return (PRESET_DEVICE_SLOTS > MAIN_INFO_ROW_COUNT)
            ? (uint8_t)(PRESET_DEVICE_SLOTS - MAIN_INFO_ROW_COUNT)
            : 0U;
    }

    if (field.type == DISPLAY_PRESET_EDIT_FIELD_FUNCTION_BUTTON)
    {
        return (PRESET_DEVICE_SLOTS > MAIN_INFO_ROW_COUNT)
            ? (uint8_t)(PRESET_DEVICE_SLOTS - MAIN_INFO_ROW_COUNT)
            : 0U;
    }

    if (field.type == DISPLAY_PRESET_EDIT_FIELD_INIT)
        return (uint8_t)((PRESET_DEVICE_SLOTS + PRESET_CC_SLOT_COUNT + 2U) - MAIN_INFO_ROW_COUNT);

    return (uint8_t)((PRESET_DEVICE_SLOTS - (MAIN_INFO_ROW_COUNT - 1U)) + field.itemIndex);
}

static uint8_t Display_PresetEditFieldsMatch(DisplayPresetEditField_t first,
                                             DisplayPresetEditField_t second)
{
    return (first.type == second.type && first.itemIndex == second.itemIndex) ? 1U : 0U;
}

static uint8_t Display_GetMainInfoProgramScrollMax(void)
{
    return (PRESET_DEVICE_SLOTS > MAIN_INFO_ROW_COUNT)
        ? (uint8_t)(PRESET_DEVICE_SLOTS - MAIN_INFO_ROW_COUNT)
        : 0U;
}

static uint8_t Display_GetMainInfoScrollMax(void)
{
    uint8_t total_rows = (uint8_t)(PRESET_DEVICE_SLOTS + PRESET_CC_SLOT_COUNT + 2U);

    return (total_rows > MAIN_INFO_ROW_COUNT)
        ? (uint8_t)(total_rows - MAIN_INFO_ROW_COUNT)
        : 0U;
}

static uint8_t Display_GetMainInfoRightFirstItemForFirstSlot(uint8_t first_slot)
{
    uint8_t program_scroll_max = Display_GetMainInfoProgramScrollMax();

    return (first_slot > program_scroll_max)
        ? (uint8_t)(first_slot - program_scroll_max)
        : 0U;
}

static uint8_t Display_GetMainInfoRightFirstItem(void)
{
    return Display_GetMainInfoRightFirstItemForFirstSlot(main_info_first_slot);
}

static void Display_ComposeMainInfoScrollIndicator(uint8_t point_up,
                                                   uint8_t visible)
{
    uint16_t indicator_y = (uint16_t)((MAIN_INFO_FONT.height - MAIN_SCROLL_INDICATOR_H) / 2U);

    if (!visible)
        return;

    for (uint8_t row = 0U; row < MAIN_SCROLL_INDICATOR_H; row++)
    {
        uint16_t line_y = point_up
            ? (uint16_t)(indicator_y + row)
            : (uint16_t)(indicator_y + (MAIN_SCROLL_INDICATOR_H - 1U - row));
        uint16_t line_x0 = (uint16_t)(MAIN_SCROLL_INDICATOR_X + ((MAIN_SCROLL_INDICATOR_W / 2U) - row));

        Display_ComposeFillRect(ST7796_WIDTH,
                    MAIN_INFO_FONT_CELL_HEIGHT,
                    line_x0,
                    line_y,
                    (uint16_t)((row * 2U) + 1U),
                    1U,
                    MAIN_SCROLL_INDICATOR_COLOUR);
    }
}

void Display_MenuRedrawCurrentItem(void);
void Display_RedrawMenuCurrentValueItem(DisplayMenuPage_t page, uint8_t item_index);
void Display_MenuRedrawCurrentValue(void);
void Display_MenuRedrawSelectionChange(DisplayMenuPage_t page, uint8_t previous_selection);
void Display_MenuRefreshBodyOnly(void);

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

static void Display_DrawMainInfoProgramRow(const Preset_t *preset,
                                           uint8_t slot_index,
                                           uint16_t row_y)
{
    char prefix[16];
    const MidiDevice_t *device = MidiDevices_Get(slot_index);
    const char *device_name = MidiDevices_GetName(slot_index);
    uint8_t channel = device ? device->channel : MAIN_UNUSED_PROGRAM;
    uint16_t prefix_chars = (uint16_t)(((RUNTIME_CONFIG_DEVICE_NAME_LENGTH + 2U) > strlen("CH 16: "))
        ? (RUNTIME_CONFIG_DEVICE_NAME_LENGTH + 2U)
        : strlen("CH 16: "));
    uint16_t value_x;

    (void)row_y;

    value_x = (uint16_t)(MAIN_INFO_LEFT_X
                       + (prefix_chars * MAIN_INFO_FONT.width));

    if (channel == MAIN_UNUSED_PROGRAM)
    {
        (void)snprintf(prefix, sizeof(prefix), "CH -:");
        for (size_t prefix_len = strlen(prefix); prefix_len < prefix_chars && prefix_len < (sizeof(prefix) - 1U); ++prefix_len)
        {
            prefix[prefix_len] = ' ';
            prefix[prefix_len + 1U] = '\0';
        }

        Display_MenuRowComposeTextSegment32(MAIN_INFO_LEFT_X,
                                            prefix,
                                            MAIN_INFO_TEXT_COLOUR,
                                            MAIN_INFO_TEXT_BG_COLOUR);
        Display_MenuRowComposeTextSegment32(value_x,
                                            "---",
                                            MAIN_INFO_TEXT_COLOUR,
                                            MAIN_INFO_TEXT_BG_COLOUR);
        return;
    }

    if (device_name && device_name[0] != '\0')
        snprintf(prefix, sizeof(prefix), "%s:", device_name);
    else
        snprintf(prefix, sizeof(prefix), "CH %u:", channel);

    for (size_t prefix_len = strlen(prefix); prefix_len < prefix_chars && prefix_len < (sizeof(prefix) - 1U); ++prefix_len)
    {
        prefix[prefix_len] = ' ';
        prefix[prefix_len + 1U] = '\0';
    }

    Display_MenuRowComposeTextSegment32(MAIN_INFO_LEFT_X,
                                        prefix,
                                        MAIN_INFO_TEXT_COLOUR,
                                        MAIN_INFO_TEXT_BG_COLOUR);

    {
        DisplayPresetEditField_t edit_field = Display_PresetEditGetField();
        uint8_t highlight_program = (edit_field.type == DISPLAY_PRESET_EDIT_FIELD_PROGRAM && edit_field.itemIndex == slot_index) ? 1U : 0U;
        char program_text[MAIN_INFO_PROGRAM_DIGITS + 1U];
        char wet_marker_text[3] = "  ";
        uint8_t program = preset->prg[slot_index].program;
        uint8_t program_is_shared = (program != MAIN_UNUSED_PROGRAM && Presets_DeviceProgramIsShared(slot_index, program)) ? 1U : 0U;
        uint16_t program_foreground = MAIN_INFO_TEXT_COLOUR;
        uint16_t program_background = MAIN_INFO_TEXT_BG_COLOUR;

        if (!preset_edit_mode_active && !menu_mode_active)
        {
            const RuntimeConfigDevice_t *device_config = RuntimeConfig_GetDevice(slot_index);
            uint8_t mix1_value = 0U;
            uint8_t mix2_value = 0U;
            uint8_t mix1_is_wet = 0U;
            uint8_t mix2_is_wet = 0U;

            if (device_config
             && channel != MAIN_UNUSED_PROGRAM)
            {
                if (device_config->mix1.cc != PRESET_CC_NUMBER_UNUSED
                 && MidiMonitor_TryGetLatestControlValueAnySource(channel,
                                                                  device_config->mix1.cc,
                                                                  &mix1_value)
                 && mix1_value == MAIN_PROGRAM_WET_VALUE)
                {
                    mix1_is_wet = 1U;
                }

                if (device_config->mix2.cc != PRESET_CC_NUMBER_UNUSED
                 && MidiMonitor_TryGetLatestControlValueAnySource(channel,
                                                                  device_config->mix2.cc,
                                                                  &mix2_value)
                 && mix2_value == MAIN_PROGRAM_WET_VALUE)
                {
                    mix2_is_wet = 1U;
                }
            }

            if (mix1_is_wet || mix2_is_wet)
            {
                (void)snprintf(wet_marker_text, sizeof(wet_marker_text), " W");
            }
        }

        if (program == MAIN_UNUSED_PROGRAM)
            strcpy(program_text, "---");
        else
            snprintf(program_text, sizeof(program_text), "%3u", program);

        if (highlight_program)
        {
            program_foreground = MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR;
            program_background = program_is_shared ? MAIN_INFO_EDIT_CURSOR_SHARED_BG_COLOUR : MAIN_INFO_EDIT_CURSOR_BG_COLOUR;
        }
        else if (program_is_shared)
        {
            program_foreground = MAIN_INFO_SHARED_TEXT_COLOUR;
            program_background = MAIN_INFO_SHARED_BG_COLOUR;
        }

        Display_MenuRowComposeTextSegment32(value_x,
                                            program_text,
                                            program_foreground,
                                            program_background);
        Display_MenuRowComposeTextSegment32((uint16_t)(value_x + (MAIN_INFO_PROGRAM_DIGITS * MAIN_INFO_FONT.width)),
                                            wet_marker_text,
                                            MAIN_INFO_TEXT_COLOUR,
                                            MAIN_INFO_TEXT_BG_COLOUR);
    }
}

static void Display_FormatMainInfoCcLabel(uint8_t cc_index,
                                          char *cc_label_text,
                                          size_t cc_label_text_size)
{
    int written;
    size_t text_len;

    if (!cc_label_text || cc_label_text_size == 0U)
        return;

    written = snprintf(cc_label_text, cc_label_text_size, "CC %u ", (unsigned)(cc_index + 1U));
    if (written < 0)
    {
        cc_label_text[0] = '\0';
        return;
    }

    text_len = strnlen(cc_label_text, cc_label_text_size - 1U);
    while (text_len < MAIN_INFO_CC_LABEL_CHARS && text_len < (cc_label_text_size - 1U))
        cc_label_text[text_len++] = ' ';

    cc_label_text[text_len] = '\0';
}

static void Display_DrawMainInfoCcLabel(uint8_t cc_index,
                                        uint16_t row_y)
{
    char cc_label_text[12];

    (void)row_y;

    Display_FormatMainInfoCcLabel(cc_index, cc_label_text, sizeof(cc_label_text));
    Display_MenuRowComposeTextSegment32(MAIN_INFO_LEFT_X,
                                        cc_label_text,
                                        MAIN_INFO_TEXT_COLOUR,
                                        MAIN_INFO_TEXT_BG_COLOUR);
}

static void Display_ComposeMainInfoCcField(const Preset_t *preset,
                                           uint8_t cc_index,
                                           DisplayPresetEditFieldType_t field_type,
                                           uint8_t highlight_field)
{
    const PresetCCSlot_t *cc;
    char field_text[MAIN_INFO_CC_VALUE_DIGITS + 1U];
    uint16_t field_x;
    uint16_t foreground = highlight_field ? MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR : MAIN_INFO_TEXT_COLOUR;
    uint16_t background = highlight_field ? MAIN_INFO_EDIT_CURSOR_BG_COLOUR : MAIN_INFO_TEXT_BG_COLOUR;

    if (!preset || cc_index >= PRESET_CC_SLOT_COUNT)
        return;

    {
        uint16_t field_chars = MAIN_INFO_CC_LABEL_CHARS;

        switch (field_type)
        {
        case DISPLAY_PRESET_EDIT_FIELD_CC_CHANNEL:
            field_chars = (uint16_t)(field_chars + strlen(MAIN_INFO_CC_CHANNEL_PREFIX));
            break;

        case DISPLAY_PRESET_EDIT_FIELD_CC_NUMBER:
            field_chars = (uint16_t)(field_chars
                                      + strlen(MAIN_INFO_CC_CHANNEL_PREFIX)
                                      + MAIN_INFO_CC_CHANNEL_DIGITS
                                      + strlen(MAIN_INFO_CC_NUMBER_PREFIX));
            break;

        case DISPLAY_PRESET_EDIT_FIELD_CC_VALUE:
            field_chars = (uint16_t)(field_chars
                                      + strlen(MAIN_INFO_CC_CHANNEL_PREFIX)
                                      + MAIN_INFO_CC_CHANNEL_DIGITS
                                      + strlen(MAIN_INFO_CC_NUMBER_PREFIX)
                                      + MAIN_INFO_CC_VALUE_DIGITS
                                      + strlen(MAIN_INFO_CC_VALUE_PREFIX));
            break;

        default:
            return;
        }

        field_x = (uint16_t)(MAIN_INFO_LEFT_X + (field_chars * MAIN_INFO_FONT.width));
    }

    cc = &preset->cc[cc_index];
    switch (field_type)
    {
    case DISPLAY_PRESET_EDIT_FIELD_CC_CHANNEL:
        if (cc->channel == PRESET_CC_CHANNEL_UNUSED)
            strcpy(field_text, "--");
        else
            snprintf(field_text, sizeof(field_text), "%2u", cc->channel);
        break;

    case DISPLAY_PRESET_EDIT_FIELD_CC_NUMBER:
        if (cc->cc_number == PRESET_CC_NUMBER_UNUSED)
            strcpy(field_text, "---");
        else
            snprintf(field_text, sizeof(field_text), "%3u", cc->cc_number);
        break;

    case DISPLAY_PRESET_EDIT_FIELD_CC_VALUE:
        if (cc->value == PRESET_CC_VALUE_UNUSED)
            strcpy(field_text, "---");
        else
            snprintf(field_text, sizeof(field_text), "%3u", cc->value);
        break;

    default:
        return;
    }

    Display_MenuRowComposeTextSegment32(field_x,
                                        field_text,
                                        foreground,
                                        background);
}

static void Display_ComposeMainInfoRelayField(const Preset_t *preset,
                                              uint8_t relay_index,
                                              uint8_t highlight_state)
{
    char state_text[8];
    uint16_t state_x;

    if (!preset || relay_index >= PRESET_RELAY_COUNT)
        return;

    snprintf(state_text, sizeof(state_text), "%-6s", preset->relay[relay_index] ? "closed" : "open");
    state_x = (uint16_t)(MAIN_INFO_RIGHT_X + (strlen("Relay_0: ") * MAIN_INFO_FONT.width));

    Display_MenuRowComposeTextSegment32(state_x,
                                        state_text,
                                        highlight_state ? MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR : MAIN_INFO_TEXT_COLOUR,
                                        highlight_state ? MAIN_INFO_EDIT_CURSOR_BG_COLOUR : MAIN_INFO_TEXT_BG_COLOUR);
}

static void Display_DrawMainInfoCcRow(const Preset_t *preset,
                                      uint8_t cc_index,
                                      uint16_t row_y)
{
    uint16_t draw_x = MAIN_INFO_LEFT_X;

    (void)row_y;

    Display_DrawMainInfoCcLabel(cc_index, row_y);
    draw_x = (uint16_t)(draw_x + (MAIN_INFO_CC_LABEL_CHARS * MAIN_INFO_FONT.width));

    Display_MenuRowComposeTextSegment32(draw_x,
                                        MAIN_INFO_CC_CHANNEL_PREFIX,
                                        MAIN_INFO_TEXT_COLOUR,
                                        MAIN_INFO_TEXT_BG_COLOUR);
    draw_x = (uint16_t)(draw_x + (strlen(MAIN_INFO_CC_CHANNEL_PREFIX) * MAIN_INFO_FONT.width));
    draw_x = (uint16_t)(draw_x + (MAIN_INFO_CC_CHANNEL_DIGITS * MAIN_INFO_FONT.width));

    Display_MenuRowComposeTextSegment32(draw_x,
                                        MAIN_INFO_CC_NUMBER_PREFIX,
                                        MAIN_INFO_TEXT_COLOUR,
                                        MAIN_INFO_TEXT_BG_COLOUR);
    draw_x = (uint16_t)(draw_x + (strlen(MAIN_INFO_CC_NUMBER_PREFIX) * MAIN_INFO_FONT.width));
    draw_x = (uint16_t)(draw_x + (MAIN_INFO_CC_VALUE_DIGITS * MAIN_INFO_FONT.width));

    Display_MenuRowComposeTextSegment32(draw_x,
                                        MAIN_INFO_CC_VALUE_PREFIX,
                                        MAIN_INFO_TEXT_COLOUR,
                                        MAIN_INFO_TEXT_BG_COLOUR);

    {
        DisplayPresetEditField_t edit_field = Display_PresetEditGetField();
        uint8_t highlight_channel = (edit_field.type == DISPLAY_PRESET_EDIT_FIELD_CC_CHANNEL && edit_field.itemIndex == cc_index) ? 1U : 0U;
        uint8_t highlight_cc_number = (edit_field.type == DISPLAY_PRESET_EDIT_FIELD_CC_NUMBER && edit_field.itemIndex == cc_index) ? 1U : 0U;
        uint8_t highlight_value = (edit_field.type == DISPLAY_PRESET_EDIT_FIELD_CC_VALUE && edit_field.itemIndex == cc_index) ? 1U : 0U;

        Display_ComposeMainInfoCcField(preset,
                                       cc_index,
                                       DISPLAY_PRESET_EDIT_FIELD_CC_CHANNEL,
                                       highlight_channel);
        Display_ComposeMainInfoCcField(preset,
                                       cc_index,
                                       DISPLAY_PRESET_EDIT_FIELD_CC_NUMBER,
                                       highlight_cc_number);
        Display_ComposeMainInfoCcField(preset,
                                       cc_index,
                                       DISPLAY_PRESET_EDIT_FIELD_CC_VALUE,
                                       highlight_value);
    }
}

static void Display_DrawMainInfoSpecialState(uint16_t row_y, uint8_t highlight_state)
{
    char prefix[RUNTIME_CONFIG_FUNCTION_BUTTON_NAME_LENGTH + 3U];
    uint8_t state_active = AppSpecialFunctions_IsActive();
    const char *state = Display_GetSpecialFunctionStateLabel(state_active);

    Display_FormatSpecialFunctionPrefix(prefix, sizeof(prefix));

    uint16_t prefix_px = MAIN_INFO_FONT.width * (uint16_t)strlen(prefix);
    uint16_t state_x = MAIN_INFO_RIGHT_X + prefix_px;
    uint16_t state_w = MAIN_INFO_FONT.width * (uint16_t)strlen(state);

    (void)row_y;

    Display_MenuRowComposeTextSegment32(MAIN_INFO_RIGHT_X,
                                        prefix,
                                        highlight_state ? MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR : MAIN_SPECIAL_FUNCTION_BUTTON_PREFIX_COLOUR,
                                        highlight_state ? MAIN_INFO_EDIT_CURSOR_BG_COLOUR : MAIN_SPECIAL_FUNCTION_BUTTON_PREFIX_BG);
    Display_MenuRowComposeTextSegment32(state_x,
                                        state,
                                        highlight_state ? MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR : (state_active ? MAIN_SPECIAL_FUNCTION_BUTTON_ACTIVE_COLOUR : MAIN_SPECIAL_FUNCTION_BUTTON_INACTIVE_COLOUR),
                                        highlight_state ? MAIN_INFO_EDIT_CURSOR_BG_COLOUR : (state_active ? MAIN_SPECIAL_FUNCTION_BUTTON_ACTIVE_BG : MAIN_SPECIAL_FUNCTION_BUTTON_INACTIVE_BG));

    if (!state_active || highlight_state)
        return;

    Display_ComposeFillRect(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            state_x,
                            0U,
                            state_w,
                            MAIN_INFO_HIGHLIGHT_BORDER_H,
                            MAIN_SPECIAL_FUNCTION_BUTTON_BORDER_COLOUR);
    Display_ComposeFillRect(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            state_x,
                            (uint16_t)(MAIN_INFO_FONT.height - MAIN_INFO_HIGHLIGHT_BORDER_H),
                            state_w,
                            MAIN_INFO_HIGHLIGHT_BORDER_H,
                            MAIN_SPECIAL_FUNCTION_BUTTON_BORDER_COLOUR);
}

static void Display_DrawMainInfoRightRow(const Preset_t *preset,
                                         uint8_t right_item_index,
                                         uint16_t row_y)
{
    (void)row_y;

    if (right_item_index < PRESET_RELAY_COUNT)
    {
        char prefix[16];
        DisplayPresetEditField_t edit_field = Display_PresetEditGetField();
        uint8_t highlight_state = (edit_field.type == DISPLAY_PRESET_EDIT_FIELD_RELAY && edit_field.itemIndex == right_item_index) ? 1U : 0U;

        snprintf(prefix, sizeof(prefix), "Relay_%u: ", right_item_index + 1U);

        Display_MenuRowComposeTextSegment32(MAIN_INFO_RIGHT_X,
                                            prefix,
                                            MAIN_INFO_TEXT_COLOUR,
                                            MAIN_INFO_TEXT_BG_COLOUR);
        Display_ComposeMainInfoRelayField(preset, right_item_index, highlight_state);
        return;
    }

    if (right_item_index == PRESET_RELAY_COUNT)
    {
        DisplayPresetEditField_t edit_field = Display_PresetEditGetField();
        uint8_t highlight_state = (edit_field.type == DISPLAY_PRESET_EDIT_FIELD_FUNCTION_BUTTON) ? 1U : 0U;

        Display_DrawMainInfoSpecialState(row_y, highlight_state);
    }
}

static void Display_DrawMainInfoLeftRow(const Preset_t *preset,
                                        uint8_t info_index,
                                        uint16_t row_y)
{
    (void)row_y;

    if (info_index < PRESET_DEVICE_SLOTS)
        Display_DrawMainInfoProgramRow(preset, info_index, row_y);
    else if (info_index < MAIN_INFO_PRESET_INIT_ROW_INDEX)
        Display_DrawMainInfoCcRow(preset, (uint8_t)(info_index - PRESET_DEVICE_SLOTS), row_y);
    else if (info_index == MAIN_INFO_PRESET_INIT_ROW_INDEX && Display_PresetEditIsActive())
        Display_ComposeCenteredBadgeRow(MAIN_INFO_PRESET_INIT_TEXT, MAIN_ALERT_BADGE_TEXT_COLOUR, RED);
}

static void Display_DrawMainInfoComposedRow(const Preset_t *preset, uint8_t row_index)
{
    uint8_t right_first_item = Display_GetMainInfoRightFirstItem();
    uint8_t info_index;
    uint8_t max_scroll = Display_GetMainInfoScrollMax();

    if (!preset || row_index >= MAIN_INFO_ROW_COUNT)
        return;

    info_index = (uint8_t)(main_info_first_slot + row_index);

    Display_MenuRowComposeClear(DISPLAY_BG_COLOUR);
    Display_DrawMainInfoLeftRow(preset, info_index, 0U);
    Display_DrawMainInfoRightRow(preset, (uint8_t)(right_first_item + row_index), 0U);

    if (row_index == 0U)
    {
        Display_ComposeMainInfoScrollIndicator(1U,
                                               (main_info_first_slot > 0U) ? 1U : 0U);
    }
    else if (row_index == (MAIN_INFO_ROW_COUNT - 1U))
    {
        Display_ComposeMainInfoScrollIndicator(0U,
                                               (main_info_first_slot < max_scroll) ? 1U : 0U);
    }

    Display_MenuRowComposeBlit(main_info_row_y[row_index]);
}

static void Display_DrawMainInfoRows(const Preset_t *preset)
{
    for (uint8_t index = 0U; index < MAIN_INFO_ROW_COUNT; ++index)
        Display_DrawMainInfoComposedRow(preset, index);
}

static void Display_DrawSavingPopup(void)
{
    uint16_t popup_w = (uint16_t)(strlen(MAIN_SAVING_POPUP_TEXT) * MAIN_INFO_FONT.width);
    uint16_t popup_x = (uint16_t)((ST7796_WIDTH - popup_w) / 2U);
    uint16_t popup_y = main_info_row_y[MAIN_SAVING_POPUP_ROW_INDEX];

    Display_ComposeFillRect(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            0U,
                            0U,
                            popup_w,
                            MAIN_INFO_FONT.height,
                            MAIN_SAVING_POPUP_BG_COLOUR);
    Display_ComposeString32(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            0U,
                            0U,
                            MAIN_SAVING_POPUP_TEXT,
                            MAIN_INFO_FONT,
                            MAIN_SAVING_POPUP_TEXT_COLOUR,
                            MAIN_SAVING_POPUP_BG_COLOUR);
    Display_ComposeFillRect(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            0U,
                            0U,
                            popup_w,
                            1U,
                            MAIN_SAVING_POPUP_BORDER_COLOUR);
    Display_ComposeFillRect(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            0U,
                            (uint16_t)(MAIN_INFO_FONT.height - 1U),
                            popup_w,
                            1U,
                            MAIN_SAVING_POPUP_BORDER_COLOUR);
    Display_ComposeFillRect(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            0U,
                            0U,
                            1U,
                            MAIN_INFO_FONT.height,
                            MAIN_SAVING_POPUP_BORDER_COLOUR);
    Display_ComposeFillRect(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            (uint16_t)(popup_w - 1U),
                            0U,
                            1U,
                            MAIN_INFO_FONT.height,
                            MAIN_SAVING_POPUP_BORDER_COLOUR);
    Display_ComposeBlit(popup_x,
                        popup_y,
                        popup_w,
                        MAIN_INFO_FONT.height);
}

static void Display_DrawTimebendPopup(void)
{
    uint16_t popup_w = (uint16_t)(strlen(MAIN_TIMEBEND_POPUP_TEXT) * MAIN_INFO_FONT.width);
    uint16_t popup_x = (uint16_t)((ST7796_WIDTH - popup_w) / 2U);
    uint16_t popup_y = main_info_row_y[MAIN_TIMEBEND_POPUP_ROW_INDEX];

    Display_ComposeFillRect(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            0U,
                            0U,
                            popup_w,
                            MAIN_INFO_FONT.height,
                            MAIN_TIMEBEND_POPUP_BG_COLOUR);
    Display_ComposeString32(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            0U,
                            0U,
                            MAIN_TIMEBEND_POPUP_TEXT,
                            MAIN_INFO_FONT,
                            MAIN_TIMEBEND_POPUP_TEXT_COLOUR,
                            MAIN_TIMEBEND_POPUP_BG_COLOUR);
    Display_ComposeFillRect(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            0U,
                            0U,
                            popup_w,
                            1U,
                            MAIN_TIMEBEND_POPUP_BORDER_COLOUR);
    Display_ComposeFillRect(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            0U,
                            (uint16_t)(MAIN_INFO_FONT.height - 1U),
                            popup_w,
                            1U,
                            MAIN_TIMEBEND_POPUP_BORDER_COLOUR);
    Display_ComposeFillRect(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            0U,
                            0U,
                            1U,
                            MAIN_INFO_FONT.height,
                            MAIN_TIMEBEND_POPUP_BORDER_COLOUR);
    Display_ComposeFillRect(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            (uint16_t)(popup_w - 1U),
                            0U,
                            1U,
                            MAIN_INFO_FONT.height,
                            MAIN_TIMEBEND_POPUP_BORDER_COLOUR);
    Display_ComposeBlit(popup_x,
                        popup_y,
                        popup_w,
                        MAIN_INFO_FONT.height);
}

static void Display_DrawLearningPopup(void)
{
    uint16_t popup_w = (uint16_t)(strlen(MAIN_LEARNING_POPUP_TEXT) * MAIN_INFO_FONT.width);
    uint16_t popup_x = (uint16_t)((ST7796_WIDTH - popup_w) / 2U);
    uint16_t popup_y = main_info_row_y[MAIN_LEARNING_POPUP_ROW_INDEX];

    Display_ComposeFillRect(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            0U,
                            0U,
                            popup_w,
                            MAIN_INFO_FONT.height,
                            MAIN_TIMEBEND_POPUP_BG_COLOUR);
    Display_ComposeString32(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            0U,
                            0U,
                            MAIN_LEARNING_POPUP_TEXT,
                            MAIN_INFO_FONT,
                            MAIN_TIMEBEND_POPUP_TEXT_COLOUR,
                            MAIN_TIMEBEND_POPUP_BG_COLOUR);
    Display_ComposeFillRect(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            0U,
                            0U,
                            popup_w,
                            1U,
                            MAIN_TIMEBEND_POPUP_BORDER_COLOUR);
    Display_ComposeFillRect(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            0U,
                            (uint16_t)(MAIN_INFO_FONT.height - 1U),
                            popup_w,
                            1U,
                            MAIN_TIMEBEND_POPUP_BORDER_COLOUR);
    Display_ComposeFillRect(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            0U,
                            0U,
                            1U,
                            MAIN_INFO_FONT.height,
                            MAIN_TIMEBEND_POPUP_BORDER_COLOUR);
    Display_ComposeFillRect(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            (uint16_t)(popup_w - 1U),
                            0U,
                            1U,
                            MAIN_INFO_FONT.height,
                            MAIN_TIMEBEND_POPUP_BORDER_COLOUR);
    Display_ComposeBlit(popup_x,
                        popup_y,
                        popup_w,
                        MAIN_INFO_FONT.height);
}

void Display_ShowSavingPopup(void)
{
    saving_popup_visible = 1U;
    Display_DrawSavingPopup();
}

void Display_HideSavingPopup(const Preset_t *preset)
{
    if (!saving_popup_visible)
        return;

    saving_popup_visible = 0U;

    if (menu_mode_active)
    {
        uint16_t popup_w = (uint16_t)(strlen(MAIN_SAVING_POPUP_TEXT) * MAIN_INFO_FONT.width);
        uint16_t popup_x = (uint16_t)((ST7796_WIDTH - popup_w) / 2U);
        uint16_t popup_y = main_info_row_y[MAIN_SAVING_POPUP_ROW_INDEX];
        uint16_t popup_bottom = (uint16_t)(popup_y + MAIN_INFO_FONT.height);
        uint16_t clear_y = popup_y;

        /* Restore only the obscured menu rows, then clear the small strip of
         * popup area that sits below the last row cell and would otherwise be
         * left behind. This avoids the full-body menu redraw that flickers. */
        Display_MenuRefreshBodyOnly();

        for (uint8_t row_index = 0U; row_index < MENU_VISIBLE_ROW_COUNT; ++row_index)
        {
            uint16_t row_y = Display_GetMenuRowYByIndex(row_index);
            uint16_t row_bottom = (uint16_t)(row_y + MAIN_INFO_FONT_CELL_HEIGHT);

            if (row_bottom <= popup_y || row_y >= popup_bottom)
                continue;

            if (clear_y < row_y)
            {
                uint16_t clear_h = (uint16_t)(row_y - clear_y);

                Display_ComposeClear(popup_w,
                                     clear_h,
                                     DISPLAY_BG_COLOUR);
                Display_ComposeBlit(popup_x,
                                    clear_y,
                                    popup_w,
                                    clear_h);
            }

            clear_y = (row_bottom < popup_bottom) ? row_bottom : popup_bottom;
            if (clear_y >= popup_bottom)
                break;
        }

        if (clear_y < popup_bottom)
        {
            uint16_t clear_h = (uint16_t)(popup_bottom - clear_y);

            Display_ComposeClear(popup_w,
                                 clear_h,
                                 DISPLAY_BG_COLOUR);
            Display_ComposeBlit(popup_x,
                                clear_y,
                                popup_w,
                                clear_h);
        }

        return;
    }

    if (!preset)
        return;

    Display_DrawMainInfoRows(preset);
}

void Display_ShowTimebendPopup(void)
{
    if (timebend_popup_visible)
        return;

    timebend_popup_visible = 1U;
    Display_DrawTimebendPopup();
}

void Display_HideTimebendPopup(const Preset_t *preset)
{
    if (!timebend_popup_visible)
        return;

    timebend_popup_visible = 0U;

    if (menu_mode_active)
    {
        uint16_t popup_w = (uint16_t)(strlen(MAIN_TIMEBEND_POPUP_TEXT) * MAIN_INFO_FONT.width);
        uint16_t popup_x = (uint16_t)((ST7796_WIDTH - popup_w) / 2U);
        uint16_t popup_y = main_info_row_y[MAIN_TIMEBEND_POPUP_ROW_INDEX];
        uint16_t popup_bottom = (uint16_t)(popup_y + MAIN_INFO_FONT.height);
        uint16_t clear_y = popup_y;

        Display_MenuRefreshBodyOnly();

        for (uint8_t row_index = 0U; row_index < MENU_VISIBLE_ROW_COUNT; ++row_index)
        {
            uint16_t row_y = Display_GetMenuRowYByIndex(row_index);
            uint16_t row_bottom = (uint16_t)(row_y + MAIN_INFO_FONT_CELL_HEIGHT);

            if (row_bottom <= popup_y || row_y >= popup_bottom)
                continue;

            if (clear_y < row_y)
            {
                uint16_t clear_h = (uint16_t)(row_y - clear_y);

                Display_ComposeClear(popup_w,
                                     clear_h,
                                     DISPLAY_BG_COLOUR);
                Display_ComposeBlit(popup_x,
                                    clear_y,
                                    popup_w,
                                    clear_h);
            }

            clear_y = (row_bottom < popup_bottom) ? row_bottom : popup_bottom;
            if (clear_y >= popup_bottom)
                break;
        }

        if (clear_y < popup_bottom)
        {
            uint16_t clear_h = (uint16_t)(popup_bottom - clear_y);

            Display_ComposeClear(popup_w,
                                 clear_h,
                                 DISPLAY_BG_COLOUR);
            Display_ComposeBlit(popup_x,
                                clear_y,
                                popup_w,
                                clear_h);
        }

        return;
    }

    if (!preset)
        return;

    Display_DrawMainInfoRows(preset);
}

void Display_ShowLearningPopup(void)
{
    if (learning_popup_visible)
        return;

    learning_popup_visible = 1U;
    Display_DrawLearningPopup();
}

void Display_HideLearningPopup(const Preset_t *preset)
{
    if (!learning_popup_visible)
        return;

    learning_popup_visible = 0U;

    if (menu_mode_active)
    {
        uint16_t popup_w = (uint16_t)(strlen(MAIN_LEARNING_POPUP_TEXT) * MAIN_INFO_FONT.width);
        uint16_t popup_x = (uint16_t)((ST7796_WIDTH - popup_w) / 2U);
        uint16_t popup_y = main_info_row_y[MAIN_LEARNING_POPUP_ROW_INDEX];
        uint16_t popup_bottom = (uint16_t)(popup_y + MAIN_INFO_FONT.height);
        uint16_t clear_y = popup_y;

        Display_MenuRefreshBodyOnly();

        for (uint8_t row_index = 0U; row_index < MENU_VISIBLE_ROW_COUNT; ++row_index)
        {
            uint16_t row_y = Display_GetMenuRowYByIndex(row_index);
            uint16_t row_bottom = (uint16_t)(row_y + MAIN_INFO_FONT_CELL_HEIGHT);

            if (row_bottom <= popup_y || row_y >= popup_bottom)
                continue;

            if (clear_y < row_y)
            {
                uint16_t clear_h = (uint16_t)(row_y - clear_y);

                Display_ComposeClear(popup_w,
                                     clear_h,
                                     DISPLAY_BG_COLOUR);
                Display_ComposeBlit(popup_x,
                                    clear_y,
                                    popup_w,
                                    clear_h);
            }

            clear_y = (row_bottom < popup_bottom) ? row_bottom : popup_bottom;
            if (clear_y >= popup_bottom)
                break;
        }

        if (clear_y < popup_bottom)
        {
            uint16_t clear_h = (uint16_t)(popup_bottom - clear_y);

            Display_ComposeClear(popup_w,
                                 clear_h,
                                 DISPLAY_BG_COLOUR);
            Display_ComposeBlit(popup_x,
                                clear_y,
                                popup_w,
                                clear_h);
        }

        return;
    }

    if (!preset)
        return;

    Display_DrawMainInfoRows(preset);
}

/* ?????? Display_DrawMainLayout ??????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
 * Draws the parts of the screen that don't change between presets:
 *   ??? Dark-grey footer bar at the bottom.
 *   ??? Centred "MIDI / RELAY STATUS" label inside the bar.
 * Called automatically by Display_DrawMainScreen when main_layout_dirty is set.
 * ????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */
static void Display_DrawMainLayout(void)
{
    Display_DrawFootbar();
    main_layout_dirty = 0U;
}

void Display_RedrawMenuSelectionItem(DisplayMenuPage_t page, uint8_t item_index, uint8_t selected)
{
    switch (page)
    {
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES:
        Display_RedrawMenuFunctionButtonSelectionItem(item_index, selected);
        return;

    default:
        (void)selected;
        Display_DrawMenuPageItem(page, item_index);
        return;
    }
}

typedef enum
{
    DISPLAY_MENU_DIRTY_ROW_ACTION_NONE = 0,
    DISPLAY_MENU_DIRTY_ROW_ACTION_SELECTION,
    DISPLAY_MENU_DIRTY_ROW_ACTION_CURRENT_VALUE,
    DISPLAY_MENU_DIRTY_ROW_ACTION_BANK_WINDOW,
    DISPLAY_MENU_DIRTY_ROW_ACTION_DEVICE_WINDOW,
    DISPLAY_MENU_DIRTY_ROW_ACTION_FUNCTION_BUTTON_PROGRAM_WINDOW,
    DISPLAY_MENU_DIRTY_ROW_ACTION_FUNCTION_BUTTON_CC_WINDOW,
    DISPLAY_MENU_DIRTY_ROW_ACTION_CLEAR,
} DisplayMenuDirtyRowAction_t;

void Display_RedrawMenuCurrentValueItem(DisplayMenuPage_t page, uint8_t item_index)
{
    switch (page)
    {
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES:
        if (Display_RedrawMenuFunctionButtonCurrentValueItem())
            return;
        break;

    default:
        break;
    }

    Display_DrawMenuPageItem(page, item_index);
}

void Display_MainInfoScrollReset(void)
{
    main_info_first_slot = preset_edit_mode_active
        ? Display_GetPresetEditScrollFirstSlot(preset_edit_cursor_index)
        : 0U;
}

uint8_t Display_MainInfoScrollBy(int8_t delta)
{
    int16_t next_slot = (int16_t)main_info_first_slot + (int16_t)delta;
    uint8_t max_scroll = Display_GetMainInfoScrollMax();

    if (next_slot < 0)
        next_slot = 0;
    else if (next_slot > (int16_t)max_scroll)
        next_slot = (int16_t)max_scroll;

    if ((uint8_t)next_slot == main_info_first_slot)
        return 0U;

    main_info_first_slot = (uint8_t)next_slot;
    return 1U;
}

uint8_t Display_MainInfoScrollAndRefresh(const Preset_t *preset, int8_t delta)
{
    uint8_t previous_first_slot;

    if (!preset)
        return 0U;

    previous_first_slot = main_info_first_slot;
    if (!Display_MainInfoScrollBy(delta))
        return 0U;

    (void)previous_first_slot;
    Display_DrawMainInfoRows(preset);

    return 1U;
}

void Display_PresetEditEnter(void)
{
    preset_edit_mode_active = 1U;
    preset_init_confirm_active = 0U;
    preset_edit_cursor_index = 0U;
    preset_name_edit_active = 0U;
    preset_name_edit_cursor_index = 0U;
    main_info_first_slot = Display_GetPresetEditScrollFirstSlot(preset_edit_cursor_index);
    Display_DrawFootbar();
}

void Display_PresetEditExit(void)
{
    preset_edit_mode_active = 0U;
    preset_init_confirm_active = 0U;
    preset_edit_cursor_index = 0U;
    preset_name_edit_active = 0U;
    preset_name_edit_cursor_index = 0U;
    main_info_first_slot = 0U;
    Display_DrawCurrentBankNameLine();
    Display_DrawFootbar();
}

uint8_t Display_PresetEditIsActive(void)
{
    return preset_edit_mode_active;
}

void Display_PresetNameEditEnter(void)
{
    DisplayPresetEditField_t field = Display_PresetEditGetField();

    if (!preset_edit_mode_active || preset_init_confirm_active || field.type != DISPLAY_PRESET_EDIT_FIELD_NAME)
        return;

    preset_name_edit_active = 1U;
    preset_name_edit_cursor_index = 0U;
}

void Display_PresetNameEditExit(void)
{
    preset_name_edit_active = 0U;
}

uint8_t Display_PresetNameEditIsActive(void)
{
    return preset_name_edit_active;
}

uint8_t Display_PresetNameEditMoveCursor(const Preset_t *preset, int8_t delta)
{
    int16_t next_index;
    uint8_t max_index;
    uint8_t previous_index;

    if (!preset_name_edit_active || !preset || delta == 0)
        return 0U;

    max_index = Display_GetPresetNameEditMaxIndex(preset);
    previous_index = preset_name_edit_cursor_index;
    next_index = (int16_t)preset_name_edit_cursor_index + (int16_t)delta;
    if (next_index < 0)
        next_index = 0;
    else if (next_index > (int16_t)max_index)
        next_index = (int16_t)max_index;

    if ((uint8_t)next_index == preset_name_edit_cursor_index)
        return 0U;

    preset_name_edit_cursor_index = (uint8_t)next_index;
    (void)previous_index;
    Display_DrawPresetName(preset);
    return 1U;
}

uint8_t Display_PresetNameEditGetCursorIndex(void)
{
    return preset_name_edit_cursor_index;
}

uint8_t Display_PresetEditMoveCursor(int8_t delta)
{
    int16_t next_cursor;

    if (!preset_edit_mode_active || preset_init_confirm_active || delta == 0)
        return 0U;

    next_cursor = (int16_t)preset_edit_cursor_index + (int16_t)delta;
    if (next_cursor < 0)
        next_cursor = 0;
    else if (next_cursor >= (int16_t)MAIN_INFO_EDIT_FIELD_COUNT)
        next_cursor = (int16_t)(MAIN_INFO_EDIT_FIELD_COUNT - 1U);

    if ((uint8_t)next_cursor == preset_edit_cursor_index)
        return 0U;

    preset_edit_cursor_index = (uint8_t)next_cursor;
    main_info_first_slot = Display_GetPresetEditScrollFirstSlot(preset_edit_cursor_index);
    return 1U;
}

uint8_t Display_PresetEditMoveCursorAndRefresh(const Preset_t *preset, int8_t delta)
{
    DisplayPresetEditField_t previous_field;
    DisplayPresetEditField_t next_field;
    uint8_t previous_first_slot;
    uint8_t moved;

    if (!preset || !preset_edit_mode_active)
        return 0U;

    previous_field = Display_PresetEditGetField();
    previous_first_slot = main_info_first_slot;
    moved = Display_PresetEditMoveCursor(delta);
    if (!moved)
        return 0U;

    next_field = Display_PresetEditGetField();
    if (main_info_first_slot != previous_first_slot)
    {
        Display_DrawMainInfoRows(preset);
        return 1U;
    }

    switch (previous_field.type)
    {
    case DISPLAY_PRESET_EDIT_FIELD_NAME:
        Display_DrawPresetName(preset);
        break;

    case DISPLAY_PRESET_EDIT_FIELD_PROGRAM:
        if (previous_field.itemIndex >= previous_first_slot
            && previous_field.itemIndex < (uint8_t)(previous_first_slot + MAIN_INFO_ROW_COUNT))
            Display_DrawMainInfoComposedRow(preset,
                                            (uint8_t)(previous_field.itemIndex - previous_first_slot));
        break;

    case DISPLAY_PRESET_EDIT_FIELD_RELAY:
    {
        uint8_t right_first_item = Display_GetMainInfoRightFirstItem();

        if (previous_field.itemIndex >= right_first_item
            && previous_field.itemIndex < (uint8_t)(right_first_item + MAIN_INFO_ROW_COUNT))
        {
            Display_DrawMainInfoComposedRow(preset,
                                            (uint8_t)(previous_field.itemIndex - right_first_item));
        }
        break;
    }

    case DISPLAY_PRESET_EDIT_FIELD_FUNCTION_BUTTON:
    {
        uint8_t right_first_item = Display_GetMainInfoRightFirstItem();

        if (PRESET_RELAY_COUNT < right_first_item
            || PRESET_RELAY_COUNT >= (uint8_t)(right_first_item + MAIN_INFO_ROW_COUNT))
        {
            break;
        }

        Display_DrawMainInfoComposedRow(preset,
                                        (uint8_t)(PRESET_RELAY_COUNT - right_first_item));
        break;
    }

    case DISPLAY_PRESET_EDIT_FIELD_CC_CHANNEL:
    case DISPLAY_PRESET_EDIT_FIELD_CC_NUMBER:
    case DISPLAY_PRESET_EDIT_FIELD_CC_VALUE:
        if ((PRESET_DEVICE_SLOTS + previous_field.itemIndex) >= previous_first_slot
            && (PRESET_DEVICE_SLOTS + previous_field.itemIndex) < (uint8_t)(previous_first_slot + MAIN_INFO_ROW_COUNT))
            Display_DrawMainInfoComposedRow(preset,
                                            (uint8_t)((PRESET_DEVICE_SLOTS + previous_field.itemIndex) - previous_first_slot));
        break;

    case DISPLAY_PRESET_EDIT_FIELD_INIT:
        if (MAIN_INFO_PRESET_INIT_ROW_INDEX >= previous_first_slot
            && MAIN_INFO_PRESET_INIT_ROW_INDEX < (uint8_t)(previous_first_slot + MAIN_INFO_ROW_COUNT))
            Display_DrawMainInfoComposedRow(preset,
                                            (uint8_t)(MAIN_INFO_PRESET_INIT_ROW_INDEX - previous_first_slot));
        break;

    default:
        break;
    }

    if (!Display_PresetEditFieldsMatch(previous_field, next_field))
        Display_PresetEditRefreshCurrentField(preset);

    return 1U;
}

DisplayPresetEditField_t Display_PresetEditGetField(void)
{
    if (!preset_edit_mode_active)
    {
        DisplayPresetEditField_t field = { DISPLAY_PRESET_EDIT_FIELD_NONE, 0U };
        return field;
    }

    return Display_GetPresetEditFieldForCursor(preset_edit_cursor_index);
}

void Display_PresetEditRefreshCurrentField(const Preset_t *preset)
{
    DisplayPresetEditField_t field;
    uint8_t row_index;

    if (!preset || !preset_edit_mode_active)
        return;

    field = Display_PresetEditGetField();
    row_index = 0xFFU;

    switch (field.type)
    {
    case DISPLAY_PRESET_EDIT_FIELD_NAME:
        Display_DrawPresetName(preset);
        return;

    case DISPLAY_PRESET_EDIT_FIELD_PROGRAM:
        if (field.itemIndex < main_info_first_slot)
            return;

        row_index = (uint8_t)(field.itemIndex - main_info_first_slot);
        if (row_index >= MAIN_INFO_ROW_COUNT)
            return;

        Display_DrawMainInfoComposedRow(preset, row_index);
        return;

    case DISPLAY_PRESET_EDIT_FIELD_RELAY:
    {
        uint8_t right_first_item = Display_GetMainInfoRightFirstItem();

        if (field.itemIndex >= PRESET_RELAY_COUNT
            || field.itemIndex < right_first_item)
            return;

        row_index = (uint8_t)(field.itemIndex - right_first_item);
        if (row_index >= MAIN_INFO_ROW_COUNT)
            return;

        Display_DrawMainInfoComposedRow(preset, row_index);
        return;
    }

    case DISPLAY_PRESET_EDIT_FIELD_FUNCTION_BUTTON:
    {
        uint8_t right_first_item = Display_GetMainInfoRightFirstItem();

        if (PRESET_RELAY_COUNT < right_first_item)
            return;

        row_index = (uint8_t)(PRESET_RELAY_COUNT - right_first_item);
        if (row_index >= MAIN_INFO_ROW_COUNT)
            return;

        Display_DrawMainInfoComposedRow(preset, row_index);
        return;
    }

    case DISPLAY_PRESET_EDIT_FIELD_CC_CHANNEL:
    case DISPLAY_PRESET_EDIT_FIELD_CC_NUMBER:
    case DISPLAY_PRESET_EDIT_FIELD_CC_VALUE:
        row_index = (uint8_t)((PRESET_DEVICE_SLOTS + field.itemIndex) - main_info_first_slot);
        if (row_index >= MAIN_INFO_ROW_COUNT)
            return;

        Display_DrawMainInfoComposedRow(preset, row_index);
        return;

    case DISPLAY_PRESET_EDIT_FIELD_INIT:
        if (MAIN_INFO_PRESET_INIT_ROW_INDEX < main_info_first_slot)
            return;

        row_index = (uint8_t)(MAIN_INFO_PRESET_INIT_ROW_INDEX - main_info_first_slot);
        if (row_index >= MAIN_INFO_ROW_COUNT)
            return;

        Display_DrawMainInfoComposedRow(preset, row_index);
        return;

    default:
        return;
    }
}

void Display_PresetInitConfirmEnter(void)
{
    if (!preset_edit_mode_active || preset_init_confirm_active)
        return;

    preset_init_confirm_active = 1U;
    Display_DrawFootbar();
    Display_DrawMainModeHeader();
    Display_DrawPresetInitConfirmPrompt();
}

void Display_PresetInitConfirmExit(void)
{
    if (!preset_init_confirm_active)
        return;

    preset_init_confirm_active = 0U;
    Display_DrawFootbar();
    Display_DrawMainModeHeader();
    Display_DrawCurrentBankNameLine();
}

uint8_t Display_PresetInitConfirmIsActive(void)
{
    return preset_init_confirm_active;
}

/* Full refresh of the live screen for one preset.
 *
 * This is the monolith-era entry point that still orchestrates the high-level
 * live layout: background, footbar, BPM strip, preset title, bank line, and
 * the visible main-info rows. Incremental helpers now handle many subregions,
 * but this function remains the safe "rebuild the whole live screen" path. */
void Display_DrawMainScreen(const Preset_t *p, uint16_t bpm)
{
    if (menu_mode_active && !menu_preview_active)
    {
        Display_MenuRefresh();
        return;
    }

    if (main_layout_dirty)
    {
        /* A dirty layout means static chrome may be stale after theme changes,
         * screensaver wake, or mode transitions, so repaint the full backdrop. */
        ST7796_FillScreen(Display_GetBackgroundColour());
        bpm_display_valid = 0U;
        display_state.transport_status_valid = 0U;
        Display_DrawMainLayout();
    }

    Display_UpdateBPM(bpm);
    Display_DrawMainModeHeader();
    Display_DrawPresetName(p);
    Display_DrawCurrentBankNameLine();
    Display_DrawMainInfoRows(p);
}

void Display_RefreshMainScreenContent(const Preset_t *p, uint16_t bpm)
{
    if (menu_mode_active && !menu_preview_active)
    {
        Display_MenuRefresh();
        return;
    }

    if (main_layout_dirty)
    {
        Display_DrawMainScreen(p, bpm);
        return;
    }

    Display_UpdateBPM(bpm);
    Display_DrawPresetName(p);
    Display_DrawCurrentBankNameLine();
    Display_DrawMainInfoRows(p);
}

void Display_RefreshPresetEditMode(const Preset_t *p, uint16_t bpm)
{
    if (menu_mode_active && !menu_preview_active)
    {
        Display_MenuRefresh();
        return;
    }

    if (!p)
        return;

    /* LIVE/EDIT toggles only affect header text, footer state, the preset-name
     * highlight mode, and the three visible info rows. Fall back to a full draw
     * only when the static layout really is dirty, e.g. after the screensaver. */
    if (main_layout_dirty)
    {
        Display_DrawMainScreen(p, bpm);
        return;
    }

    Display_DrawMainModeHeader();
    Display_DrawPresetName(p);
    Display_DrawMainInfoRows(p);
}


