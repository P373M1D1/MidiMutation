#include "display_functions.h"
#include "button_functions.h"
#include "midi_functions.h"
#include "midi_devices.h"
#include "runtime_config.h"
#include "st7796.h"
#include "fonts.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>

/* ?????? display_functions.c ???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
 *
 * All visual output for the ST7796 480??320 TFT display and the DAC backlight.
 *
 * Hardware connections (configured in main.cpp MX_GPIO_Init / MX_SPI1_Init):
 *   SPI1  ??? display data bus
 *     SCK  = PA5   (SPI1_SCK,  AF5)
 *     MOSI = PA7   (SPI1_MOSI, AF5)
 *   Control pins (push-pull outputs, high speed):
 *     RST  = PF12  (ST7796_RST_Pin  / ST7796_RST_GPIO_Port)
 *     CS   = PD14  (ST7796_CS_Pin   / ST7796_CS_GPIO_Port)
 *     DC   = PD15  (ST7796_DC_Pin   / ST7796_DC_GPIO_Port)
 *   SPI1 baud rate = PCLK2 / 2 = 96 MHz / 2 = 48 MHz
 *     (PCLK2 = SYSCLK / 1 per main.cpp SystemClock_Config APB2 divider)
 *
 *   Backlight ??? DAC1 CH1 on PA4 (12-bit, 0???4095 ??? 0???3.3 V ??? LED driver)
 *     DAC and GPIOA clocks are enabled here in Display_BL_Init because
 *     the backlight must be brought up before ST7796_Init is called.
 *     (GPIOA clock is also enabled by MX_GPIO_Init in main.cpp; enabling
 *     it twice is harmless ??? the HAL macro is idempotent.)
 *
 * Screen coordinates: origin (0,0) is top-left, x???right, y???down.
 * Landscape orientation: width = 480 px, height = 320 px.
 *
 * Screen layout (see Display_DrawMainLayout / Display_DrawMainScreen):
 *   y=   0 ..  34 :  BPM value,     Font_Consolas15x35, right side (x=365)
 *   y=  85 .. 133 :  Preset name,   Font_Consolas23x49, centred, 20 chars wide
 *   y= 145 .. 179 :  Bank name,     Font_Consolas15x35, centred
 *   y= 184 .. 291 :  3 info rows,   Font_Consolas15x35
 *                      left  (x= 30): "CH n: ppp"  MIDI channel + program number
 *                      right (x=220): "Relay_n: open/closed"
 *   y= 298 .. 319 :  Footer bar,    dark grey, "MIDI / RELAY STATUS"
 * ????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */

/* ?????? Backlight ??????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */
/* DAC1 CH1 on PA4.  The DAC is 12-bit (0 = off, 4095 = full brightness).
 *
 * Fade timing:
 *   BL_SPIN_DELAY = 48 000 busy-wait cycles.
 *   At SYSCLK = 96 MHz each cycle ??? 10.4 ns ??? 48 000 cycles ??? 0.5 ms per step.
 *   100 steps ?? 0.5 ms = ~50 ms total fade duration.
 *
 * The spin loop uses a volatile counter to prevent the compiler from
 * optimising the delay away.
 */

#define BL_STEPS      100U      // number of DAC ramp steps used for backlight fades
#define BL_SPIN_DELAY 48000U    /* busy-wait cycles @ 96 MHz ??? 0.5 ms per step */
#define BL_BRIGHTNESS_DEFAULT 2095U /* max DAC value for full backlight brightness */

#define DISPLAY_BG_COLOUR              BLACK               // default background colour for full-screen clears and text backgrounds

static uint16_t Display_GetConfiguredBacklightBrightness(void)
{
    const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();
    uint16_t brightness = BL_BRIGHTNESS_DEFAULT;

    if (global)
        brightness = global->backlight_brightness;

    if (brightness > 4095U)
        brightness = 4095U;

    return brightness;
}

/* ?????? Display_BL_Init ???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
 * Configures PA4 as an analog output and enables DAC1 channel 1.
 * Must be called before Display_BL_FadeIn / FadeOut.
 * Called early in main.cpp (before ST7796_Init) so the backlight can be
 * kept off while the display initialises, avoiding a white flash.
 * ????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */
void Display_BL_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();  /* PA4 ??? DAC1_OUT1 (backlight analog output) */
    __HAL_RCC_DAC_CLK_ENABLE();    /* DAC peripheral clock                       */

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin  = GPIO_PIN_4;
    gpio.Mode = GPIO_MODE_ANALOG;  /* analog mode disables the digital driver    */
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    DAC->CR      = DAC_CR_EN1;     /* enable channel 1, no trigger, no buffer    */
    DAC->DHR12R1 = 0U;             /* start with backlight fully off             */
}

/* ?????? Display_BL_FadeIn ?????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
 * Ramps the DAC output from 0 to 4095 over ~50 ms.
 * Blocking ??? call only from main-loop context, not from an ISR.
 * ????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */
void Display_BL_FadeIn(void)
{
    uint16_t brightness = Display_GetConfiguredBacklightBrightness();

    for (uint32_t step = 0U; step <= BL_STEPS; step++)
    {
        DAC->DHR12R1 = (step * brightness) / BL_STEPS;          /* linear ramp up    */
        for (volatile uint32_t d = 0U; d < BL_SPIN_DELAY; d++) {}
    }
}

/* ?????? Display_BL_FadeOut ??????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
 * Ramps the DAC output from 4095 down to 0 over ~50 ms.
 * The loop counts down using an unsigned counter; the 'if (step==0) break'
 * guard prevents underflow wrap-around (uint32 wrapping to 0xFFFFFFFF).
 * ????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */
void Display_BL_FadeOut(void)
{
    uint16_t brightness = Display_GetConfiguredBacklightBrightness();

    for (uint32_t step = BL_STEPS; ; step--)
    {
        DAC->DHR12R1 = (step * brightness) / BL_STEPS;          /* linear ramp down  */
        for (volatile uint32_t d = 0U; d < BL_SPIN_DELAY; d++) {}
        if (step == 0U) break;                              /* avoid uint underflow */
    }
}

/* ?????? Screen layout constants ????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */
#define MAIN_FOOTBAR_Y                 298U                  // top edge of the footer/status bar
#define MAIN_FOOTBAR_H                 (ST7796_HEIGHT - MAIN_FOOTBAR_Y) // footer height from its top edge to screen bottom
#define MAIN_FOOTBAR_COLOR             JET           // fill colour for the footer/status bar
#define MAIN_FOOTBAR_FONT              Font_Consolas8x21    // font used for the footer caption
#define MAIN_FOOTBAR_TEXT_COLOUR       WHITE                 // text colour for the footer caption
#define MAIN_FOOTBAR_SECTION_COUNT     3U                    // footer is conceptually split into three unlabeled regions
#define MAIN_FOOTBAR_SECTION_WIDTH     (ST7796_WIDTH / MAIN_FOOTBAR_SECTION_COUNT) // width of one footer region
#define MAIN_FOOTBAR_LEFT_TEXT         "SCROLL / EDIT"              // label for the left footer region during normal operation
#define MAIN_FOOTBAR_CENTER_TEXT       "MENU"               // label for the middle footer region during normal operation
#define MAIN_FOOTBAR_RIGHT_TEXT        "TEMPO"               // label for the right footer region during normal operation
#define MAIN_FOOTBAR_EDIT_LEFT_TEXT    "SELECT / ENTER"                    // label for the left footer region while preset edit mode is active
#define MAIN_FOOTBAR_EDIT_CENTER_TEXT  "SEND"                      // label for the middle footer region while preset edit mode is active
#define MAIN_FOOTBAR_EDIT_RIGHT_TEXT   "VALUE / EXIT"              // label for the right footer region while preset edit mode is active
#define MAIN_FOOTBAR_MENU_LEFT_TEXT    "NAV / ENTER"              // label for the left footer region while menu mode is active
#define MAIN_FOOTBAR_MENU_CENTER_TEXT  "HOME"                     // label for the middle footer region while menu mode is active
#define MAIN_FOOTBAR_MENU_RIGHT_TEXT   "VALUE / BACK"             // label for the right footer region while menu mode is active
#define MAIN_FOOTBAR_CONFIRM_LEFT_TEXT  ""                         // left footer label while a confirm page is active
#define MAIN_FOOTBAR_CONFIRM_CENTER_TEXT "YES"                    // center footer label while a confirm page is active
#define MAIN_FOOTBAR_CONFIRM_RIGHT_TEXT "NO"                      // right footer label while a confirm page is active
#define MAIN_INFO_LEFT_X               30U                  // x origin of the left info column (MIDI programs)
#define MAIN_INFO_RIGHT_X              235U                 // x origin of the right info column (relay / special state), shifted right by one glyph cell
#define MAIN_INFO_FONT                 Font_Consolas15x35   // font used for bank text, BPM text, and info rows
#define MAIN_INFO_TEXT_COLOUR          CHARCOAL            // normal text colour for info rows
#define MAIN_INFO_TEXT_BG_COLOUR       DISPLAY_BG_COLOUR    // background colour behind normal info text
#define MAIN_INFO_SHARED_TEXT_COLOUR   DISPLAY_BG_COLOUR    // text colour when a shared program number is inverted
#define MAIN_INFO_SHARED_BG_COLOUR     MAIN_INFO_TEXT_COLOUR // background colour when a shared program number is inverted
#define MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR  BLACK            // text colour inside the active edit cursor field
#define MAIN_INFO_EDIT_CURSOR_BG_COLOUR    WHITE            // background colour for the active edit cursor field
#define MAIN_INFO_EDIT_CURSOR_SHARED_BG_COLOUR YELLOW       // caution background for an edited program value that is also used in another preset
#define MAIN_INFO_ROW_COUNT            3U                   // number of vertically stacked info rows currently visible on the main screen
#define MAIN_INFO_PROGRAM_DIGITS       3U                   // fixed width of the displayed MIDI program number
#define MAIN_INFO_CC_CHANNEL_DIGITS    2U                   // fixed width of the displayed MIDI CC channel number
#define MAIN_INFO_CC_VALUE_DIGITS      3U                   // fixed width of the displayed MIDI CC number/value fields
#define MAIN_INFO_CC_LABEL_DIGITS      ((PRESET_CC_SLOT_COUNT >= 100U) ? 3U : ((PRESET_CC_SLOT_COUNT >= 10U) ? 2U : 1U)) // digit width reserved for labels like "CC 8"
#define MAIN_INFO_CC_LABEL_CHARS       (2U + MAIN_INFO_CC_LABEL_DIGITS + 2U) // padded width of labels like "CC 8 "
#define MAIN_INFO_CC_CHANNEL_PREFIX    "CH: "              // label shown ahead of the CC channel value
#define MAIN_INFO_CC_NUMBER_PREFIX     " CC: "             // label shown ahead of the CC number value
#define MAIN_INFO_CC_VALUE_PREFIX      " Value: "          // label shown ahead of the CC value
#define MAIN_INFO_EDIT_FIELD_COUNT     (1U + PRESET_DEVICE_SLOTS + PRESET_RELAY_COUNT + (PRESET_CC_SLOT_COUNT * 3U)) // number of editable fields in preset edit mode, including the preset name
#define MAIN_INFO_SHARED_PAD_CHARS     2U                   // extra chars cleared when special-function text shrinks
#define MAIN_UNUSED_PROGRAM            0xFFU                // sentinel meaning no MIDI program is assigned to that slot
#define MAIN_EMPTY_RIGHT_INFO_TEXT     "                "   // blank filler used to clear an unused right-side row
#define MAIN_SAVING_POPUP_TEXT         " SAVING "          // temporary overlay shown while preset edits are being committed to flash
#define MAIN_SAVING_POPUP_BG_COLOUR    WHITE             // background behind the saving overlay
#define MAIN_SAVING_POPUP_TEXT_COLOUR  BLACK                // text colour for the saving overlay
#define MAIN_SAVING_POPUP_ROW_INDEX    1U                   // center the saving overlay on the middle info row
#define MAIN_SCROLL_INDICATOR_X        8U                   // x position of the device-list scroll indicator triangles
#define MAIN_SCROLL_INDICATOR_W        9U                   // width of the scroll indicator triangles
#define MAIN_SCROLL_INDICATOR_H        5U                   // height of the scroll indicator triangles
#define MAIN_SCROLL_INDICATOR_COLOUR   MAIN_INFO_TEXT_COLOUR // colour of the up/down scroll indicators
#define MAIN_MODE_HEADER_TEXT_Y        10U                  // y position of the centered LIVE/EDIT mode label at the top of the screen
#define MAIN_MODE_HEADER_TEXT_CHARS    4U                   // width reserved for the centered top mode label
#define MAIN_MODE_HEADER_EDIT_TEXT_CHARS 6U                 // wider badge width for EDIT so the yellow background has one padded cell on each side
#define MAIN_MODE_HEADER_MENU_PAD_CHARS 2U                  // padded cells added to menu header badges so they match the EDIT badge style
#define MAIN_MODE_HEADER_MAX_TEXT_CHARS  10U                // widest header badge footprint that must be cleared between mode changes
#define MAIN_MODE_HEADER_FONT          MAIN_FOOTBAR_FONT    // font used for the top mode label
#define MAIN_MODE_HEADER_COLOUR        WHITE                // text colour for the LIVE/EDIT mode label
#define MAIN_MODE_HEADER_EDIT_COLOUR   BLACK               // text colour for the EDIT mode label
#define MAIN_MODE_HEADER_EDIT_BG_COLOUR YELLOW              // background colour behind the EDIT mode label for contrast
#define MAIN_PRESET_TEXT_Y             85U                  // y position of the large preset name line
#define MAIN_PRESET_TEXT_CHARS         20U                  // fixed character width used when centering preset names
#define MAIN_PRESET_FONT               Font_Consolas23x49   // large font for the preset name
#define MAIN_PRESET_COLOUR             WHITE               // text colour for the preset name
#define MAIN_PRESET_BG_COLOUR          DISPLAY_BG_COLOUR    // background colour behind the preset name
#define MAIN_BANK_TEXT_Y               145U                 // y position of the bank name line
#define MAIN_BANK_TEXT_CHARS           PRESET_BANK_NAME_MAXLEN // fixed character width used when centering bank names
#define MAIN_BANK_FONT                 Font_Consolas15x35   // font for the bank name line
#define MAIN_BANK_COLOUR               CHARCOAL            // text colour for the bank name line
#define MAIN_BANK_BG_COLOUR            DISPLAY_BG_COLOUR    // background colour behind the bank name line
#define MAIN_SPECIAL_FUNCTION_BUTTON_DEFAULT_NAME       "Vita" // fallback label shown ahead of the special-function-button state
#define MAIN_SPECIAL_FUNCTION_BUTTON_DEFAULT_ACTIVE_TEXT "undead" // fallback text shown when the special-function button mode is active
#define MAIN_SPECIAL_FUNCTION_BUTTON_DEFAULT_INACTIVE_TEXT "dead"   // fallback text shown when the special-function button mode is inactive
#define MAIN_SPECIAL_FUNCTION_BUTTON_ACTIVE_COLOUR     WHITE // text colour for the active special-function-button state
#define MAIN_SPECIAL_FUNCTION_BUTTON_INACTIVE_COLOUR   CHARCOAL // text colour for the inactive special-function-button state
#define MAIN_SPECIAL_FUNCTION_BUTTON_ACTIVE_BG         DARK_RED // highlight background behind the active special-function-button state
#define MAIN_SPECIAL_FUNCTION_BUTTON_INACTIVE_BG       DISPLAY_BG_COLOUR // background behind the inactive special-function-button state
#define MAIN_SPECIAL_FUNCTION_BUTTON_PREFIX_COLOUR     MAIN_INFO_TEXT_COLOUR // colour of the special-function-button label prefix
#define MAIN_SPECIAL_FUNCTION_BUTTON_PREFIX_BG         MAIN_INFO_TEXT_BG_COLOUR // background behind the special-function-button label prefix
#define MAIN_SPECIAL_FUNCTION_BUTTON_BORDER_COLOUR     DISPLAY_BG_COLOUR // top/bottom border colour used to style the active special-function state
#define MAIN_INFO_HIGHLIGHT_BORDER_H   2U                   // thickness of the top and bottom highlight bars around active state text

#define MENU_ROOT_ITEM_COUNT            3U                   // number of top-level entries currently shown in the menu shell
#define MENU_VISIBLE_ROW_COUNT          4U                   // number of menu rows visible at one time in the current shell layout
#define MENU_GLOBAL_ITEM_COUNT          4U                   // number of editable global-setting rows currently implemented
#define MENU_BANK_EDIT_ITEM_COUNT       4U                   // number of items on the bank edit page
#define MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT 3U              // number of editable text rows before the compare table starts
#define MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT // first logical row index of the compare table
#define MENU_FUNCTION_BUTTON_CC_FIRST_INDEX (MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX + RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT) // first logical row index of the CC compare section
#define MENU_FUNCTION_BUTTON_ITEM_COUNT (MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT + MENU_FUNCTION_BUTTON_MESSAGE_ROW_COUNT) // total rows in the combined function-button editor page
#define MENU_FUNCTION_BUTTON_MESSAGE_ROW_COUNT (RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT + RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT) // total rows shown on the dense function-button message pages
#define MENU_DEVICE_EDIT_ITEM_COUNT     8U                   // number of items on the device edit page
#define MENU_ITEM_X                     24U                  // left edge of the menu row content area
#define MENU_ITEM_W                     (ST7796_WIDTH - (MENU_ITEM_X * 2U)) // width of the menu row content area
#define MENU_BODY_Y                     MAIN_PRESET_TEXT_Y   // top edge of the menu body area below the header strip
#define MENU_BODY_H                     (MAIN_FOOTBAR_Y - MENU_BODY_Y) // height of the menu-owned area above the footbar
#define MENU_PLACEHOLDER_TEXT           "COMING SOON"       // placeholder body text for menu branches not implemented yet

static const uint16_t main_info_row_y[MAIN_INFO_ROW_COUNT] = {184U, 220U, 256U};
static const uint16_t menu_row_y[MENU_VISIBLE_ROW_COUNT] = {85U, 127U, 169U, 211U};

typedef enum {
    DISPLAY_MENU_PAGE_ROOT = 0,
    DISPLAY_MENU_PAGE_BANKS,
    DISPLAY_MENU_PAGE_BANK_EDIT,
    DISPLAY_MENU_PAGE_FUNCTION_BUTTON,
    DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES,
    DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES,
    DISPLAY_MENU_PAGE_DEVICES,
    DISPLAY_MENU_PAGE_DEVICE_EDIT,
    DISPLAY_MENU_PAGE_DEVICE_INIT_CONFIRM,
    DISPLAY_MENU_PAGE_GLOBAL,
} DisplayMenuPage_t;

typedef enum {
    DISPLAY_MENU_TEXT_FIELD_NONE = 0,
    DISPLAY_MENU_TEXT_FIELD_BANK_NAME,
    DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_NAME,
    DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_ACTIVE_LABEL,
    DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_INACTIVE_LABEL,
    DISPLAY_MENU_TEXT_FIELD_DEVICE_NAME,
} DisplayMenuTextField_t;

#define BPM_FONT                       Font_Consolas15x35   // font used for all BPM display text
#define BPM_TEXT_Y                     7U                   // y position of the BPM line at the top of the screen
#define BPM_INTERNAL_X                 365U                 // legacy anchor for internal BPM placement

/* Set to 1 whenever the static elements (footer bar) need to be redrawn ???
 * e.g. after the screensaver has painted over them. */
static uint8_t main_layout_dirty = 1U;
static uint8_t bpm_display_valid = 0U;
static uint8_t bpm_display_external = 0U;
static uint8_t bpm_display_sync_lost = 0U;
static uint16_t bpm_display_value_x10 = 0U;
static uint32_t bpm_display_external_update_tick = 0U;
static char bpm_display_internal_text[8] = "";
static char bpm_display_external_text[14] = "";
static uint16_t bpm_display_internal_head_x = 0U;
static char transport_barbeat_text[5] = "";
static uint8_t main_info_first_slot = 0U;
static uint8_t preset_edit_mode_active = 0U;
static uint8_t preset_edit_cursor_index = 0U;
static uint8_t preset_name_edit_active = 0U;
static uint8_t preset_name_edit_cursor_index = 0U;
static uint8_t preset_name_full_refresh_pending = 0U;
static uint8_t preset_name_last_render_length = 0U;
static uint8_t preset_name_last_pad_left = 0U;
static uint8_t saving_popup_visible = 0U;
static uint8_t menu_mode_active = 0U;
static uint8_t menu_root_selection_index = 0U;
static uint8_t menu_bank_selection_index = 0U;
static uint8_t menu_active_bank_index = 0U;
static uint8_t menu_bank_edit_selection_index = 0U;
static uint8_t menu_function_button_selection_index = 0U;
static uint8_t menu_device_selection_index = 0U;
static uint8_t menu_active_device_index = 0U;
static uint8_t menu_device_edit_selection_index = 0U;
static uint8_t menu_device_cc_field_index = 0U;
static uint8_t menu_device_cc_field_edit_active = 0U;
static uint8_t menu_global_selection_index = 0U;
static uint8_t menu_function_button_message_selection_index = 0U;
static uint8_t menu_function_button_message_field_index = 0U;
static uint8_t menu_function_button_message_field_edit_active = 0U;
static DisplayMenuPage_t menu_page = DISPLAY_MENU_PAGE_ROOT;
static DisplayMenuPage_t menu_last_drawn_page = DISPLAY_MENU_PAGE_ROOT;
static DisplayMenuTextField_t menu_text_edit_field = DISPLAY_MENU_TEXT_FIELD_NONE;
static uint8_t menu_text_edit_cursor_index = 0U;
static uint8_t menu_draw_state_valid = 0U;
static const char menu_text_edit_charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 ";

#define BPM_DISPLAY_AREA_X              280U                 // left edge of the rectangle reserved for BPM text updates
#define BPM_DISPLAY_AREA_W              200U                 // width of the rectangle reserved for BPM text updates
#define BPM_SYNC_LOST_X                 280U                 // x position of the EXT SYNC LOST message
#define BPM_INTERNAL_VALUE_X            365U                 // x position of the internal BPM number block
#define BPM_INTERNAL_VALUE_W            (BPM_FONT.width * 3U) // width reserved for the 3-digit internal BPM number
#define BPM_INTERNAL_SUFFIX_X           (BPM_INTERNAL_VALUE_X + BPM_INTERNAL_VALUE_W) // x position where the internal BPM suffix would begin
#define BPM_INTERNAL_COLOUR             GREEN_WEB           // colour used for internal BPM text
#define BPM_BG_COLOUR                   DISPLAY_BG_COLOUR    // background colour behind all BPM text redraws
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
#define EXT_BPM_COLOUR                  COBALT_BLUE         // colour used for external BPM text
#define BPM_SYNC_LOST_TEXT              "EXT SYNC LOST"      // message shown when external MIDI clock times out
#define BPM_SYNC_LOST_COLOUR            RED                 // colour used for the EXT SYNC LOST warning

#define TRANSPORT_BARBEAT_TEXT_X        10U                  // top-left x position for bar.beat transport readout
#define TRANSPORT_BARBEAT_TEXT_Y        7U                   // top-left y position for bar.beat transport readout
#define TRANSPORT_BARBEAT_TEXT_CHARS    4U                   // fixed width for values like "64.4" or "-.-"
#define TRANSPORT_BARBEAT_TEXT_W        (TRANSPORT_BARBEAT_TEXT_CHARS * MAIN_PRESET_FONT.width) // clear/update width of bar.beat readout
#define TRANSPORT_BARBEAT_TEXT_COLOUR   MAIN_PRESET_COLOUR   // use preset font colour as requested

#define LOADING_BAR_X                   10U                  // left edge of the startup loading bar
#define LOADING_BAR_Y                   262U                 // top edge of the startup loading bar
#define LOADING_BAR_W                   460U                 // total drawable width of the startup loading bar
#define LOADING_BAR_H                   28U                  // height of the startup loading bar
#define LOADING_BAR_COLOUR              DARK_RED            // fill colour of the progress portion of the startup loading bar
#define LOADING_BAR_BG_COLOUR           DISPLAY_BG_COLOUR   // background colour behind the startup loading bar and its text row
#define LOADING_BAR_TEXT_Y              246U                // y position of the loading-bar status text line
#define LOADING_BAR_TEXT_FONT           Font_7x10           // font used for loading-bar status text
#define LOADING_BAR_TEXT_COLOUR         WHITE               // colour used for loading-bar status text
#define LOADING_BAR_PHASE_DIVISOR       3U                  // point where the first status-text phase change triggers
#define LOADING_BAR_PHASE_HOLD_MS       1000U               // time each loading-bar status message is held on screen
#define LOADING_BAR_WAIT_TEXT           "... waiting for DNA match" // first startup loading-bar message
#define LOADING_BAR_MATCH_TEXT          "DNA match found"  // second startup loading-bar message
#define LOADING_BAR_MARKERS_TEXT        "..accessing genetic markers" // third startup loading-bar message
#define LOADING_BAR_DONE_TEXT           "mutation complete" // final message shown when startup loading completes

#define SCREENSAVER_TIMEOUT_MIN_DEFAULT 10UL // idle time before the backlight-only screensaver activates

static uint32_t Display_GetConfiguredScreensaverTimeoutMs(void)
{
    const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();
    uint32_t timeout_minutes = SCREENSAVER_TIMEOUT_MIN_DEFAULT;

    if (global && global->screensaver_timeout_minutes > 0U)
        timeout_minutes = global->screensaver_timeout_minutes;

    return timeout_minutes * 60UL * 1000UL;
}

static void Display_FormatSpecialFunctionPrefix(char *buffer, size_t buffer_size)
{
    const RuntimeConfigFunctionButton_t *function_button = RuntimeConfig_GetFunctionButton(current_bank);
    const char *name = MAIN_SPECIAL_FUNCTION_BUTTON_DEFAULT_NAME;

    if (function_button && function_button->name[0] != '\0')
        name = function_button->name;

    if (buffer_size == 0U)
        return;

    (void)snprintf(buffer, buffer_size, "%s: ", name);
}

static const char *Display_GetSpecialFunctionStateLabel(uint8_t state_active)
{
    const RuntimeConfigFunctionButton_t *function_button = RuntimeConfig_GetFunctionButton(current_bank);

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

    return (uint8_t)((PRESET_DEVICE_SLOTS - (MAIN_INFO_ROW_COUNT - 1U)) + field.itemIndex);
}

static uint8_t Display_PresetEditFieldsMatch(DisplayPresetEditField_t first,
                                             DisplayPresetEditField_t second)
{
    return (first.type == second.type && first.itemIndex == second.itemIndex) ? 1U : 0U;
}

static uint8_t Display_GetPresetNameLength(const Preset_t *preset)
{
    if (!preset)
        return 0U;

    return (uint8_t)strnlen(preset->name, PRESET_NAME_LENGTH);
}

static uint8_t Display_GetPresetNameRenderLength(const Preset_t *preset)
{
    uint8_t name_length = Display_GetPresetNameLength(preset);

    return (name_length > 0U) ? name_length : 1U;
}

static uint8_t Display_GetPresetNamePadLeft(const Preset_t *preset)
{
    return (uint8_t)((PRESET_NAME_LENGTH - Display_GetPresetNameRenderLength(preset)) / 2U);
}

static uint8_t Display_GetPresetNameEditMaxIndex(const Preset_t *preset)
{
    return (uint8_t)(PRESET_NAME_LENGTH - Display_GetPresetNamePadLeft(preset) - 1U);
}

/* The preset name stays visually centred even when the stored string is
 * shorter than PRESET_NAME_LENGTH. These helpers translate between the logical
 * edit index inside the compact string and the physical cell index on screen. */
static uint16_t Display_GetPresetNameBaseX(void)
{
    return (uint16_t)((ST7796_WIDTH - (PRESET_NAME_LENGTH * MAIN_PRESET_FONT.width)) / 2U);
}

static uint8_t Display_GetPresetNameCellIndex(const Preset_t *preset, uint8_t logical_index)
{
    return (uint8_t)(Display_GetPresetNamePadLeft(preset) + logical_index);
}

/* Draw a single preset-name cell so cursor moves and single-character edits can
 * touch only the glyphs whose contents or highlight state actually changed. */
static void Display_DrawPresetNameCell(const Preset_t *preset, uint8_t cell_index)
{
    DisplayPresetEditField_t edit_field;
    uint8_t name_field_selected;
    uint8_t name_char_edit_active;
    uint8_t name_length;
    uint8_t render_length;
    uint8_t pad_left;
    int16_t logical_index;
    uint16_t char_x;
    uint16_t foreground = MAIN_PRESET_COLOUR;
    uint16_t background = MAIN_PRESET_BG_COLOUR;
    char ch = ' ';

    if (!preset || cell_index >= PRESET_NAME_LENGTH)
        return;

    edit_field = Display_PresetEditGetField();
    name_field_selected = (edit_field.type == DISPLAY_PRESET_EDIT_FIELD_NAME && preset_edit_mode_active && !preset_name_edit_active) ? 1U : 0U;
    name_char_edit_active = (edit_field.type == DISPLAY_PRESET_EDIT_FIELD_NAME && preset_edit_mode_active && preset_name_edit_active) ? 1U : 0U;
    name_length = Display_GetPresetNameLength(preset);
    render_length = Display_GetPresetNameRenderLength(preset);
    pad_left = Display_GetPresetNamePadLeft(preset);
    logical_index = (int16_t)cell_index - (int16_t)pad_left;
    char_x = (uint16_t)(Display_GetPresetNameBaseX() + (cell_index * MAIN_PRESET_FONT.width));

    if (logical_index >= 0 && logical_index < (int16_t)name_length)
        ch = preset->name[logical_index];

    if (name_field_selected
        && logical_index >= 0
        && logical_index < (int16_t)render_length)
    {
        foreground = MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR;
        background = MAIN_INFO_EDIT_CURSOR_BG_COLOUR;
    }

    if (name_char_edit_active && logical_index == (int16_t)preset_name_edit_cursor_index)
    {
        foreground = MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR;
        background = MAIN_INFO_EDIT_CURSOR_BG_COLOUR;
    }

    ST7796_DrawFilledRectangle(char_x,
                               MAIN_PRESET_TEXT_Y,
                               MAIN_PRESET_FONT.width,
                               MAIN_PRESET_FONT.height,
                               background);

    if (ch != ' ')
    {
        ST7796_WriteChar32(char_x,
                           MAIN_PRESET_TEXT_Y,
                           ch,
                           MAIN_PRESET_FONT,
                           foreground,
                           background);
    }
}

static void Display_UpdateBpmTextCells(uint16_t x,
                                       uint16_t y,
                                       uint8_t width_chars,
                                       const char *old_text,
                                       const char *new_text,
                                       uint16_t colour)
{
    size_t old_len = old_text ? strlen(old_text) : 0U;
    size_t new_len = new_text ? strlen(new_text) : 0U;

    for (uint8_t index = 0U; index < width_chars; index++)
    {
        char old_ch = (index < old_len) ? old_text[index] : ' ';
        char new_ch = (index < new_len) ? new_text[index] : ' ';

        if (old_ch == new_ch)
            continue;

        uint16_t char_x = (uint16_t)(x + ((uint16_t)index * BPM_FONT.width));
        ST7796_DrawFilledRectangle(char_x, y, BPM_FONT.width, BPM_FONT.height, BPM_BG_COLOUR);
        if (new_ch != ' ')
        {
            ST7796_WriteChar32(char_x, y, new_ch, BPM_FONT, colour, BPM_BG_COLOUR);
        }
    }
}

static void Display_UpdateTransportBarBeat(void)
{
    uint8_t bar;
    uint8_t beat;
    uint8_t external_signal_present;
    uint8_t sync_lost;
    char next_text[5];

    external_signal_present = MidiClockIsExternalSignalPresent();
    sync_lost = MidiClockIsSyncLost();

    if (MidiClockGetBarBeat(&bar, &beat))
    {
        if (bar > RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MAX)
            bar = RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MAX;
        if (beat > 9U)
            beat = 9U;

        if (bar >= 10U)
        {
            next_text[0] = (char)('0' + (bar / 10U));
            next_text[1] = (char)('0' + (bar % 10U));
            next_text[2] = '.';
            next_text[3] = (char)('0' + beat);
            next_text[4] = '\0';
        }
        else
        {
            next_text[0] = (char)('0' + bar);
            next_text[1] = '.';
            next_text[2] = (char)('0' + beat);
            next_text[3] = '\0';
        }
    }
    else if (external_signal_present || sync_lost)
    {
        strcpy(next_text, "-.-");
    }
    else
    {
        next_text[0] = '\0';
    }

    if (strcmp(next_text, transport_barbeat_text) == 0)
        return;

    {
        size_t old_len = strlen(transport_barbeat_text);
        size_t new_len = strlen(next_text);

        for (uint8_t index = 0U; index < TRANSPORT_BARBEAT_TEXT_CHARS; index++)
        {
            char old_ch = (index < old_len) ? transport_barbeat_text[index] : ' ';
            char new_ch = (index < new_len) ? next_text[index] : ' ';
            uint16_t char_x;

            if (old_ch == new_ch)
                continue;

            char_x = (uint16_t)(TRANSPORT_BARBEAT_TEXT_X + ((uint16_t)index * MAIN_PRESET_FONT.width));
            ST7796_DrawFilledRectangle(char_x,
                                       TRANSPORT_BARBEAT_TEXT_Y,
                                       MAIN_PRESET_FONT.width,
                                       MAIN_PRESET_FONT.height,
                                       DISPLAY_BG_COLOUR);
            if (new_ch != ' ')
            {
                ST7796_WriteChar32(char_x,
                                   TRANSPORT_BARBEAT_TEXT_Y,
                                   new_ch,
                                   MAIN_PRESET_FONT,
                                   TRANSPORT_BARBEAT_TEXT_COLOUR,
                                   DISPLAY_BG_COLOUR);
            }
        }
    }

    if (next_text[0] != '\0')
    {
        /* Characters are rendered above cell-by-cell; keep this branch only
         * to preserve the previous blank/non-blank intent. */
    }
    strcpy(transport_barbeat_text, next_text);
}

typedef enum
{
    BPM_TEXT_MODE_INTERNAL = 0,
    BPM_TEXT_MODE_EXTERNAL = 1
} DisplayBpmTextMode_t;

static void Display_FormatBpmText(char *buffer,
                                  size_t buffer_size,
                                  DisplayBpmTextMode_t mode,
                                  uint16_t bpm_or_bpm_x10)
{
    char text[20];

    if (mode == BPM_TEXT_MODE_EXTERNAL)
    {
        snprintf(text, sizeof(text), "EXT %u.%u BPM",
                 (unsigned)(bpm_or_bpm_x10 / 10U),
                 (unsigned)(bpm_or_bpm_x10 % 10U));

        snprintf(buffer, buffer_size, "%*s", (int)BPM_EXT_TEXT_CHARS, text);
        return;
    }

    snprintf(buffer, buffer_size, "INT %u", (unsigned)bpm_or_bpm_x10);
}

static uint16_t Display_GetExternalBpmHysteresisX10(uint16_t reference_bpm_x10)
{
    uint32_t hysteresis_x10 = (((uint32_t)reference_bpm_x10 * BPM_EXT_HYSTERESIS_BPS) + 5000U) / 10000U;

    if (hysteresis_x10 < BPM_EXT_HYSTERESIS_MIN_X10)
        hysteresis_x10 = BPM_EXT_HYSTERESIS_MIN_X10;

    return (uint16_t)hysteresis_x10;
}

static void Display_ClearBpmArea(void)
{
    ST7796_DrawFilledRectangle(BPM_DISPLAY_AREA_X, BPM_TEXT_Y, BPM_DISPLAY_AREA_W, BPM_FONT.height, BPM_BG_COLOUR);
}

static uint8_t Display_GetMainInfoProgramScrollMax(void)
{
    return (PRESET_DEVICE_SLOTS > MAIN_INFO_ROW_COUNT)
        ? (uint8_t)(PRESET_DEVICE_SLOTS - MAIN_INFO_ROW_COUNT)
        : 0U;
}

static uint8_t Display_GetMainInfoScrollMax(void)
{
    uint8_t total_rows = (uint8_t)(PRESET_DEVICE_SLOTS + PRESET_CC_SLOT_COUNT);

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

static uint8_t Display_GetMainInfoRightFirstItemRenderState(uint8_t first_item)
{
    uint8_t blank_first_item = (uint8_t)(PRESET_RELAY_COUNT + 1U);

    return (first_item > blank_first_item) ? blank_first_item : first_item;
}

static void Display_DrawMainInfoScrollIndicator(uint16_t x,
                                                uint16_t y,
                                                uint8_t point_up,
                                                uint8_t visible)
{
    ST7796_DrawFilledRectangle(x,
                               y,
                               MAIN_SCROLL_INDICATOR_W,
                               MAIN_SCROLL_INDICATOR_H,
                               DISPLAY_BG_COLOUR);
    if (!visible)
        return;

    for (uint8_t row = 0U; row < MAIN_SCROLL_INDICATOR_H; row++)
    {
        uint16_t line_y = point_up
            ? (uint16_t)(y + row)
            : (uint16_t)(y + (MAIN_SCROLL_INDICATOR_H - 1U - row));
        uint16_t line_x0 = (uint16_t)(x + ((MAIN_SCROLL_INDICATOR_W / 2U) - row));
        uint16_t line_x1 = (uint16_t)(x + ((MAIN_SCROLL_INDICATOR_W / 2U) + row));

        ST7796_DrawLine(line_x0,
                        line_y,
                        line_x1,
                        line_y,
                        MAIN_SCROLL_INDICATOR_COLOUR);
    }
}

static void Display_DrawMainInfoScrollIndicators(void)
{
    uint8_t max_scroll = Display_GetMainInfoScrollMax();
    uint16_t up_y = (uint16_t)(main_info_row_y[0] + ((MAIN_INFO_FONT.height - MAIN_SCROLL_INDICATOR_H) / 2U));
    uint16_t down_y = (uint16_t)(main_info_row_y[MAIN_INFO_ROW_COUNT - 1U] + ((MAIN_INFO_FONT.height - MAIN_SCROLL_INDICATOR_H) / 2U));

    Display_DrawMainInfoScrollIndicator(MAIN_SCROLL_INDICATOR_X,
                                        up_y,
                                        1U,
                                        (main_info_first_slot > 0U) ? 1U : 0U);
    Display_DrawMainInfoScrollIndicator(MAIN_SCROLL_INDICATOR_X,
                                        down_y,
                                        0U,
                                        (main_info_first_slot < max_scroll) ? 1U : 0U);
}

static void Display_DrawFootbarLabel(uint8_t section_index, const char *text)
{
    size_t text_len = strlen(text);
    uint16_t section_x = (uint16_t)(section_index * MAIN_FOOTBAR_SECTION_WIDTH);
    uint16_t text_w = (uint16_t)text_len * MAIN_FOOTBAR_FONT.width;
    uint16_t text_x = (uint16_t)(section_x + ((MAIN_FOOTBAR_SECTION_WIDTH - text_w) / 2U));
    uint16_t text_y = (uint16_t)(MAIN_FOOTBAR_Y + ((MAIN_FOOTBAR_H - MAIN_FOOTBAR_FONT.height) / 2U));

    if (text_len == 0U)
        return;

    ST7796_WriteString32(text_x,
                         text_y,
                         text,
                         MAIN_FOOTBAR_FONT,
                         MAIN_FOOTBAR_TEXT_COLOUR,
                         MAIN_FOOTBAR_COLOR);
}

static const char *Display_GetFootbarLabel(uint8_t section_index)
{
    if (menu_mode_active)
    {
        if (menu_page == DISPLAY_MENU_PAGE_DEVICE_INIT_CONFIRM)
        {
            switch (section_index)
            {
            case 0U:
                return MAIN_FOOTBAR_CONFIRM_LEFT_TEXT;
            case 1U:
                return MAIN_FOOTBAR_CONFIRM_CENTER_TEXT;
            case 2U:
                return MAIN_FOOTBAR_CONFIRM_RIGHT_TEXT;
            default:
                return "";
            }
        }

        switch (section_index)
        {
        case 0U:
            return MAIN_FOOTBAR_MENU_LEFT_TEXT;
        case 1U:
            return MAIN_FOOTBAR_MENU_CENTER_TEXT;
        case 2U:
            return MAIN_FOOTBAR_MENU_RIGHT_TEXT;
        default:
            return "";
        }
    }

    if (preset_edit_mode_active)
    {
        switch (section_index)
        {
        case 0U:
            return MAIN_FOOTBAR_EDIT_LEFT_TEXT;
        case 1U:
            return MAIN_FOOTBAR_EDIT_CENTER_TEXT;
        case 2U:
            return MAIN_FOOTBAR_EDIT_RIGHT_TEXT;
        default:
            return "";
        }
    }

    switch (section_index)
    {
    case 0U:
        return MAIN_FOOTBAR_LEFT_TEXT;
    case 1U:
        return MAIN_FOOTBAR_CENTER_TEXT;
    case 2U:
        return MAIN_FOOTBAR_RIGHT_TEXT;
    default:
        return "";
    }
}

static void Display_DrawFootbar(void)
{
    ST7796_DrawFilledRectangle(0U, MAIN_FOOTBAR_Y, ST7796_WIDTH, MAIN_FOOTBAR_H, MAIN_FOOTBAR_COLOR);

    for (uint8_t section_index = 0U; section_index < MAIN_FOOTBAR_SECTION_COUNT; ++section_index)
    {
        Display_DrawFootbarLabel(section_index, Display_GetFootbarLabel(section_index));
    }
}

static void Display_WriteCenteredPaddedText32WithBackground(uint16_t y,
                                                            const char *text,
                                                            uint8_t width_chars,
                                                            FontDef32 font,
                                                            uint16_t colour,
                                                            uint16_t background)
{
    char padded[MAIN_PRESET_TEXT_CHARS + 1U];
    size_t max_chars = (size_t)width_chars;
    size_t text_len = strnlen(text, max_chars);
    size_t pad_left = (max_chars - text_len) / 2U;

    memset(padded, ' ', max_chars);
    memcpy(padded + pad_left, text, text_len);
    padded[max_chars] = '\0';

    ST7796_WriteString32((uint16_t)((ST7796_WIDTH - ((uint16_t)width_chars * font.width)) / 2U),
                         y,
                         padded,
                         font,
                         colour,
                         background);
}

static void Display_WriteCenteredPaddedText32(uint16_t y,
                                              const char *text,
                                              uint8_t width_chars,
                                              FontDef32 font,
                                              uint16_t colour)
{
    Display_WriteCenteredPaddedText32WithBackground(y,
                                                    text,
                                                    width_chars,
                                                    font,
                                                    colour,
                                                    DISPLAY_BG_COLOUR);
}

static uint8_t Display_GetModeHeaderWidthChars(const char *text)
{
    size_t text_len = strlen(text);

    if (menu_mode_active)
    {
        uint8_t width_chars = (uint8_t)(text_len + MAIN_MODE_HEADER_MENU_PAD_CHARS);

        if (width_chars > MAIN_MODE_HEADER_MAX_TEXT_CHARS)
            width_chars = MAIN_MODE_HEADER_MAX_TEXT_CHARS;

        return width_chars;
    }

    return preset_edit_mode_active ? MAIN_MODE_HEADER_EDIT_TEXT_CHARS : MAIN_MODE_HEADER_TEXT_CHARS;
}

static const char *Display_GetMenuHeaderTextForPage(DisplayMenuPage_t page, char *buffer, size_t buffer_size)
{
    switch (page)
    {
    case DISPLAY_MENU_PAGE_BANKS:
        return "BANKS";
    case DISPLAY_MENU_PAGE_BANK_EDIT:
        (void)snprintf(buffer, buffer_size, "BANK %u", (uint8_t)(menu_active_bank_index + 1U));
        return buffer;
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON:
        return "FUNC BTN";
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES:
        return "FUNC BTN";
    case DISPLAY_MENU_PAGE_DEVICES:
        return "DEVICES";
    case DISPLAY_MENU_PAGE_DEVICE_EDIT:
        (void)snprintf(buffer, buffer_size, "DEVICE %u", (uint8_t)(menu_active_device_index + 1U));
        return buffer;
    case DISPLAY_MENU_PAGE_DEVICE_INIT_CONFIRM:
        return "CONFIRM";
    case DISPLAY_MENU_PAGE_GLOBAL:
        return "GLOBAL";
    case DISPLAY_MENU_PAGE_ROOT:
    default:
        return "MENU";
    }
}

static uint8_t Display_MenuPageUsesConfirmFootbar(DisplayMenuPage_t page)
{
    return (page == DISPLAY_MENU_PAGE_DEVICE_INIT_CONFIRM) ? 1U : 0U;
}

static uint8_t Display_MenuHeaderChanged(DisplayMenuPage_t previous_page, DisplayMenuPage_t current_page)
{
    char previous_text[12];
    char current_text[12];
    const char *previous_header = Display_GetMenuHeaderTextForPage(previous_page, previous_text, sizeof(previous_text));
    const char *current_header = Display_GetMenuHeaderTextForPage(current_page, current_text, sizeof(current_text));

    return (strcmp(previous_header, current_header) != 0) ? 1U : 0U;
}

static void Display_ClearMenuBody(void)
{
    ST7796_DrawFilledRectangle(0U,
                               MENU_BODY_Y,
                               ST7796_WIDTH,
                               MENU_BODY_H,
                               DISPLAY_BG_COLOUR);
}

static const char *Display_GetCurrentHeaderText(void)
{
    static char menu_header_text[12];

    if (menu_mode_active)
        return Display_GetMenuHeaderTextForPage(menu_page, menu_header_text, sizeof(menu_header_text));

    return preset_edit_mode_active ? "EDIT" : "LIVE";
}

static uint8_t Display_GetGlobalBrightnessUiValue(uint16_t brightness)
{
    if (brightness >= 4095U)
        return 255U;

    return (uint8_t)(((uint32_t)brightness * 255UL + 2047UL) / 4095UL);
}

static uint16_t Display_GetBrightnessFromUiValue(uint8_t ui_value)
{
    return (uint16_t)(((uint32_t)ui_value * 4095UL + 127UL) / 255UL);
}

static void Display_ApplyConfiguredBacklightBrightnessNow(void)
{
    if (Display_ScreensaverIsActive())
        return;

    DAC->DHR12R1 = Display_GetConfiguredBacklightBrightness();
}

static uint8_t Display_AdjustWrappedU8(uint8_t *value, uint8_t min_value, uint8_t max_value, int8_t delta)
{
    int32_t next_value;
    int32_t span;

    if (!value || min_value > max_value || delta == 0)
        return 0U;

    span = (int32_t)max_value - (int32_t)min_value + 1L;
    next_value = (int32_t)(*value) + (int32_t)delta;

    while (next_value < (int32_t)min_value)
        next_value += span;

    while (next_value > (int32_t)max_value)
        next_value -= span;

    if ((uint8_t)next_value == *value)
        return 0U;

    *value = (uint8_t)next_value;
    return 1U;
}

static uint8_t Display_AdjustWrappedOptionalU8(uint8_t *value,
                                               uint8_t unused_value,
                                               uint8_t min_value,
                                               uint8_t max_value,
                                               int8_t delta)
{
    int32_t current_value;
    int32_t next_value;
    int32_t unused_marker;
    int32_t span;

    if (!value || min_value > max_value || delta == 0)
        return 0U;

    unused_marker = (int32_t)min_value - 1L;
    span = (int32_t)max_value - (int32_t)min_value + 2L;
    current_value = (*value == unused_value) ? unused_marker : (int32_t)(*value);
    next_value = current_value + (int32_t)delta;

    while (next_value < unused_marker)
        next_value += span;

    while (next_value > (int32_t)max_value)
        next_value -= span;

    if (next_value == current_value)
        return 0U;

    *value = (next_value == unused_marker) ? unused_value : (uint8_t)next_value;
    return 1U;
}

static uint8_t Display_GetMenuFirstVisibleIndex(uint8_t item_count, uint8_t selected_index)
{
    if (item_count <= MENU_VISIBLE_ROW_COUNT)
        return 0U;

    if (selected_index < MENU_VISIBLE_ROW_COUNT)
        return 0U;

    if (selected_index >= item_count)
        selected_index = (uint8_t)(item_count - 1U);

    return (uint8_t)(selected_index - (MENU_VISIBLE_ROW_COUNT - 1U));
}

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

    return (uint8_t)(selected_index - (visible_count - 1U));
}

static DisplayMenuTextField_t Display_GetMenuTextFieldForSelection(void)
{
    if (menu_page == DISPLAY_MENU_PAGE_BANK_EDIT)
    {
        if (menu_bank_edit_selection_index == 0U)
            return DISPLAY_MENU_TEXT_FIELD_BANK_NAME;

        return DISPLAY_MENU_TEXT_FIELD_NONE;
    }

    if (menu_page == DISPLAY_MENU_PAGE_FUNCTION_BUTTON)
    {
        switch (menu_function_button_selection_index)
        {
        case 0U:
            return DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_NAME;
        case 1U:
            return DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_ACTIVE_LABEL;
        case 2U:
            return DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_INACTIVE_LABEL;
        default:
            return DISPLAY_MENU_TEXT_FIELD_NONE;
        }
    }

    if (menu_page == DISPLAY_MENU_PAGE_DEVICE_EDIT)
    {
        if (menu_device_edit_selection_index == 0U)
            return DISPLAY_MENU_TEXT_FIELD_DEVICE_NAME;
    }

    return DISPLAY_MENU_TEXT_FIELD_NONE;
}

static uint8_t Display_GetMenuTextFieldLength(DisplayMenuTextField_t field)
{
    switch (field)
    {
    case DISPLAY_MENU_TEXT_FIELD_BANK_NAME:
        return RUNTIME_CONFIG_BANK_NAME_LENGTH;
    case DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_NAME:
        return RUNTIME_CONFIG_FUNCTION_BUTTON_NAME_LENGTH;
    case DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_ACTIVE_LABEL:
    case DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_INACTIVE_LABEL:
        return RUNTIME_CONFIG_FUNCTION_BUTTON_LABEL_LENGTH;
    case DISPLAY_MENU_TEXT_FIELD_DEVICE_NAME:
        return RUNTIME_CONFIG_DEVICE_NAME_LENGTH;
    default:
        return 0U;
    }
}

static size_t Display_GetMenuTextFieldCapacity(DisplayMenuTextField_t field)
{
    return (size_t)Display_GetMenuTextFieldLength(field) + 1U;
}

static char *Display_GetMenuTextFieldPointer(DisplayMenuTextField_t field)
{
    RuntimeConfigBank_t *bank = RuntimeConfig_GetMutableBank(menu_active_bank_index);
    RuntimeConfigFunctionButton_t *function_button = RuntimeConfig_GetMutableFunctionButton(menu_active_bank_index);
    RuntimeConfigDevice_t *device = RuntimeConfig_GetMutableDevice(menu_active_device_index);

    switch (field)
    {
    case DISPLAY_MENU_TEXT_FIELD_BANK_NAME:
        return bank ? bank->name : NULL;
    case DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_NAME:
        return function_button ? function_button->name : NULL;
    case DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_ACTIVE_LABEL:
        return function_button ? function_button->active_label : NULL;
    case DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_INACTIVE_LABEL:
        return function_button ? function_button->inactive_label : NULL;
    case DISPLAY_MENU_TEXT_FIELD_DEVICE_NAME:
        return device ? device->name : NULL;
    default:
        return NULL;
    }
}

static void Display_LoadMenuTextCells(const char *source, uint8_t cell_count, char *cells)
{
    size_t text_length;

    memset(cells, ' ', cell_count);

    if (!source)
        return;

    text_length = strnlen(source, cell_count);
    memcpy(cells, source, text_length);
}

static void Display_StoreMenuTextCells(char *destination,
                                       size_t destination_size,
                                       const char *cells,
                                       uint8_t cell_count)
{
    int16_t last_non_space_index;

    if (!destination || destination_size == 0U || !cells)
        return;

    memset(destination, 0, destination_size);

    for (last_non_space_index = (int16_t)cell_count - 1; last_non_space_index >= 0; --last_non_space_index)
    {
        if (cells[last_non_space_index] != ' ')
            break;
    }

    if (last_non_space_index < 0)
        return;

    memcpy(destination, cells, (size_t)last_non_space_index + 1U);
    destination[last_non_space_index + 1] = '\0';
}

static int16_t Display_FindMenuTextCharsetIndex(char ch)
{
    for (uint8_t index = 0U; index < (sizeof(menu_text_edit_charset) - 1U); ++index)
    {
        if (menu_text_edit_charset[index] == ch)
            return (int16_t)index;
    }

    return 0;
}

static void Display_DrawMenuBankEditItem(uint8_t item_index);
static void Display_DrawMenuFunctionButtonItem(uint8_t item_index);
static void Display_DrawMenuDeviceItem(uint8_t item_index);
static void Display_DrawMenuDeviceEditItem(uint8_t item_index);
static void Display_DrawMenuGlobalItem(uint8_t item_index);
static void Display_MenuRedrawCurrentItem(void);
static void Display_MenuRedrawCurrentValue(void);
static void Display_MenuRedrawSelectionChange(DisplayMenuPage_t page, uint8_t previous_selection);
static void Display_MenuRefreshBodyOnly(void);

static void Display_MenuTextEditEnter(DisplayMenuTextField_t field)
{
    if (field == DISPLAY_MENU_TEXT_FIELD_NONE)
        return;

    menu_text_edit_field = field;
    menu_text_edit_cursor_index = 0U;
    Display_MenuRedrawCurrentItem();
}

static uint8_t Display_MenuAdjustTextCharacter(int8_t delta)
{
    char cells[RUNTIME_CONFIG_BANK_NAME_LENGTH];
    char *text_field;
    uint8_t cell_count;
    int16_t current_charset_index;
    int16_t next_charset_index;
    int16_t charset_length = (int16_t)(sizeof(menu_text_edit_charset) - 1U);

    if (delta == 0 || menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_NONE)
        return 0U;

    text_field = Display_GetMenuTextFieldPointer(menu_text_edit_field);
    cell_count = Display_GetMenuTextFieldLength(menu_text_edit_field);
    if (!text_field || cell_count == 0U || cell_count > sizeof(cells) || menu_text_edit_cursor_index >= cell_count)
        return 0U;

    Display_LoadMenuTextCells(text_field, cell_count, cells);
    current_charset_index = Display_FindMenuTextCharsetIndex(cells[menu_text_edit_cursor_index]);
    next_charset_index = current_charset_index + (int16_t)delta;

    while (next_charset_index < 0)
        next_charset_index += charset_length;

    while (next_charset_index >= charset_length)
        next_charset_index -= charset_length;

    if (cells[menu_text_edit_cursor_index] == menu_text_edit_charset[next_charset_index])
        return 0U;

    cells[menu_text_edit_cursor_index] = menu_text_edit_charset[next_charset_index];
    Display_StoreMenuTextCells(text_field,
                               Display_GetMenuTextFieldCapacity(menu_text_edit_field),
                               cells,
                               cell_count);
    return 1U;
}

static uint8_t Display_MenuFunctionButtonMessagePageIsActive(void)
{
    return (menu_page == DISPLAY_MENU_PAGE_FUNCTION_BUTTON
         && menu_function_button_selection_index >= MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX) ? 1U : 0U;
}

static uint8_t Display_GetFunctionButtonMessageFieldCount(uint8_t row_index)
{
    return (row_index < RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT) ? 4U : 6U;
}

static uint8_t Display_GetFunctionButtonFocusFieldCount(uint8_t selection_index)
{
    if (selection_index < MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX)
        return 1U;

    return Display_GetFunctionButtonMessageFieldCount((uint8_t)(selection_index - MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX));
}

static uint16_t Display_GetFunctionButtonFocusCount(void)
{
    uint16_t focus_count = 0U;

    for (uint8_t selection_index = 0U; selection_index < MENU_FUNCTION_BUTTON_ITEM_COUNT; ++selection_index)
        focus_count = (uint16_t)(focus_count + Display_GetFunctionButtonFocusFieldCount(selection_index));

    return focus_count;
}

static uint16_t Display_GetFunctionButtonFocusIndex(void)
{
    uint16_t focus_index = 0U;

    for (uint8_t selection_index = 0U; selection_index < menu_function_button_selection_index; ++selection_index)
        focus_index = (uint16_t)(focus_index + Display_GetFunctionButtonFocusFieldCount(selection_index));

    if (Display_MenuFunctionButtonMessagePageIsActive())
    {
        uint8_t field_count = Display_GetFunctionButtonFocusFieldCount(menu_function_button_selection_index);

        if (menu_function_button_message_field_index >= field_count)
            menu_function_button_message_field_index = 0U;

        focus_index = (uint16_t)(focus_index + menu_function_button_message_field_index);
    }

    return focus_index;
}

static void Display_SetFunctionButtonFocusIndex(uint16_t focus_index)
{
    for (uint8_t selection_index = 0U; selection_index < MENU_FUNCTION_BUTTON_ITEM_COUNT; ++selection_index)
    {
        uint8_t field_count = Display_GetFunctionButtonFocusFieldCount(selection_index);

        if (focus_index < field_count)
        {
            menu_function_button_selection_index = selection_index;
            menu_function_button_message_field_index = (selection_index < MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX)
                ? 0U
                : (uint8_t)focus_index;
            return;
        }

        focus_index = (uint16_t)(focus_index - field_count);
    }

    menu_function_button_selection_index = (uint8_t)(MENU_FUNCTION_BUTTON_ITEM_COUNT - 1U);
    menu_function_button_message_field_index = (uint8_t)(Display_GetFunctionButtonFocusFieldCount(menu_function_button_selection_index) - 1U);
}

static uint8_t Display_GetDeviceEditFocusFieldCount(uint8_t selection_index)
{
    return (selection_index >= 3U && selection_index <= 6U) ? 2U : 1U;
}

static uint16_t Display_GetDeviceEditFocusCount(void)
{
    uint16_t focus_count = 0U;

    for (uint8_t selection_index = 0U; selection_index < MENU_DEVICE_EDIT_ITEM_COUNT; ++selection_index)
        focus_count = (uint16_t)(focus_count + Display_GetDeviceEditFocusFieldCount(selection_index));

    return focus_count;
}

static uint16_t Display_GetDeviceEditFocusIndex(void)
{
    uint16_t focus_index = 0U;

    for (uint8_t selection_index = 0U; selection_index < menu_device_edit_selection_index; ++selection_index)
        focus_index = (uint16_t)(focus_index + Display_GetDeviceEditFocusFieldCount(selection_index));

    if (menu_page == DISPLAY_MENU_PAGE_DEVICE_EDIT
     && menu_device_edit_selection_index >= 3U
     && menu_device_edit_selection_index <= 6U)
    {
        uint8_t field_count = Display_GetDeviceEditFocusFieldCount(menu_device_edit_selection_index);

        if (menu_device_cc_field_index >= field_count)
            menu_device_cc_field_index = 0U;

        focus_index = (uint16_t)(focus_index + menu_device_cc_field_index);
    }

    return focus_index;
}

static void Display_SetDeviceEditFocusIndex(uint16_t focus_index)
{
    for (uint8_t selection_index = 0U; selection_index < MENU_DEVICE_EDIT_ITEM_COUNT; ++selection_index)
    {
        uint8_t field_count = Display_GetDeviceEditFocusFieldCount(selection_index);

        if (focus_index < field_count)
        {
            menu_device_edit_selection_index = selection_index;
            menu_device_cc_field_index = (selection_index >= 3U && selection_index <= 6U)
                ? (uint8_t)focus_index
                : 0U;
            return;
        }

        focus_index = (uint16_t)(focus_index - field_count);
    }

    menu_device_edit_selection_index = (uint8_t)(MENU_DEVICE_EDIT_ITEM_COUNT - 1U);
    menu_device_cc_field_index = 0U;
}

static void Display_FormatMenuOptionalField(char *buffer,
                                            size_t buffer_size,
                                            uint8_t value,
                                            uint8_t unused_value,
                                            uint8_t digits,
                                            uint8_t highlighted)
{
    char field_text[5];

    if (!buffer || buffer_size == 0U || digits == 0U || digits >= sizeof(field_text))
        return;

    if (value == unused_value)
    {
        memset(field_text, '-', digits);
        field_text[digits] = '\0';
    }
    else
        (void)snprintf(field_text, sizeof(field_text), "%*u", digits, value);

    (void)highlighted;
    (void)snprintf(buffer, buffer_size, "%s", field_text);
}

static void Display_FormatMenuNumericField(char *buffer,
                                           size_t buffer_size,
                                           uint8_t value,
                                           uint8_t digits,
                                           uint8_t highlighted)
{
    char field_text[5];

    if (!buffer || buffer_size == 0U || digits == 0U || digits >= sizeof(field_text))
        return;

    (void)snprintf(field_text, sizeof(field_text), "%*u", digits, value);

    (void)highlighted;
    (void)snprintf(buffer, buffer_size, "%s", field_text);
}

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

static void Display_DrawMenuTextSegment32(uint16_t x,
                                          uint16_t row_y,
                                          const char *text,
                                          uint8_t highlighted)
{
    size_t text_length = text ? strlen(text) : 0U;
    uint16_t segment_bg;
    uint16_t segment_fg;

    if (text_length == 0U)
        return;

    segment_bg = Display_GetMenuRowBackgroundColour(highlighted);
    segment_fg = Display_GetMenuRowForegroundColour(highlighted);

    ST7796_DrawFilledRectangle(x,
                               row_y,
                               (uint16_t)(text_length * MAIN_INFO_FONT.width),
                               MAIN_INFO_FONT.height,
                               segment_bg);
    ST7796_WriteString32(x,
                         row_y,
                         text,
                         MAIN_INFO_FONT,
                         segment_fg,
                         segment_bg);
}

static uint16_t Display_WriteMenuValueSegment32(uint16_t x,
                                                uint16_t y,
                                                const char *text,
                                                uint8_t highlighted)
{
    if (!text)
        return x;

    ST7796_WriteString32(x,
                         y,
                         text,
                         MAIN_INFO_FONT,
                         highlighted ? MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR : MAIN_INFO_TEXT_COLOUR,
                         highlighted ? MAIN_INFO_EDIT_CURSOR_BG_COLOUR : DISPLAY_BG_COLOUR);

    return (uint16_t)(x + ((uint16_t)strlen(text) * MAIN_INFO_FONT.width));
}

static void Display_DrawMenuTextEditValue(uint16_t row_y,
                                          const char *source,
                                          uint8_t cell_count)
{
    char cells[RUNTIME_CONFIG_BANK_NAME_LENGTH];
    uint16_t value_x;

    if (cell_count == 0U || cell_count > sizeof(cells))
        return;

    Display_LoadMenuTextCells(source, cell_count, cells);
    value_x = (uint16_t)(ST7796_WIDTH - MENU_ITEM_X - ((uint16_t)cell_count * MAIN_INFO_FONT.width));

    for (uint8_t index = 0U; index < cell_count; ++index)
    {
        uint16_t char_x = (uint16_t)(value_x + (index * MAIN_INFO_FONT.width));
        uint8_t highlighted = (index == menu_text_edit_cursor_index) ? 1U : 0U;
        uint16_t foreground = highlighted ? MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR : MAIN_INFO_TEXT_COLOUR;
        uint16_t background = highlighted ? MAIN_INFO_EDIT_CURSOR_BG_COLOUR : DISPLAY_BG_COLOUR;

        ST7796_DrawFilledRectangle(char_x,
                                   row_y,
                                   MAIN_INFO_FONT.width,
                                   MAIN_INFO_FONT.height,
                                   background);

        if (cells[index] != ' ')
        {
            ST7796_WriteChar32(char_x,
                               row_y,
                               cells[index],
                               MAIN_INFO_FONT,
                               foreground,
                               background);
        }
    }
}

static void Display_DrawMenuDeviceCcEditFields(uint16_t row_y, const MidiCC_t *cc)
{
    char cc_number_text[4];
    char value_text[4];
    uint16_t value_x;

    if (!cc)
        return;

    Display_FormatMenuOptionalField(cc_number_text,
                                    sizeof(cc_number_text),
                                    cc->cc,
                                    PRESET_CC_NUMBER_UNUSED,
                                    3U,
                                    0U);
    Display_FormatMenuNumericField(value_text,
                                   sizeof(value_text),
                                   cc->value,
                                   3U,
                                   0U);

    value_x = Display_GetMenuRightAlignedValueX("CC:--- VAL:---");
    value_x = Display_WriteMenuValueSegment32(value_x, row_y, "CC:", 0U);
    value_x = Display_WriteMenuValueSegment32(value_x,
                                              row_y,
                                              cc_number_text,
                                              (menu_device_cc_field_index == 0U) ? 1U : 0U);
    value_x = Display_WriteMenuValueSegment32(value_x, row_y, " VAL:", 0U);
    (void)Display_WriteMenuValueSegment32(value_x,
                                          row_y,
                                          value_text,
                                          (menu_device_cc_field_index == 1U) ? 1U : 0U);
}

static void Display_ResetFunctionButtonMessageEditor(void)
{
    menu_function_button_message_selection_index = 0U;
    menu_function_button_message_field_index = 0U;
    menu_function_button_message_field_edit_active = 0U;
}

static void Display_DrawMenuRow(uint16_t row_y,
                                const char *label,
                                const char *value,
                                uint8_t selected)
{
    uint16_t value_x = MENU_ITEM_X;
    uint8_t highlight_value = (selected && value && value[0] != '\0') ? 1U : 0U;
    uint8_t highlight_label = (selected && !highlight_value) ? 1U : 0U;

    ST7796_DrawFilledRectangle(0U,
                               row_y,
                               ST7796_WIDTH,
                               MAIN_INFO_FONT.height,
                               DISPLAY_BG_COLOUR);

    Display_DrawMenuTextSegment32(MENU_ITEM_X,
                                  row_y,
                                  label,
                                  highlight_label);

    if (!value || value[0] == '\0')
        return;

    value_x = Display_GetMenuRightAlignedValueX(value);
    Display_DrawMenuTextSegment32(value_x,
                                  row_y,
                                  value,
                                  highlight_value);
}

static void Display_DrawMenuRowValueOnly(uint16_t row_y,
                                         const char *label,
                                         const char *value,
                                         uint8_t selected)
{
    uint16_t value_clear_x = MENU_ITEM_X;
    uint16_t value_x;
    size_t label_length = label ? strlen(label) : 0U;
    uint16_t row_right_x = (uint16_t)(ST7796_WIDTH - MENU_ITEM_X);

    if (label_length > 0U)
    {
        value_clear_x = (uint16_t)(MENU_ITEM_X + ((uint16_t)(label_length + 1U) * MAIN_INFO_FONT.width));
        if (value_clear_x > row_right_x)
            value_clear_x = row_right_x;
    }

    ST7796_DrawFilledRectangle(value_clear_x,
                               row_y,
                               (uint16_t)(row_right_x - value_clear_x),
                               MAIN_INFO_FONT.height,
                               DISPLAY_BG_COLOUR);

    if (!value || value[0] == '\0')
        return;

    value_x = Display_GetMenuRightAlignedValueX(value);
    if (value_x < value_clear_x)
        value_x = value_clear_x;

    Display_DrawMenuTextSegment32(value_x,
                                  row_y,
                                  value,
                                  selected);
}

static void Display_DrawMainModeHeader(void)
{
    const char *header_text = Display_GetCurrentHeaderText();
    uint8_t header_width_chars = Display_GetModeHeaderWidthChars(header_text);
    uint16_t clear_w = (uint16_t)(MAIN_MODE_HEADER_MAX_TEXT_CHARS * MAIN_MODE_HEADER_FONT.width);
    uint16_t clear_x = (uint16_t)((ST7796_WIDTH - clear_w) / 2U);

    ST7796_DrawFilledRectangle(clear_x,
                               MAIN_MODE_HEADER_TEXT_Y,
                               clear_w,
                               MAIN_MODE_HEADER_FONT.height,
                               DISPLAY_BG_COLOUR);

    Display_WriteCenteredPaddedText32WithBackground(MAIN_MODE_HEADER_TEXT_Y,
                                                    header_text,
                                                    header_width_chars,
                                                    MAIN_MODE_HEADER_FONT,
                                                    (preset_edit_mode_active || menu_mode_active) ? MAIN_MODE_HEADER_EDIT_COLOUR : MAIN_MODE_HEADER_COLOUR,
                                                    (preset_edit_mode_active || menu_mode_active) ? MAIN_MODE_HEADER_EDIT_BG_COLOUR : DISPLAY_BG_COLOUR);
}

static void Display_DrawPresetName(const Preset_t *preset)
{
    uint16_t base_x = Display_GetPresetNameBaseX();

    if (!preset)
        return;

    preset_name_full_refresh_pending = 0U;
    preset_name_last_render_length = Display_GetPresetNameRenderLength(preset);
    preset_name_last_pad_left = Display_GetPresetNamePadLeft(preset);

    ST7796_DrawFilledRectangle(base_x,
                               MAIN_PRESET_TEXT_Y,
                               PRESET_NAME_LENGTH * MAIN_PRESET_FONT.width,
                               MAIN_PRESET_FONT.height,
                               MAIN_PRESET_BG_COLOUR);

    for (uint8_t cell_index = 0U; cell_index < PRESET_NAME_LENGTH; ++cell_index)
        Display_DrawPresetNameCell(preset, cell_index);
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

        ST7796_WriteString32(MAIN_INFO_LEFT_X,
                             row_y,
                             prefix,
                             MAIN_INFO_FONT,
                             MAIN_INFO_TEXT_COLOUR,
                             MAIN_INFO_TEXT_BG_COLOUR);
        ST7796_WriteString32(value_x,
                             row_y,
                             "---",
                             MAIN_INFO_FONT,
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

    ST7796_WriteString32(MAIN_INFO_LEFT_X,
                         row_y,
                         prefix,
                         MAIN_INFO_FONT,
                         MAIN_INFO_TEXT_COLOUR,
                         MAIN_INFO_TEXT_BG_COLOUR);

    {
        DisplayPresetEditField_t edit_field = Display_PresetEditGetField();
        uint8_t highlight_program = (edit_field.type == DISPLAY_PRESET_EDIT_FIELD_PROGRAM && edit_field.itemIndex == slot_index) ? 1U : 0U;
        char program_text[MAIN_INFO_PROGRAM_DIGITS + 1U];
        uint8_t program = preset->prg[slot_index].program;
        uint8_t program_is_shared = (program != MAIN_UNUSED_PROGRAM && Presets_DeviceProgramIsShared(slot_index, program)) ? 1U : 0U;

        if (program == MAIN_UNUSED_PROGRAM)
            strcpy(program_text, "---");
        else
            snprintf(program_text, sizeof(program_text), "%3u", program);

        if (highlight_program)
        {
            ST7796_WriteString32(value_x,
                                 row_y,
                                 program_text,
                                 MAIN_INFO_FONT,
                                 MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR,
                                 program_is_shared ? MAIN_INFO_EDIT_CURSOR_SHARED_BG_COLOUR : MAIN_INFO_EDIT_CURSOR_BG_COLOUR);
            return;
        }

        if (program_is_shared)
        {
            ST7796_WriteString32(value_x,
                                 row_y,
                                 program_text,
                                 MAIN_INFO_FONT,
                                 MAIN_INFO_SHARED_TEXT_COLOUR,
                                 MAIN_INFO_SHARED_BG_COLOUR);
            return;
        }

        ST7796_WriteString32(value_x,
                             row_y,
                             program_text,
                             MAIN_INFO_FONT,
                             MAIN_INFO_TEXT_COLOUR,
                             MAIN_INFO_TEXT_BG_COLOUR);
    }
}

static void Display_DrawMainInfoProgramField(const Preset_t *preset,
                                             uint8_t slot_index,
                                             uint16_t row_y,
                                             uint8_t highlight_program)
{
    char program_text[MAIN_INFO_PROGRAM_DIGITS + 1U];
    const MidiDevice_t *device = MidiDevices_Get(slot_index);
    uint8_t program;
    uint8_t channel = device ? device->channel : MAIN_UNUSED_PROGRAM;
    uint16_t value_x;
    uint8_t program_is_shared;

    if (!preset || channel == MAIN_UNUSED_PROGRAM)
        return;

    program = preset->prg[slot_index].program;
    program_is_shared = (program != MAIN_UNUSED_PROGRAM && Presets_DeviceProgramIsShared(slot_index, program)) ? 1U : 0U;
    value_x = (uint16_t)(MAIN_INFO_LEFT_X
                       + ((((uint16_t)(RUNTIME_CONFIG_DEVICE_NAME_LENGTH + 2U) > strlen("CH 16: "))
                           ? (uint16_t)(RUNTIME_CONFIG_DEVICE_NAME_LENGTH + 2U)
                           : (uint16_t)strlen("CH 16: ")) * MAIN_INFO_FONT.width));

    if (program == MAIN_UNUSED_PROGRAM)
        strcpy(program_text, "---");
    else
        snprintf(program_text, sizeof(program_text), "%3u", program);

    if (highlight_program)
    {
        ST7796_WriteString32(value_x,
                             row_y,
                             program_text,
                             MAIN_INFO_FONT,
                             MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR,
                             program_is_shared ? MAIN_INFO_EDIT_CURSOR_SHARED_BG_COLOUR : MAIN_INFO_EDIT_CURSOR_BG_COLOUR);
        return;
    }

    if (program_is_shared)
    {
        ST7796_WriteString32(value_x,
                             row_y,
                             program_text,
                             MAIN_INFO_FONT,
                             MAIN_INFO_SHARED_TEXT_COLOUR,
                             MAIN_INFO_SHARED_BG_COLOUR);
        return;
    }

    ST7796_WriteString32(value_x,
                         row_y,
                         program_text,
                         MAIN_INFO_FONT,
                         MAIN_INFO_TEXT_COLOUR,
                         MAIN_INFO_TEXT_BG_COLOUR);
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

    Display_FormatMainInfoCcLabel(cc_index, cc_label_text, sizeof(cc_label_text));
    ST7796_WriteString32(MAIN_INFO_LEFT_X,
                         row_y,
                         cc_label_text,
                         MAIN_INFO_FONT,
                         MAIN_INFO_TEXT_COLOUR,
                         MAIN_INFO_TEXT_BG_COLOUR);
}

static void Display_DrawMainInfoCcField(const Preset_t *preset,
                                        uint8_t cc_index,
                                        DisplayPresetEditFieldType_t field_type,
                                        uint16_t row_y,
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

    ST7796_WriteString32(field_x,
                         row_y,
                         field_text,
                         MAIN_INFO_FONT,
                         foreground,
                         background);
}

static void Display_DrawMainInfoCcFields(const Preset_t *preset,
                                         uint8_t cc_index,
                                         uint16_t row_y)
{
    DisplayPresetEditField_t edit_field = Display_PresetEditGetField();
    uint8_t highlight_channel = (edit_field.type == DISPLAY_PRESET_EDIT_FIELD_CC_CHANNEL && edit_field.itemIndex == cc_index) ? 1U : 0U;
    uint8_t highlight_cc_number = (edit_field.type == DISPLAY_PRESET_EDIT_FIELD_CC_NUMBER && edit_field.itemIndex == cc_index) ? 1U : 0U;
    uint8_t highlight_value = (edit_field.type == DISPLAY_PRESET_EDIT_FIELD_CC_VALUE && edit_field.itemIndex == cc_index) ? 1U : 0U;

    Display_DrawMainInfoCcField(preset,
                                cc_index,
                                DISPLAY_PRESET_EDIT_FIELD_CC_CHANNEL,
                                row_y,
                                highlight_channel);
    Display_DrawMainInfoCcField(preset,
                                cc_index,
                                DISPLAY_PRESET_EDIT_FIELD_CC_NUMBER,
                                row_y,
                                highlight_cc_number);
    Display_DrawMainInfoCcField(preset,
                                cc_index,
                                DISPLAY_PRESET_EDIT_FIELD_CC_VALUE,
                                row_y,
                                highlight_value);
}

static void Display_DrawMainInfoRelayField(const Preset_t *preset,
                                           uint8_t relay_index,
                                           uint16_t row_y,
                                           uint8_t highlight_state)
{
    char state_text[8];
    uint16_t state_x;

    if (!preset || relay_index >= PRESET_RELAY_COUNT)
        return;

    snprintf(state_text, sizeof(state_text), "%-6s", preset->relay[relay_index] ? "closed" : "open");
    state_x = (uint16_t)(MAIN_INFO_RIGHT_X + (strlen("Relay_0: ") * MAIN_INFO_FONT.width));

    ST7796_WriteString32(state_x,
                         row_y,
                         state_text,
                         MAIN_INFO_FONT,
                         highlight_state ? MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR : MAIN_INFO_TEXT_COLOUR,
                         highlight_state ? MAIN_INFO_EDIT_CURSOR_BG_COLOUR : MAIN_INFO_TEXT_BG_COLOUR);
}

static void Display_DrawMainInfoCcRow(const Preset_t *preset,
                                      uint8_t cc_index,
                                      uint16_t row_y)
{
    uint16_t draw_x = MAIN_INFO_LEFT_X;
    Display_DrawMainInfoCcLabel(cc_index, row_y);
    draw_x = (uint16_t)(draw_x + (MAIN_INFO_CC_LABEL_CHARS * MAIN_INFO_FONT.width));

    ST7796_WriteString32(draw_x,
                         row_y,
                         MAIN_INFO_CC_CHANNEL_PREFIX,
                         MAIN_INFO_FONT,
                         MAIN_INFO_TEXT_COLOUR,
                         MAIN_INFO_TEXT_BG_COLOUR);
    draw_x = (uint16_t)(draw_x + (strlen(MAIN_INFO_CC_CHANNEL_PREFIX) * MAIN_INFO_FONT.width));
    draw_x = (uint16_t)(draw_x + (MAIN_INFO_CC_CHANNEL_DIGITS * MAIN_INFO_FONT.width));

    ST7796_WriteString32(draw_x,
                         row_y,
                         MAIN_INFO_CC_NUMBER_PREFIX,
                         MAIN_INFO_FONT,
                         MAIN_INFO_TEXT_COLOUR,
                         MAIN_INFO_TEXT_BG_COLOUR);
    draw_x = (uint16_t)(draw_x + (strlen(MAIN_INFO_CC_NUMBER_PREFIX) * MAIN_INFO_FONT.width));
    draw_x = (uint16_t)(draw_x + (MAIN_INFO_CC_VALUE_DIGITS * MAIN_INFO_FONT.width));

    ST7796_WriteString32(draw_x,
                         row_y,
                         MAIN_INFO_CC_VALUE_PREFIX,
                         MAIN_INFO_FONT,
                         MAIN_INFO_TEXT_COLOUR,
                         MAIN_INFO_TEXT_BG_COLOUR);
    Display_DrawMainInfoCcFields(preset, cc_index, row_y);
}

static void Display_DrawMainInfoSpecialState(uint16_t row_y)
{
    char prefix[RUNTIME_CONFIG_FUNCTION_BUTTON_NAME_LENGTH + 3U];
    uint8_t state_active = Button_SpecialFunctionsActive();
    const char *state = Display_GetSpecialFunctionStateLabel(state_active);

    Display_FormatSpecialFunctionPrefix(prefix, sizeof(prefix));

    uint16_t prefix_px = MAIN_INFO_FONT.width * (uint16_t)strlen(prefix);
    uint16_t state_x = MAIN_INFO_RIGHT_X + prefix_px;
    uint16_t state_w = MAIN_INFO_FONT.width * (uint16_t)strlen(state);

    ST7796_WriteString32(MAIN_INFO_RIGHT_X,
                         row_y,
                         prefix,
                         MAIN_INFO_FONT,
                         MAIN_SPECIAL_FUNCTION_BUTTON_PREFIX_COLOUR,
                         MAIN_SPECIAL_FUNCTION_BUTTON_PREFIX_BG);
    ST7796_WriteString32(state_x,
                         row_y,
                         state,
                         MAIN_INFO_FONT,
                         state_active ? MAIN_SPECIAL_FUNCTION_BUTTON_ACTIVE_COLOUR : MAIN_SPECIAL_FUNCTION_BUTTON_INACTIVE_COLOUR,
                         state_active ? MAIN_SPECIAL_FUNCTION_BUTTON_ACTIVE_BG : MAIN_SPECIAL_FUNCTION_BUTTON_INACTIVE_BG);

    if (!state_active)
        return;

    ST7796_DrawFilledRectangle(state_x,
                               row_y,
                               state_w,
                               MAIN_INFO_HIGHLIGHT_BORDER_H,
                               MAIN_SPECIAL_FUNCTION_BUTTON_BORDER_COLOUR);
    ST7796_DrawFilledRectangle(state_x,
                               row_y + MAIN_INFO_FONT.height - MAIN_INFO_HIGHLIGHT_BORDER_H,
                               state_w,
                               MAIN_INFO_HIGHLIGHT_BORDER_H,
                               MAIN_SPECIAL_FUNCTION_BUTTON_BORDER_COLOUR);
}

static void Display_DrawMainInfoRightRow(const Preset_t *preset,
                                         uint8_t right_item_index,
                                         uint16_t row_y)
{
    if (right_item_index < PRESET_RELAY_COUNT)
    {
        char prefix[16];
        DisplayPresetEditField_t edit_field = Display_PresetEditGetField();
        uint8_t highlight_state = (edit_field.type == DISPLAY_PRESET_EDIT_FIELD_RELAY && edit_field.itemIndex == right_item_index) ? 1U : 0U;

        snprintf(prefix, sizeof(prefix), "Relay_%u: ", right_item_index + 1U);

        ST7796_WriteString32(MAIN_INFO_RIGHT_X,
                             row_y,
                             prefix,
                             MAIN_INFO_FONT,
                             MAIN_INFO_TEXT_COLOUR,
                             MAIN_INFO_TEXT_BG_COLOUR);
        Display_DrawMainInfoRelayField(preset, right_item_index, row_y, highlight_state);
        return;
    }

    if (right_item_index == PRESET_RELAY_COUNT)
        Display_DrawMainInfoSpecialState(row_y);
}

static void Display_DrawMainInfoRightRowsOnly(const Preset_t *preset,
                                              uint8_t previous_right_first_item)
{
    uint16_t right_width = (uint16_t)(ST7796_WIDTH - MAIN_INFO_RIGHT_X);
    uint8_t right_first_item = Display_GetMainInfoRightFirstItem();

    for (uint8_t index = 0U; index < MAIN_INFO_ROW_COUNT; ++index)
    {
        uint8_t info_index = (uint8_t)(main_info_first_slot + index);
        uint8_t previous_right_item_index = (uint8_t)(previous_right_first_item + index);
        uint8_t right_item_index = (uint8_t)(right_first_item + index);
        uint16_t row_y = main_info_row_y[index];

        if (Display_GetMainInfoRightFirstItemRenderState(previous_right_item_index)
            == Display_GetMainInfoRightFirstItemRenderState(right_item_index))
        {
            continue;
        }

        ST7796_DrawFilledRectangle(MAIN_INFO_RIGHT_X,
                                   row_y,
                                   right_width,
                                   MAIN_INFO_FONT.height,
                                   DISPLAY_BG_COLOUR);

        if (info_index >= PRESET_DEVICE_SLOTS)
            Display_DrawMainInfoCcRow(preset, (uint8_t)(info_index - PRESET_DEVICE_SLOTS), row_y);

        Display_DrawMainInfoRightRow(preset, right_item_index, row_y);
    }
}

static void Display_DrawMainInfoLeftRow(const Preset_t *preset,
                                        uint8_t info_index,
                                        uint16_t row_y)
{
    if (info_index < PRESET_DEVICE_SLOTS)
        Display_DrawMainInfoProgramRow(preset, info_index, row_y);
    else
        Display_DrawMainInfoCcRow(preset, (uint8_t)(info_index - PRESET_DEVICE_SLOTS), row_y);
}

static void Display_DrawMainInfoRows(const Preset_t *preset)
{
    uint8_t right_first_item = Display_GetMainInfoRightFirstItem();

    for (uint8_t index = 0U; index < MAIN_INFO_ROW_COUNT; ++index)
    {
        uint8_t info_index = (uint8_t)(main_info_first_slot + index);
        uint16_t row_y = main_info_row_y[index];
        ST7796_DrawFilledRectangle(0U, row_y, ST7796_WIDTH, MAIN_INFO_FONT.height, DISPLAY_BG_COLOUR);

        Display_DrawMainInfoLeftRow(preset, info_index, row_y);

        Display_DrawMainInfoRightRow(preset, (uint8_t)(right_first_item + index), row_y);
    }

    Display_DrawMainInfoScrollIndicators();
}

static void Display_DrawSavingPopup(void)
{
    uint16_t popup_w = (uint16_t)(strlen(MAIN_SAVING_POPUP_TEXT) * MAIN_INFO_FONT.width);
    uint16_t popup_x = (uint16_t)((ST7796_WIDTH - popup_w) / 2U);
    uint16_t popup_y = main_info_row_y[MAIN_SAVING_POPUP_ROW_INDEX];

    ST7796_DrawFilledRectangle(popup_x,
                               popup_y,
                               popup_w,
                               MAIN_INFO_FONT.height,
                               MAIN_SAVING_POPUP_BG_COLOUR);
    ST7796_WriteString32(popup_x,
                         popup_y,
                         MAIN_SAVING_POPUP_TEXT,
                         MAIN_INFO_FONT,
                         MAIN_SAVING_POPUP_TEXT_COLOUR,
                         MAIN_SAVING_POPUP_BG_COLOUR);
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
        Display_MenuRefresh();
        return;
    }

    if (!preset)
        return;

    Display_DrawMainInfoRows(preset);
}

static void Display_DrawMainInfoLeftRowsOnly(const Preset_t *preset,
                                             uint8_t previous_first_slot)
{
    for (uint8_t index = 0U; index < MAIN_INFO_ROW_COUNT; ++index)
    {
        uint8_t previous_info_index = (uint8_t)(previous_first_slot + index);
        uint8_t info_index = (uint8_t)(main_info_first_slot + index);
        uint16_t row_y = main_info_row_y[index];

        if (previous_info_index < PRESET_DEVICE_SLOTS && info_index < PRESET_DEVICE_SLOTS)
        {
            Display_DrawMainInfoProgramRow(preset, info_index, row_y);
            continue;
        }

        if (previous_info_index >= PRESET_DEVICE_SLOTS && info_index >= PRESET_DEVICE_SLOTS)
        {
            Display_DrawMainInfoCcLabel((uint8_t)(info_index - PRESET_DEVICE_SLOTS), row_y);
            Display_DrawMainInfoCcFields(preset,
                                         (uint8_t)(info_index - PRESET_DEVICE_SLOTS),
                                         row_y);
            continue;
        }

        ST7796_DrawFilledRectangle(0U,
                                   row_y,
                                   MAIN_INFO_RIGHT_X,
                                   MAIN_INFO_FONT.height,
                                   DISPLAY_BG_COLOUR);

        Display_DrawMainInfoLeftRow(preset, info_index, row_y);
    }

    Display_DrawMainInfoScrollIndicators();
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

static void Display_ClearStandardMenuRow(uint8_t row_index)
{
    if (row_index >= MENU_VISIBLE_ROW_COUNT)
        return;

    ST7796_DrawFilledRectangle(0U,
                               menu_row_y[row_index],
                               ST7796_WIDTH,
                               MAIN_INFO_FONT.height,
                               DISPLAY_BG_COLOUR);
}

static void Display_DrawMenuRootItem(uint8_t item_index)
{
    static const char * const menu_root_items[MENU_ROOT_ITEM_COUNT] = {
        "Banks",
        "Devices",
        "Global",
    };

    if (item_index >= MENU_ROOT_ITEM_COUNT)
    {
        Display_ClearStandardMenuRow(item_index);
        return;
    }

    Display_DrawMenuRow(menu_row_y[item_index],
                        menu_root_items[item_index],
                        "",
                        (item_index == menu_root_selection_index) ? 1U : 0U);
}

static void Display_DrawMenuRoot(void)
{
    for (uint8_t index = 0U; index < MENU_VISIBLE_ROW_COUNT; ++index)
        Display_DrawMenuRootItem(index);
}

static void Display_DrawMenuBankItem(uint8_t bank_index)
{
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(PRESET_BANK_COUNT, menu_bank_selection_index);
    uint8_t row_index;
    char label_text[12];

    if (bank_index < first_visible_index || bank_index >= (uint8_t)(first_visible_index + MENU_VISIBLE_ROW_COUNT))
        return;

    row_index = (uint8_t)(bank_index - first_visible_index);
    (void)snprintf(label_text, sizeof(label_text), "Bank %u", (uint8_t)(bank_index + 1U));
    Display_DrawMenuRow(menu_row_y[row_index],
                        label_text,
                        RuntimeConfig_GetBank(bank_index)->name,
                        (bank_index == menu_bank_selection_index) ? 1U : 0U);
}

static void Display_DrawMenuBanks(void)
{
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(PRESET_BANK_COUNT, menu_bank_selection_index);

    for (uint8_t row_index = 0U; row_index < MENU_VISIBLE_ROW_COUNT; ++row_index)
    {
        uint8_t bank_index = (uint8_t)(first_visible_index + row_index);
        char label_text[12];

        if (bank_index >= PRESET_BANK_COUNT)
        {
            Display_ClearStandardMenuRow(row_index);
            continue;
        }

        (void)label_text;
        Display_DrawMenuBankItem(bank_index);
    }
}

static void Display_FormatBankEditValue(uint8_t item_index, char *buffer, size_t buffer_size)
{
    const RuntimeConfigBank_t *bank = RuntimeConfig_GetBank(menu_active_bank_index);

    if (!buffer || buffer_size == 0U || !bank)
        return;

    switch (item_index)
    {
    case 0U:
        (void)snprintf(buffer, buffer_size, "%s", bank->name);
        break;
    case 1U:
        (void)snprintf(buffer, buffer_size, "%s", bank->wet_dry_enabled ? "Yes" : "No");
        break;
    case 2U:
        (void)snprintf(buffer, buffer_size, "Edit");
        break;
    case 3U:
        (void)snprintf(buffer, buffer_size, "%u bars", bank->midi_clock_bar_count);
        break;
    default:
        buffer[0] = '\0';
        break;
    }
}

static void Display_DrawMenuBankEdit(void)
{
    for (uint8_t index = 0U; index < MENU_VISIBLE_ROW_COUNT; ++index)
    {
        if (index < MENU_BANK_EDIT_ITEM_COUNT)
        {
            static const char * const menu_bank_edit_labels[MENU_BANK_EDIT_ITEM_COUNT] = {
                "Bank Name",
                "Wet / Dry",
                "Function Button",
                "Counter",
            };
            char value_text[24];

            Display_FormatBankEditValue(index, value_text, sizeof(value_text));
            Display_DrawMenuRow(menu_row_y[index],
                                menu_bank_edit_labels[index],
                                value_text,
                                (index == menu_bank_edit_selection_index) ? 1U : 0U);
        }
        else
            Display_ClearStandardMenuRow(index);
    }
}

static void Display_DrawMenuBankEditItem(uint8_t item_index)
{
    static const char * const menu_bank_edit_labels[MENU_BANK_EDIT_ITEM_COUNT] = {
        "Bank Name",
        "Wet / Dry",
        "Function Button",
        "Counter",
    };
    const RuntimeConfigBank_t *bank = RuntimeConfig_GetBank(menu_active_bank_index);
    char value_text[24];

    if (item_index >= MENU_BANK_EDIT_ITEM_COUNT)
    {
        Display_ClearStandardMenuRow(item_index);
        return;
    }

    if (item_index == 0U && menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_BANK_NAME)
    {
        Display_DrawMenuRow(menu_row_y[item_index],
                            menu_bank_edit_labels[item_index],
                            "",
                            0U);
        Display_DrawMenuTextEditValue(menu_row_y[item_index],
                                      bank ? bank->name : "",
                                      RUNTIME_CONFIG_BANK_NAME_LENGTH);
        return;
    }

    Display_FormatBankEditValue(item_index, value_text, sizeof(value_text));
    Display_DrawMenuRow(menu_row_y[item_index],
                        menu_bank_edit_labels[item_index],
                        value_text,
                        (item_index == menu_bank_edit_selection_index) ? 1U : 0U);
}

static void Display_FormatFunctionButtonValue(uint8_t item_index, char *buffer, size_t buffer_size)
{
    const RuntimeConfigFunctionButton_t *function_button = RuntimeConfig_GetFunctionButton(menu_active_bank_index);

    if (!buffer || buffer_size == 0U || !function_button)
        return;

    switch (item_index)
    {
    case 0U:
        (void)snprintf(buffer, buffer_size, "%s", function_button->name);
        break;
    case 1U:
        (void)snprintf(buffer, buffer_size, "%s", function_button->active_label);
        break;
    case 2U:
        (void)snprintf(buffer, buffer_size, "%s", function_button->inactive_label);
        break;
    default:
        buffer[0] = '\0';
        break;
    }
}

static uint8_t Display_GetFunctionButtonMessageSelectionIndex(void)
{
    if (menu_function_button_selection_index < MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX)
        return 0U;

    return (uint8_t)(menu_function_button_selection_index - MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX);
}

static void Display_DrawMenuFunctionButtonTextItemAtRow(uint8_t item_index, uint8_t row_index)
{
    static const char * const menu_function_button_labels[MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT] = {
        "Name",
        "Active Label",
        "Inactive Label",
    };
    const RuntimeConfigFunctionButton_t *function_button = RuntimeConfig_GetFunctionButton(menu_active_bank_index);
    char value_text[20];

    if (item_index >= MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT || row_index >= MENU_VISIBLE_ROW_COUNT)
    {
        if (row_index < MENU_VISIBLE_ROW_COUNT)
            Display_ClearStandardMenuRow(row_index);
        return;
    }

    if ((item_index == 0U && menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_NAME)
     || (item_index == 1U && menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_ACTIVE_LABEL)
     || (item_index == 2U && menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_INACTIVE_LABEL))
    {
        const char *text_value = "";
        uint8_t cell_count = 0U;

        switch (item_index)
        {
        case 0U:
            text_value = function_button ? function_button->name : "";
            cell_count = RUNTIME_CONFIG_FUNCTION_BUTTON_NAME_LENGTH;
            break;
        case 1U:
            text_value = function_button ? function_button->active_label : "";
            cell_count = RUNTIME_CONFIG_FUNCTION_BUTTON_LABEL_LENGTH;
            break;
        case 2U:
            text_value = function_button ? function_button->inactive_label : "";
            cell_count = RUNTIME_CONFIG_FUNCTION_BUTTON_LABEL_LENGTH;
            break;
        default:
            break;
        }

        Display_DrawMenuRow(menu_row_y[row_index],
                            menu_function_button_labels[item_index],
                            "",
                            0U);
        Display_DrawMenuTextEditValue(menu_row_y[row_index], text_value, cell_count);
        return;
    }

    Display_FormatFunctionButtonValue(item_index, value_text, sizeof(value_text));
    Display_DrawMenuRow(menu_row_y[row_index],
                        menu_function_button_labels[item_index],
                        value_text,
                        (item_index == menu_function_button_selection_index) ? 1U : 0U);
}

static void Display_DrawMenuFunctionButtonCompareHeaderRow(uint16_t row_y)
{
    Display_DrawMenuRow(row_y, "   Active        Inactive", "", 0U);
}

static void Display_DrawMenuFunctionButtonCcHeaderRow(uint16_t row_y)
{
    Display_DrawMenuRow(row_y, "  Ch Cc  Val    Ch Cc  Val", "", 0U);
}

static void Display_FormatFunctionButtonProgramCompareRow(uint8_t program_index,
                                                          char *buffer,
                                                          size_t buffer_size)
{
    const RuntimeConfigFunctionButton_t *function_button = RuntimeConfig_GetFunctionButton(menu_active_bank_index);
    char active_channel_text[4];
    char active_program_text[4];
    char inactive_channel_text[4];
    char inactive_program_text[4];

    if (!buffer || buffer_size == 0U || !function_button || program_index >= RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT)
        return;

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
    (void)snprintf(buffer,
                   buffer_size,
                   "CH:%s PRG:%s  CH:%s PRG:%s",
                   active_channel_text,
                   active_program_text,
                   inactive_channel_text,
                   inactive_program_text);
}

static void Display_FormatFunctionButtonCcCompareRow(uint8_t cc_index,
                                                     char *buffer,
                                                     size_t buffer_size)
{
    const RuntimeConfigFunctionButton_t *function_button = RuntimeConfig_GetFunctionButton(menu_active_bank_index);
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

static void Display_DrawMenuFunctionButtonProgramCompareEditRow(uint16_t row_y, uint8_t program_index)
{
    const RuntimeConfigFunctionButton_t *function_button = RuntimeConfig_GetFunctionButton(menu_active_bank_index);
    char active_channel_text[4];
    char active_program_text[4];
    char inactive_channel_text[4];
    char inactive_program_text[4];
    uint16_t draw_x = MENU_ITEM_X;

    if (!function_button || program_index >= RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT)
        return;

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

    Display_DrawMenuRow(row_y, "", "", 0U);
    draw_x = Display_WriteMenuValueSegment32(draw_x, row_y, "CH:", 0U);
    draw_x = Display_WriteMenuValueSegment32(draw_x,
                                             row_y,
                                             active_channel_text,
                                             (menu_function_button_message_field_index == 0U) ? 1U : 0U);
    draw_x = Display_WriteMenuValueSegment32(draw_x, row_y, " PRG:", 0U);
    draw_x = Display_WriteMenuValueSegment32(draw_x,
                                             row_y,
                                             active_program_text,
                                             (menu_function_button_message_field_index == 1U) ? 1U : 0U);
    draw_x = Display_WriteMenuValueSegment32(draw_x, row_y, "  CH:", 0U);
    draw_x = Display_WriteMenuValueSegment32(draw_x,
                                             row_y,
                                             inactive_channel_text,
                                             (menu_function_button_message_field_index == 2U) ? 1U : 0U);
    draw_x = Display_WriteMenuValueSegment32(draw_x, row_y, " PRG:", 0U);
    (void)Display_WriteMenuValueSegment32(draw_x,
                                          row_y,
                                          inactive_program_text,
                                          (menu_function_button_message_field_index == 3U) ? 1U : 0U);
}

static void Display_DrawMenuFunctionButtonCcCompareEditRow(uint16_t row_y, uint8_t cc_index)
{
    const RuntimeConfigFunctionButton_t *function_button = RuntimeConfig_GetFunctionButton(menu_active_bank_index);
    char row_number_text[3];
    char active_channel_text[4];
    char active_cc_text[4];
    char active_value_text[4];
    char inactive_channel_text[4];
    char inactive_cc_text[4];
    char inactive_value_text[4];
    uint16_t draw_x = MENU_ITEM_X;

    if (!function_button || cc_index >= RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT)
        return;

    (void)snprintf(row_number_text, sizeof(row_number_text), "%u", (uint8_t)(cc_index + 1U));
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

    Display_DrawMenuRow(row_y, "", "", 0U);
    draw_x = Display_WriteMenuValueSegment32(draw_x, row_y, row_number_text, 0U);
    draw_x = Display_WriteMenuValueSegment32(draw_x, row_y, " ", 0U);
    draw_x = Display_WriteMenuValueSegment32(draw_x,
                                             row_y,
                                             active_channel_text,
                                             (menu_function_button_message_field_index == 0U) ? 1U : 0U);
    draw_x = Display_WriteMenuValueSegment32(draw_x, row_y, " ", 0U);
    draw_x = Display_WriteMenuValueSegment32(draw_x,
                                             row_y,
                                             active_cc_text,
                                             (menu_function_button_message_field_index == 1U) ? 1U : 0U);
    draw_x = Display_WriteMenuValueSegment32(draw_x, row_y, " ", 0U);
    draw_x = Display_WriteMenuValueSegment32(draw_x,
                                             row_y,
                                             active_value_text,
                                             (menu_function_button_message_field_index == 2U) ? 1U : 0U);
    draw_x = Display_WriteMenuValueSegment32(draw_x, row_y, "    ", 0U);
    draw_x = Display_WriteMenuValueSegment32(draw_x,
                                             row_y,
                                             inactive_channel_text,
                                             (menu_function_button_message_field_index == 3U) ? 1U : 0U);
    draw_x = Display_WriteMenuValueSegment32(draw_x, row_y, " ", 0U);
    draw_x = Display_WriteMenuValueSegment32(draw_x,
                                             row_y,
                                             inactive_cc_text,
                                             (menu_function_button_message_field_index == 4U) ? 1U : 0U);
    draw_x = Display_WriteMenuValueSegment32(draw_x, row_y, " ", 0U);
    (void)Display_WriteMenuValueSegment32(draw_x,
                                          row_y,
                                          inactive_value_text,
                                          (menu_function_button_message_field_index == 5U) ? 1U : 0U);
}

static void Display_DrawMenuFunctionButtonProgramCompareRowAtRow(uint8_t row_index,
                                                                 uint8_t program_index,
                                                                 uint8_t selected)
{
    char value_text[32];

    if (program_index >= RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT)
    {
        Display_ClearStandardMenuRow(row_index);
        return;
    }

    if (selected)
    {
        Display_DrawMenuFunctionButtonProgramCompareEditRow(menu_row_y[row_index], program_index);
        return;
    }

    Display_FormatFunctionButtonProgramCompareRow(program_index, value_text, sizeof(value_text));
    Display_DrawMenuRow(menu_row_y[row_index], value_text, "", selected);
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
        Display_DrawMenuFunctionButtonCcCompareEditRow(menu_row_y[row_index], cc_index);
        return;
    }

    Display_FormatFunctionButtonCcCompareRow(cc_index, value_text, sizeof(value_text));
    Display_DrawMenuRow(menu_row_y[row_index], value_text, "", selected);
}

static void Display_DrawMenuFunctionButton(void)
{
    uint8_t message_selection_index;

    if (menu_function_button_selection_index < MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX)
    {
        for (uint8_t row_index = 0U; row_index < MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT; ++row_index)
            Display_DrawMenuFunctionButtonTextItemAtRow(row_index, row_index);

        Display_DrawMenuFunctionButtonCompareHeaderRow(menu_row_y[MENU_VISIBLE_ROW_COUNT - 1U]);
        return;
    }

    message_selection_index = Display_GetFunctionButtonMessageSelectionIndex();

    if (message_selection_index < RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT)
    {
        if (message_selection_index == 0U)
        {
            Display_DrawMenuFunctionButtonTextItemAtRow(1U, 0U);
            Display_DrawMenuFunctionButtonTextItemAtRow(2U, 1U);
            Display_DrawMenuFunctionButtonCompareHeaderRow(menu_row_y[2U]);
            Display_DrawMenuFunctionButtonProgramCompareRowAtRow(3U, 0U, 1U);
            return;
        }

        if (message_selection_index == 1U)
        {
            Display_DrawMenuFunctionButtonTextItemAtRow(2U, 0U);
            Display_DrawMenuFunctionButtonCompareHeaderRow(menu_row_y[1U]);
            Display_DrawMenuFunctionButtonProgramCompareRowAtRow(2U, 0U, 0U);
            Display_DrawMenuFunctionButtonProgramCompareRowAtRow(3U, 1U, 1U);
            return;
        }

        Display_DrawMenuFunctionButtonCompareHeaderRow(menu_row_y[0]);
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

        return;
    }

    Display_DrawMenuFunctionButtonCompareHeaderRow(menu_row_y[0]);
    Display_DrawMenuFunctionButtonCcHeaderRow(menu_row_y[1]);
    {
        uint8_t cc_selection_index = (uint8_t)(message_selection_index - RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT);

        if (cc_selection_index == 0U)
        {
            Display_DrawMenuFunctionButtonProgramCompareRowAtRow(1U,
                                                                 (uint8_t)(RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT - 1U),
                                                                 0U);
            Display_DrawMenuFunctionButtonCcHeaderRow(menu_row_y[2U]);
            Display_DrawMenuFunctionButtonCcCompareRowAtRow(3U, 0U, 1U);
            return;
        }

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

static void Display_DrawMenuFunctionButtonItem(uint8_t item_index)
{
    (void)item_index;
    Display_DrawMenuFunctionButton();
}

static uint8_t Display_AdjustFunctionButtonMessageValue(int8_t delta)
{
    RuntimeConfigFunctionButton_t *function_button = RuntimeConfig_GetMutableFunctionButton(menu_active_bank_index);
    uint8_t message_selection_index;

    if (!function_button || delta == 0 || !Display_MenuFunctionButtonMessagePageIsActive())
        return 0U;

    message_selection_index = Display_GetFunctionButtonMessageSelectionIndex();

    if (message_selection_index < RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT)
    {
        RuntimeConfigProgramMessage_t *active_program_message = &function_button->active_programs[message_selection_index];
        RuntimeConfigProgramMessage_t *inactive_program_message = &function_button->inactive_programs[message_selection_index];

        switch (menu_function_button_message_field_index)
        {
        case 0U:
            return Display_AdjustWrappedOptionalU8(&active_program_message->channel,
                                                   PRESET_CC_CHANNEL_UNUSED,
                                                   1U,
                                                   16U,
                                                   delta);
        case 1U:
            return Display_AdjustWrappedOptionalU8(&active_program_message->program,
                                                   PRESET_PROGRAM_NONE,
                                                   0U,
                                                   127U,
                                                   delta);
        case 2U:
            return Display_AdjustWrappedOptionalU8(&inactive_program_message->channel,
                                                   PRESET_CC_CHANNEL_UNUSED,
                                                   1U,
                                                   16U,
                                                   delta);
        case 3U:
            return Display_AdjustWrappedOptionalU8(&inactive_program_message->program,
                                                   PRESET_PROGRAM_NONE,
                                                   0U,
                                                   127U,
                                                   delta);
        default:
            return 0U;
        }
    }

    {
        uint8_t cc_index = (uint8_t)(message_selection_index - RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT);
        PresetCCSlot_t *active_cc_message = &function_button->active_cc[cc_index];
        PresetCCSlot_t *inactive_cc_message = &function_button->inactive_cc[cc_index];

        switch (menu_function_button_message_field_index)
        {
        case 0U:
            return Display_AdjustWrappedOptionalU8(&active_cc_message->channel,
                                                   PRESET_CC_CHANNEL_UNUSED,
                                                   1U,
                                                   16U,
                                                   delta);
        case 1U:
            return Display_AdjustWrappedOptionalU8(&active_cc_message->cc_number,
                                                   PRESET_CC_NUMBER_UNUSED,
                                                   0U,
                                                   127U,
                                                   delta);
        case 2U:
            return Display_AdjustWrappedOptionalU8(&active_cc_message->value,
                                                   PRESET_CC_VALUE_UNUSED,
                                                   0U,
                                                   127U,
                                                   delta);
        case 3U:
            return Display_AdjustWrappedOptionalU8(&inactive_cc_message->channel,
                                                   PRESET_CC_CHANNEL_UNUSED,
                                                   1U,
                                                   16U,
                                                   delta);
        case 4U:
            return Display_AdjustWrappedOptionalU8(&inactive_cc_message->cc_number,
                                                   PRESET_CC_NUMBER_UNUSED,
                                                   0U,
                                                   127U,
                                                   delta);
        case 5U:
            return Display_AdjustWrappedOptionalU8(&inactive_cc_message->value,
                                                   PRESET_CC_VALUE_UNUSED,
                                                   0U,
                                                   127U,
                                                   delta);
        default:
            return 0U;
        }
    }
}

static void Display_DrawMenuDevices(void)
{
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MIDI_DEVICE_COUNT, menu_device_selection_index);

    for (uint8_t row_index = 0U; row_index < MENU_VISIBLE_ROW_COUNT; ++row_index)
    {
        uint8_t device_index = (uint8_t)(first_visible_index + row_index);
        char label_text[14];
        char value_text[12];
        const RuntimeConfigDevice_t *device;

        if (device_index >= MIDI_DEVICE_COUNT)
        {
            Display_ClearStandardMenuRow(row_index);
            continue;
        }

        (void)device;
        (void)label_text;
        (void)value_text;
        Display_DrawMenuDeviceItem(device_index);
    }
}

static void Display_DrawMenuDeviceItem(uint8_t device_index)
{
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MIDI_DEVICE_COUNT, menu_device_selection_index);
    uint8_t row_index;
    char label_text[14];
    char value_text[12];
    const RuntimeConfigDevice_t *device;

    if (device_index < first_visible_index || device_index >= (uint8_t)(first_visible_index + MENU_VISIBLE_ROW_COUNT))
        return;

    row_index = (uint8_t)(device_index - first_visible_index);
    device = RuntimeConfig_GetDevice(device_index);
    (void)snprintf(label_text, sizeof(label_text), "Device %u", (uint8_t)(device_index + 1U));

    if (device->name[0] != '\0')
        (void)snprintf(value_text, sizeof(value_text), "%s", device->name);
    else
        (void)snprintf(value_text, sizeof(value_text), "CH %u", device->channel);

    Display_DrawMenuRow(menu_row_y[row_index],
                        label_text,
                        value_text,
                        (device_index == menu_device_selection_index) ? 1U : 0U);
}

static uint8_t Display_MenuDeviceCcRowIsSelected(void)
{
    return (menu_page == DISPLAY_MENU_PAGE_DEVICE_EDIT
         && menu_device_edit_selection_index >= 3U
         && menu_device_edit_selection_index <= 6U) ? 1U : 0U;
}

static MidiCC_t *Display_GetSelectedDeviceCc(RuntimeConfigDevice_t *device)
{
    if (!device)
        return NULL;

    switch (menu_device_edit_selection_index)
    {
    case 3U:
        return &device->active;
    case 4U:
        return &device->bypass;
    case 5U:
        return &device->level;
    case 6U:
        return &device->tap_tempo;
    default:
        return NULL;
    }
}

static void Display_ResetActiveDeviceToUnusedDefaults(RuntimeConfigDevice_t *device)
{
    if (!device)
        return;

    memset(device->name, 0, sizeof(device->name));
    device->channel = (uint8_t)(menu_active_device_index + 1U);
    device->active.cc = PRESET_CC_NUMBER_UNUSED;
    device->active.value = 0U;
    device->bypass.cc = PRESET_CC_NUMBER_UNUSED;
    device->bypass.value = 0U;
    device->level.cc = PRESET_CC_NUMBER_UNUSED;
    device->level.value = 0U;
    device->tap_tempo.cc = PRESET_CC_NUMBER_UNUSED;
    device->tap_tempo.value = 0U;
    device->max_preset = 127U;
}

static void Display_FormatDeviceCcValue(const MidiCC_t *cc, char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0U)
        return;

    if (!cc || cc->cc == PRESET_CC_NUMBER_UNUSED)
    {
        (void)snprintf(buffer, buffer_size, "CC:--- VAL:---");
        return;
    }

    (void)snprintf(buffer, buffer_size, "CC:%3u VAL:%3u", cc->cc, cc->value);
}

static void Display_FormatDeviceCcEditValue(const MidiCC_t *cc, char *buffer, size_t buffer_size)
{
    char cc_number_text[8];
    char value_text[8];

    if (!buffer || buffer_size == 0U || !cc)
        return;

    Display_FormatMenuOptionalField(cc_number_text,
                                    sizeof(cc_number_text),
                                    cc->cc,
                                    PRESET_CC_NUMBER_UNUSED,
                                    3U,
                                    (menu_device_cc_field_index == 0U) ? 1U : 0U);
    Display_FormatMenuNumericField(value_text,
                                   sizeof(value_text),
                                   cc->value,
                                   3U,
                                   (menu_device_cc_field_index == 1U) ? 1U : 0U);
    (void)snprintf(buffer, buffer_size, "CC:%s VAL:%s", cc_number_text, value_text);
}

static void Display_FormatDeviceEditValue(uint8_t item_index, char *buffer, size_t buffer_size)
{
    const RuntimeConfigDevice_t *device = RuntimeConfig_GetDevice(menu_active_device_index);

    if (!buffer || buffer_size == 0U || !device)
        return;

    switch (item_index)
    {
    case 0U:
        if (device->name[0] != '\0')
            (void)snprintf(buffer, buffer_size, "%s", device->name);
        else
            (void)snprintf(buffer, buffer_size, "CH %u", device->channel);
        break;
    case 1U:
        (void)snprintf(buffer, buffer_size, "%u", device->max_preset);
        break;
    case 2U:
        (void)snprintf(buffer, buffer_size, "%u", device->channel);
        break;
    case 3U:
        if (menu_device_cc_field_edit_active && menu_device_edit_selection_index == 3U)
            Display_FormatDeviceCcEditValue(&device->active, buffer, buffer_size);
        else
            Display_FormatDeviceCcValue(&device->active, buffer, buffer_size);
        break;
    case 4U:
        if (menu_device_cc_field_edit_active && menu_device_edit_selection_index == 4U)
            Display_FormatDeviceCcEditValue(&device->bypass, buffer, buffer_size);
        else
            Display_FormatDeviceCcValue(&device->bypass, buffer, buffer_size);
        break;
    case 5U:
        if (menu_device_cc_field_edit_active && menu_device_edit_selection_index == 5U)
            Display_FormatDeviceCcEditValue(&device->level, buffer, buffer_size);
        else
            Display_FormatDeviceCcValue(&device->level, buffer, buffer_size);
        break;
    case 6U:
        if (menu_device_cc_field_edit_active && menu_device_edit_selection_index == 6U)
            Display_FormatDeviceCcEditValue(&device->tap_tempo, buffer, buffer_size);
        else
            Display_FormatDeviceCcValue(&device->tap_tempo, buffer, buffer_size);
        break;
    case 7U:
        (void)snprintf(buffer, buffer_size, "Pending");
        break;
    default:
        buffer[0] = '\0';
        break;
    }
}

static void Display_DrawMenuDeviceEdit(void)
{
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MENU_DEVICE_EDIT_ITEM_COUNT,
                                                                   menu_device_edit_selection_index);

    for (uint8_t row_index = 0U; row_index < MENU_VISIBLE_ROW_COUNT; ++row_index)
    {
        uint8_t item_index = (uint8_t)(first_visible_index + row_index);
        char value_text[20];
        uint8_t row_selected;

        if (item_index >= MENU_DEVICE_EDIT_ITEM_COUNT)
        {
            Display_ClearStandardMenuRow(row_index);
            continue;
        }

        (void)value_text;
        (void)row_selected;
        Display_DrawMenuDeviceEditItem(item_index);
    }
}

static void Display_DrawMenuDeviceEditItem(uint8_t item_index)
{
    static const char * const menu_device_edit_labels[MENU_DEVICE_EDIT_ITEM_COUNT] = {
        "Name",
        "Max Preset",
        "Channel",
        "Active CC",
        "Bypass CC",
        "Level CC",
        "Tap-SW CC",
        "Init Device",
    };
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MENU_DEVICE_EDIT_ITEM_COUNT,
                                                                   menu_device_edit_selection_index);
    uint8_t row_index;
    uint8_t row_selected;
    char value_text[20];
    const RuntimeConfigDevice_t *device = RuntimeConfig_GetDevice(menu_active_device_index);

    if (item_index < first_visible_index || item_index >= (uint8_t)(first_visible_index + MENU_VISIBLE_ROW_COUNT))
        return;

    row_index = (uint8_t)(item_index - first_visible_index);
    row_selected = (item_index == menu_device_edit_selection_index) ? 1U : 0U;
    if (row_selected && item_index == 0U && menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_DEVICE_NAME)
        row_selected = 0U;
    if (row_selected && item_index >= 3U && item_index <= 6U)
        row_selected = 0U;

    if (item_index == 0U && menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_DEVICE_NAME)
    {
        Display_DrawMenuRow(menu_row_y[row_index],
                            menu_device_edit_labels[item_index],
                            "",
                            0U);
        Display_DrawMenuTextEditValue(menu_row_y[row_index],
                                      device ? device->name : "",
                                      RUNTIME_CONFIG_DEVICE_NAME_LENGTH);
        return;
    }

    if (item_index >= 3U && item_index <= 6U && item_index == menu_device_edit_selection_index)
    {
        Display_DrawMenuRow(menu_row_y[row_index],
                            menu_device_edit_labels[item_index],
                            "",
                            0U);
        Display_DrawMenuDeviceCcEditFields(menu_row_y[row_index], Display_GetSelectedDeviceCc((RuntimeConfigDevice_t *)device));
        return;
    }

    Display_FormatDeviceEditValue(item_index, value_text, sizeof(value_text));

    Display_DrawMenuRow(menu_row_y[row_index],
                        menu_device_edit_labels[item_index],
                        value_text,
                        row_selected);
}

static void Display_DrawMenuDeviceInitConfirm(void)
{
    char confirm_text[20];
    uint16_t text_x;

    (void)snprintf(confirm_text,
                   sizeof(confirm_text),
                   "INIT DEVICE %u?",
                   (uint8_t)(menu_active_device_index + 1U));
    text_x = (uint16_t)((ST7796_WIDTH - ((uint16_t)strlen(confirm_text) * MAIN_BANK_FONT.width)) / 2U);

    ST7796_WriteString32(text_x,
                         MAIN_BANK_TEXT_Y,
                         confirm_text,
                         MAIN_BANK_FONT,
                         MAIN_BANK_COLOUR,
                         DISPLAY_BG_COLOUR);
}

static void Display_FormatGlobalMenuValue(uint8_t item_index, char *buffer, size_t buffer_size)
{
    const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();

    if (!buffer || buffer_size == 0U || !global)
        return;

    switch (item_index)
    {
    case 0U:
        (void)snprintf(buffer, buffer_size, "%u sec", global->startup_delay_seconds);
        break;
    case 1U:
        (void)snprintf(buffer, buffer_size, "%u min", global->screensaver_timeout_minutes);
        break;
    case 2U:
        (void)snprintf(buffer,
                       buffer_size,
                       "%s",
                       (global->sync_style == RUNTIME_CONFIG_SYNC_STYLE_TAP_TEMPO_CC) ? "Tap Tempo CC" : "MIDI clock");
        break;
    case 3U:
        (void)snprintf(buffer,
                       buffer_size,
                       "%u",
                       Display_GetGlobalBrightnessUiValue(global->backlight_brightness));
        break;
    default:
        buffer[0] = '\0';
        break;
    }
}

static void Display_DrawMenuGlobal(void)
{
    for (uint8_t index = 0U; index < MENU_VISIBLE_ROW_COUNT; ++index)
    {
        if (index < MENU_GLOBAL_ITEM_COUNT)
            Display_DrawMenuGlobalItem(index);
        else
            Display_ClearStandardMenuRow(index);
    }
}

static void Display_DrawMenuGlobalItem(uint8_t item_index)
{
    static const char * const menu_global_labels[MENU_GLOBAL_ITEM_COUNT] = {
        "Startup Delay",
        "Screen Saver",
        "Sync Style",
        "Brightness",
    };
    char value_text[20];

    if (item_index >= MENU_GLOBAL_ITEM_COUNT)
    {
        Display_ClearStandardMenuRow(item_index);
        return;
    }

    Display_FormatGlobalMenuValue(item_index, value_text, sizeof(value_text));
    Display_DrawMenuRow(menu_row_y[item_index],
                        menu_global_labels[item_index],
                        value_text,
                        (item_index == menu_global_selection_index) ? 1U : 0U);
}

static void Display_DrawCurrentMenuPageBody(void)
{
    switch (menu_page)
    {
    case DISPLAY_MENU_PAGE_BANKS:
        Display_DrawMenuBanks();
        break;
    case DISPLAY_MENU_PAGE_BANK_EDIT:
        Display_DrawMenuBankEdit();
        break;
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON:
        Display_DrawMenuFunctionButton();
        break;
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES:
        Display_DrawMenuFunctionButton();
        break;
    case DISPLAY_MENU_PAGE_DEVICES:
        Display_DrawMenuDevices();
        break;
    case DISPLAY_MENU_PAGE_DEVICE_EDIT:
        Display_DrawMenuDeviceEdit();
        break;
    case DISPLAY_MENU_PAGE_DEVICE_INIT_CONFIRM:
        Display_DrawMenuDeviceInitConfirm();
        break;
    case DISPLAY_MENU_PAGE_GLOBAL:
        Display_DrawMenuGlobal();
        break;
    case DISPLAY_MENU_PAGE_ROOT:
    default:
        Display_DrawMenuRoot();
        break;
    }
}

static uint8_t Display_GetMenuSelectionIndexForPage(DisplayMenuPage_t page)
{
    switch (page)
    {
    case DISPLAY_MENU_PAGE_ROOT:
        return menu_root_selection_index;
    case DISPLAY_MENU_PAGE_BANKS:
        return menu_bank_selection_index;
    case DISPLAY_MENU_PAGE_BANK_EDIT:
        return menu_bank_edit_selection_index;
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES:
        return menu_function_button_selection_index;
    case DISPLAY_MENU_PAGE_DEVICES:
        return menu_device_selection_index;
    case DISPLAY_MENU_PAGE_DEVICE_EDIT:
        return menu_device_edit_selection_index;
    case DISPLAY_MENU_PAGE_GLOBAL:
        return menu_global_selection_index;
    default:
        return 0U;
    }
}

static uint8_t Display_GetMenuFirstVisibleIndexForPage(DisplayMenuPage_t page, uint8_t selection_index)
{
    switch (page)
    {
    case DISPLAY_MENU_PAGE_BANKS:
        return Display_GetMenuFirstVisibleIndex(PRESET_BANK_COUNT, selection_index);
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON:
        return Display_GetMenuFirstVisibleIndex(MENU_FUNCTION_BUTTON_ITEM_COUNT, selection_index);
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES:
        return Display_GetMenuFirstVisibleIndex(MENU_FUNCTION_BUTTON_MESSAGE_ROW_COUNT, selection_index);
    case DISPLAY_MENU_PAGE_DEVICES:
        return Display_GetMenuFirstVisibleIndex(MIDI_DEVICE_COUNT, selection_index);
    case DISPLAY_MENU_PAGE_DEVICE_EDIT:
        return Display_GetMenuFirstVisibleIndex(MENU_DEVICE_EDIT_ITEM_COUNT, selection_index);
    default:
        return 0U;
    }
}

static void Display_DrawMenuPageItem(DisplayMenuPage_t page, uint8_t item_index)
{
    switch (page)
    {
    case DISPLAY_MENU_PAGE_ROOT:
        Display_DrawMenuRootItem(item_index);
        break;
    case DISPLAY_MENU_PAGE_BANKS:
        Display_DrawMenuBankItem(item_index);
        break;
    case DISPLAY_MENU_PAGE_BANK_EDIT:
        Display_DrawMenuBankEditItem(item_index);
        break;
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES:
        Display_DrawMenuFunctionButtonItem(item_index);
        break;
    case DISPLAY_MENU_PAGE_DEVICES:
        Display_DrawMenuDeviceItem(item_index);
        break;
    case DISPLAY_MENU_PAGE_DEVICE_EDIT:
        Display_DrawMenuDeviceEditItem(item_index);
        break;
    case DISPLAY_MENU_PAGE_GLOBAL:
        Display_DrawMenuGlobalItem(item_index);
        break;
    default:
        break;
    }
}

static void Display_MenuRefreshBodyOnly(void)
{
    Display_ClearMenuBody();
    Display_DrawCurrentMenuPageBody();
    menu_last_drawn_page = menu_page;
    menu_draw_state_valid = 1U;
}

static void Display_MenuRedrawCurrentPageRows(void)
{
    Display_DrawCurrentMenuPageBody();
    menu_last_drawn_page = menu_page;
    menu_draw_state_valid = 1U;
}

static void Display_MenuRedrawCurrentItem(void)
{
    if (menu_page == DISPLAY_MENU_PAGE_FUNCTION_BUTTON)
    {
        Display_MenuRedrawCurrentPageRows();
        return;
    }

    Display_DrawMenuPageItem(menu_page, Display_GetMenuSelectionIndexForPage(menu_page));
}

static void Display_MenuRedrawCurrentValue(void)
{
    char value_text[32];

    if (!menu_mode_active || !menu_draw_state_valid)
    {
        Display_MenuRedrawCurrentItem();
        return;
    }

    switch (menu_page)
    {
    case DISPLAY_MENU_PAGE_BANK_EDIT:
    {
        static const char * const menu_bank_edit_labels[MENU_BANK_EDIT_ITEM_COUNT] = {
            "Bank Name",
            "Wet / Dry",
            "Function Button",
            "Counter",
        };
        uint8_t item_index = menu_bank_edit_selection_index;
        const RuntimeConfigBank_t *bank = RuntimeConfig_GetBank(menu_active_bank_index);

        if (item_index >= MENU_BANK_EDIT_ITEM_COUNT)
            return;

        if (item_index == 0U && menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_BANK_NAME)
        {
            Display_DrawMenuRowValueOnly(menu_row_y[item_index],
                                         menu_bank_edit_labels[item_index],
                                         "",
                                         0U);
            Display_DrawMenuTextEditValue(menu_row_y[item_index],
                                          bank ? bank->name : "",
                                          RUNTIME_CONFIG_BANK_NAME_LENGTH);
            return;
        }

        Display_FormatBankEditValue(item_index, value_text, sizeof(value_text));
        Display_DrawMenuRowValueOnly(menu_row_y[item_index],
                                     menu_bank_edit_labels[item_index],
                                     value_text,
                                     1U);
        return;
    }

    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON:
        Display_MenuRedrawCurrentPageRows();
        return;

    case DISPLAY_MENU_PAGE_DEVICE_EDIT:
    {
        static const char * const menu_device_edit_labels[MENU_DEVICE_EDIT_ITEM_COUNT] = {
            "Name",
            "Max Preset",
            "Channel",
            "Active CC",
            "Bypass CC",
            "Level CC",
            "Tap-SW CC",
            "Init Device",
        };
        const RuntimeConfigDevice_t *device = RuntimeConfig_GetDevice(menu_active_device_index);
        uint8_t item_index = menu_device_edit_selection_index;
        uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MENU_DEVICE_EDIT_ITEM_COUNT,
                                                                       item_index);
        uint8_t row_index;
        uint8_t row_selected = 1U;

        if (item_index < first_visible_index || item_index >= (uint8_t)(first_visible_index + MENU_VISIBLE_ROW_COUNT))
            return;

        if (item_index == 0U && menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_DEVICE_NAME)
        {
            row_index = (uint8_t)(item_index - first_visible_index);
            Display_DrawMenuRowValueOnly(menu_row_y[row_index],
                                         menu_device_edit_labels[item_index],
                                         "",
                                         0U);
            Display_DrawMenuTextEditValue(menu_row_y[row_index],
                                          device ? device->name : "",
                                          RUNTIME_CONFIG_DEVICE_NAME_LENGTH);
            return;
        }

        if (item_index >= 3U && item_index <= 6U)
            row_selected = 0U;

        row_index = (uint8_t)(item_index - first_visible_index);

        if (item_index >= 3U && item_index <= 6U)
        {
            Display_DrawMenuRowValueOnly(menu_row_y[row_index],
                                         menu_device_edit_labels[item_index],
                                         "",
                                         0U);
            Display_DrawMenuDeviceCcEditFields(menu_row_y[row_index], Display_GetSelectedDeviceCc((RuntimeConfigDevice_t *)device));
            return;
        }

        Display_FormatDeviceEditValue(item_index, value_text, sizeof(value_text));
        Display_DrawMenuRowValueOnly(menu_row_y[row_index],
                                     menu_device_edit_labels[item_index],
                                     value_text,
                                     row_selected);
        return;
    }

    case DISPLAY_MENU_PAGE_GLOBAL:
    {
        static const char * const menu_global_labels[MENU_GLOBAL_ITEM_COUNT] = {
            "Startup Delay",
            "Screen Saver",
            "Sync Style",
            "Brightness",
        };
        uint8_t item_index = menu_global_selection_index;

        if (item_index >= MENU_GLOBAL_ITEM_COUNT)
            return;

        Display_FormatGlobalMenuValue(item_index, value_text, sizeof(value_text));
        Display_DrawMenuRowValueOnly(menu_row_y[item_index],
                                     menu_global_labels[item_index],
                                     value_text,
                                     1U);
        return;
    }

    default:
        Display_MenuRedrawCurrentItem();
        return;
    }
}

static void Display_MenuRedrawSelectionChange(DisplayMenuPage_t page, uint8_t previous_selection)
{
    uint8_t current_selection = Display_GetMenuSelectionIndexForPage(page);
    uint8_t previous_first_visible = Display_GetMenuFirstVisibleIndexForPage(page, previous_selection);
    uint8_t current_first_visible = Display_GetMenuFirstVisibleIndexForPage(page, current_selection);

    if (page == DISPLAY_MENU_PAGE_FUNCTION_BUTTON)
    {
        Display_MenuRedrawCurrentPageRows();
        return;
    }

    if (previous_first_visible != current_first_visible)
    {
        Display_MenuRedrawCurrentPageRows();
        return;
    }

    Display_DrawMenuPageItem(page, previous_selection);
    if (current_selection != previous_selection)
        Display_DrawMenuPageItem(page, current_selection);
}

void Display_MenuRefresh(void)
{
    if (!menu_mode_active)
        return;

    main_layout_dirty = 0U;

    if (!menu_draw_state_valid)
    {
        ST7796_DrawFilledRectangle(0U, 0U, ST7796_WIDTH, MENU_BODY_Y, DISPLAY_BG_COLOUR);
        Display_DrawFootbar();
        Display_DrawMainModeHeader();
    }
    else
    {
        if (Display_MenuPageUsesConfirmFootbar(menu_last_drawn_page) != Display_MenuPageUsesConfirmFootbar(menu_page))
            Display_DrawFootbar();

        if (Display_MenuHeaderChanged(menu_last_drawn_page, menu_page))
            Display_DrawMainModeHeader();
    }

    Display_MenuRefreshBodyOnly();
}

void Display_MenuEnter(void)
{
    menu_mode_active = 1U;
    menu_draw_state_valid = 0U;
    menu_last_drawn_page = DISPLAY_MENU_PAGE_ROOT;
    menu_page = DISPLAY_MENU_PAGE_ROOT;
    menu_text_edit_field = DISPLAY_MENU_TEXT_FIELD_NONE;
    menu_text_edit_cursor_index = 0U;
    Display_ResetFunctionButtonMessageEditor();
    menu_root_selection_index = 0U;
    menu_bank_selection_index = current_bank;
    menu_active_bank_index = current_bank;
    menu_bank_edit_selection_index = 0U;
    menu_function_button_selection_index = 0U;
    menu_device_selection_index = 0U;
    menu_active_device_index = 0U;
    menu_device_edit_selection_index = 0U;
    menu_device_cc_field_index = 0U;
    menu_device_cc_field_edit_active = 0U;
    menu_global_selection_index = 0U;
    main_layout_dirty = 1U;
    bpm_display_valid = 0U;
    Display_MenuRefresh();
}

void Display_MenuExit(void)
{
    Display_ResetFunctionButtonMessageEditor();
    menu_text_edit_field = DISPLAY_MENU_TEXT_FIELD_NONE;
    menu_text_edit_cursor_index = 0U;
    menu_device_cc_field_index = 0U;
    menu_device_cc_field_edit_active = 0U;
    menu_draw_state_valid = 0U;
    menu_last_drawn_page = DISPLAY_MENU_PAGE_ROOT;
    menu_mode_active = 0U;
    menu_page = DISPLAY_MENU_PAGE_ROOT;
    main_layout_dirty = 1U;
    bpm_display_valid = 0U;
}

uint8_t Display_MenuIsActive(void)
{
    return menu_mode_active;
}

uint8_t Display_MenuSubEditorIsActive(void)
{
    return (menu_text_edit_field != DISPLAY_MENU_TEXT_FIELD_NONE) ? 1U : 0U;
}

void Display_MenuTextEditExit(void)
{
    if (menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_NONE)
        return;

    menu_text_edit_field = DISPLAY_MENU_TEXT_FIELD_NONE;
    menu_text_edit_cursor_index = 0U;

    if (menu_mode_active)
        Display_MenuRedrawCurrentItem();
}

uint8_t Display_MenuTextEditIsActive(void)
{
    return (menu_text_edit_field != DISPLAY_MENU_TEXT_FIELD_NONE) ? 1U : 0U;
}

uint8_t Display_MenuTextEditMoveCursor(int8_t delta)
{
    int16_t next_index;
    uint8_t max_index;

    if (!menu_mode_active || delta == 0 || menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_NONE)
        return 0U;

    max_index = (uint8_t)(Display_GetMenuTextFieldLength(menu_text_edit_field) - 1U);
    next_index = (int16_t)menu_text_edit_cursor_index + (int16_t)delta;
    if (next_index < 0)
        next_index = 0;
    else if (next_index > (int16_t)max_index)
        next_index = (int16_t)max_index;

    if ((uint8_t)next_index == menu_text_edit_cursor_index)
        return 0U;

    menu_text_edit_cursor_index = (uint8_t)next_index;
    Display_MenuRedrawCurrentValue();
    return 1U;
}

void Display_MenuHome(void)
{
    RuntimeConfigDevice_t *device;

    if (!menu_mode_active)
        return;

    if (menu_page == DISPLAY_MENU_PAGE_DEVICE_INIT_CONFIRM)
    {
        device = RuntimeConfig_GetMutableDevice(menu_active_device_index);
        Display_ResetFunctionButtonMessageEditor();
        menu_device_cc_field_index = 0U;
        menu_device_cc_field_edit_active = 0U;
        menu_text_edit_field = DISPLAY_MENU_TEXT_FIELD_NONE;
        menu_text_edit_cursor_index = 0U;

        if (device)
        {
            Display_ResetActiveDeviceToUnusedDefaults(device);
            RuntimeConfig_MarkDirty();
        }

        menu_page = DISPLAY_MENU_PAGE_DEVICE_EDIT;
        Display_MenuRefresh();
        return;
    }

    menu_text_edit_field = DISPLAY_MENU_TEXT_FIELD_NONE;
    menu_text_edit_cursor_index = 0U;
    Display_ResetFunctionButtonMessageEditor();
    menu_device_cc_field_index = 0U;
    menu_device_cc_field_edit_active = 0U;

    switch (menu_page)
    {
    case DISPLAY_MENU_PAGE_GLOBAL:
        menu_root_selection_index = 2U;
        break;
    case DISPLAY_MENU_PAGE_BANK_EDIT:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES:
    case DISPLAY_MENU_PAGE_BANKS:
        menu_root_selection_index = 0U;
        break;
    case DISPLAY_MENU_PAGE_DEVICE_EDIT:
    case DISPLAY_MENU_PAGE_DEVICES:
        menu_root_selection_index = 1U;
        break;
    default:
        break;
    }

    menu_page = DISPLAY_MENU_PAGE_ROOT;
    Display_MenuRefresh();
}

uint8_t Display_MenuMoveSelection(int8_t delta)
{
    uint8_t *selection = NULL;
    uint8_t item_count = 0U;
    uint8_t previous_selection;
    int16_t next_selection;

    if (!menu_mode_active || delta == 0 || menu_text_edit_field != DISPLAY_MENU_TEXT_FIELD_NONE)
        return 0U;

    if (menu_page == DISPLAY_MENU_PAGE_FUNCTION_BUTTON)
    {
        uint16_t current_focus = Display_GetFunctionButtonFocusIndex();
        uint16_t focus_count = Display_GetFunctionButtonFocusCount();
        int32_t next_focus = (int32_t)current_focus + (int32_t)delta;

        if (next_focus < 0)
            next_focus = 0;
        else if (next_focus >= (int32_t)focus_count)
            next_focus = (int32_t)focus_count - 1;

        if ((uint16_t)next_focus == current_focus)
            return 0U;

        previous_selection = menu_function_button_selection_index;
        Display_SetFunctionButtonFocusIndex((uint16_t)next_focus);

        if (menu_function_button_selection_index == previous_selection)
            Display_MenuRedrawCurrentItem();
        else
            Display_MenuRedrawSelectionChange(menu_page, previous_selection);

        return 1U;
    }

    if (menu_page == DISPLAY_MENU_PAGE_DEVICE_EDIT)
    {
        uint16_t current_focus = Display_GetDeviceEditFocusIndex();
        uint16_t focus_count = Display_GetDeviceEditFocusCount();
        int32_t next_focus = (int32_t)current_focus + (int32_t)delta;

        if (next_focus < 0)
            next_focus = 0;
        else if (next_focus >= (int32_t)focus_count)
            next_focus = (int32_t)focus_count - 1;

        if ((uint16_t)next_focus == current_focus)
            return 0U;

        previous_selection = menu_device_edit_selection_index;
        Display_SetDeviceEditFocusIndex((uint16_t)next_focus);

        if (menu_device_edit_selection_index == previous_selection)
            Display_MenuRedrawCurrentItem();
        else
            Display_MenuRedrawSelectionChange(menu_page, previous_selection);

        return 1U;
    }

    switch (menu_page)
    {
    case DISPLAY_MENU_PAGE_ROOT:
        selection = &menu_root_selection_index;
        item_count = MENU_ROOT_ITEM_COUNT;
        break;
    case DISPLAY_MENU_PAGE_BANKS:
        selection = &menu_bank_selection_index;
        item_count = PRESET_BANK_COUNT;
        break;
    case DISPLAY_MENU_PAGE_BANK_EDIT:
        selection = &menu_bank_edit_selection_index;
        item_count = MENU_BANK_EDIT_ITEM_COUNT;
        break;
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON:
        selection = &menu_function_button_selection_index;
        item_count = MENU_FUNCTION_BUTTON_ITEM_COUNT;
        break;
    case DISPLAY_MENU_PAGE_DEVICES:
        selection = &menu_device_selection_index;
        item_count = MIDI_DEVICE_COUNT;
        break;
    case DISPLAY_MENU_PAGE_DEVICE_EDIT:
        selection = &menu_device_edit_selection_index;
        item_count = MENU_DEVICE_EDIT_ITEM_COUNT;
        break;
    case DISPLAY_MENU_PAGE_GLOBAL:
        selection = &menu_global_selection_index;
        item_count = MENU_GLOBAL_ITEM_COUNT;
        break;
    default:
        if (!selection)
            return 0U;
        break;
    }

    next_selection = (int16_t)(*selection) + (int16_t)delta;
    if (next_selection < 0)
        next_selection = 0;
    else if (next_selection >= (int16_t)item_count)
        next_selection = (int16_t)item_count - 1;

    if ((uint8_t)next_selection == *selection)
        return 0U;

    previous_selection = *selection;
    *selection = (uint8_t)next_selection;

    Display_MenuRedrawSelectionChange(menu_page, previous_selection);
    return 1U;
}

uint8_t Display_MenuActivate(void)
{
    DisplayMenuTextField_t text_field;

    if (!menu_mode_active)
        return 0U;

    if (menu_text_edit_field != DISPLAY_MENU_TEXT_FIELD_NONE)
    {
        Display_MenuTextEditExit();
        return 1U;
    }

    switch (menu_page)
    {
    case DISPLAY_MENU_PAGE_ROOT:
        switch (menu_root_selection_index)
        {
        case 0U:
            menu_bank_selection_index = current_bank;
            menu_page = DISPLAY_MENU_PAGE_BANKS;
            break;
        case 1U:
            menu_device_selection_index = 0U;
            menu_page = DISPLAY_MENU_PAGE_DEVICES;
            break;
        case 2U:
            menu_page = DISPLAY_MENU_PAGE_GLOBAL;
            menu_global_selection_index = 0U;
            break;
        default:
            return 0U;
        }
        break;
    case DISPLAY_MENU_PAGE_BANKS:
        menu_active_bank_index = menu_bank_selection_index;
        menu_bank_edit_selection_index = 0U;
        menu_page = DISPLAY_MENU_PAGE_BANK_EDIT;
        break;
    case DISPLAY_MENU_PAGE_BANK_EDIT:
        text_field = Display_GetMenuTextFieldForSelection();
        if (text_field != DISPLAY_MENU_TEXT_FIELD_NONE)
        {
            Display_MenuTextEditEnter(text_field);
            return 1U;
        }

        if (menu_bank_edit_selection_index == 2U)
        {
            Display_ResetFunctionButtonMessageEditor();
            menu_function_button_selection_index = 0U;
            menu_page = DISPLAY_MENU_PAGE_FUNCTION_BUTTON;
        }
        else
            return 0U;
        break;
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON:
        text_field = Display_GetMenuTextFieldForSelection();
        if (text_field != DISPLAY_MENU_TEXT_FIELD_NONE)
        {
            Display_MenuTextEditEnter(text_field);
            return 1U;
        }

        return 0U;
    case DISPLAY_MENU_PAGE_DEVICE_EDIT:
        text_field = Display_GetMenuTextFieldForSelection();
        if (text_field != DISPLAY_MENU_TEXT_FIELD_NONE)
        {
            Display_MenuTextEditEnter(text_field);
            return 1U;
        }

        if (menu_device_edit_selection_index == 7U)
        {
            menu_page = DISPLAY_MENU_PAGE_DEVICE_INIT_CONFIRM;
            Display_MenuRefresh();
            return 1U;
        }

        return 0U;
    case DISPLAY_MENU_PAGE_DEVICES:
        menu_active_device_index = menu_device_selection_index;
        menu_device_edit_selection_index = 0U;
        menu_device_cc_field_index = 0U;
        menu_device_cc_field_edit_active = 0U;
        menu_page = DISPLAY_MENU_PAGE_DEVICE_EDIT;
        break;
    default:
        return 0U;
    }

    Display_MenuRefresh();
    return 1U;
}

uint8_t Display_MenuBack(void)
{
    if (!menu_mode_active)
        return 0U;

    switch (menu_page)
    {
    case DISPLAY_MENU_PAGE_BANKS:
        menu_root_selection_index = 0U;
        break;
    case DISPLAY_MENU_PAGE_BANK_EDIT:
        menu_page = DISPLAY_MENU_PAGE_BANKS;
        Display_MenuRefresh();
        return 1U;
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON:
        menu_page = DISPLAY_MENU_PAGE_BANK_EDIT;
        Display_MenuRefresh();
        return 1U;
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES:
        Display_ResetFunctionButtonMessageEditor();
        menu_page = DISPLAY_MENU_PAGE_FUNCTION_BUTTON;
        Display_MenuRefresh();
        return 1U;
    case DISPLAY_MENU_PAGE_DEVICE_EDIT:
        menu_device_cc_field_index = 0U;
        menu_page = DISPLAY_MENU_PAGE_DEVICES;
        Display_MenuRefresh();
        return 1U;
    case DISPLAY_MENU_PAGE_DEVICE_INIT_CONFIRM:
        menu_page = DISPLAY_MENU_PAGE_DEVICE_EDIT;
        Display_MenuRefresh();
        return 1U;
    case DISPLAY_MENU_PAGE_DEVICES:
        menu_root_selection_index = 1U;
        break;
    case DISPLAY_MENU_PAGE_GLOBAL:
        menu_root_selection_index = 2U;
        break;
    case DISPLAY_MENU_PAGE_ROOT:
        Display_MenuExit();
        return 1U;
    default:
        return 0U;
    }

    menu_page = DISPLAY_MENU_PAGE_ROOT;
    Display_MenuRefresh();
    return 1U;
}

uint8_t Display_MenuAdjustValue(int8_t delta)
{
    RuntimeConfigGlobal_t *global = RuntimeConfig_GetMutableGlobal();
    RuntimeConfigBank_t *bank = RuntimeConfig_GetMutableBank(menu_active_bank_index);
    RuntimeConfigDevice_t *device = RuntimeConfig_GetMutableDevice(menu_active_device_index);
    uint8_t changed = 0U;

    if (!menu_mode_active || delta == 0)
        return 0U;

    if (menu_text_edit_field != DISPLAY_MENU_TEXT_FIELD_NONE)
    {
        changed = Display_MenuAdjustTextCharacter(delta);
        if (!changed)
            return 0U;

        RuntimeConfig_MarkDirty();
        Display_MenuRedrawCurrentValue();
        return 1U;
    }

    if (Display_MenuFunctionButtonMessagePageIsActive())
    {
        changed = Display_AdjustFunctionButtonMessageValue(delta);
        if (!changed)
            return 0U;

        RuntimeConfig_MarkDirty();
        Display_MenuRedrawCurrentItem();
        return 1U;
    }

    if (menu_page == DISPLAY_MENU_PAGE_BANK_EDIT)
    {
        if (!bank)
            return 0U;

        switch (menu_bank_edit_selection_index)
        {
        case 1U:
            changed = Display_AdjustWrappedU8(&bank->wet_dry_enabled, 0U, 1U, delta);
            break;
        case 3U:
            changed = Display_AdjustWrappedU8(&bank->midi_clock_bar_count,
                                              RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MIN,
                                              RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MAX,
                                              delta);
            break;
        default:
            break;
        }
    }
    else if (menu_page == DISPLAY_MENU_PAGE_DEVICE_EDIT)
    {
        MidiCC_t *device_cc = Display_GetSelectedDeviceCc(device);

        if (!device)
            return 0U;

        if (Display_MenuDeviceCcRowIsSelected() && device_cc)
        {
            switch (menu_device_cc_field_index)
            {
            case 0U:
                changed = Display_AdjustWrappedOptionalU8(&device_cc->cc,
                                                          PRESET_CC_NUMBER_UNUSED,
                                                          0U,
                                                          127U,
                                                          delta);
                break;
            case 1U:
                changed = Display_AdjustWrappedU8(&device_cc->value, 0U, 127U, delta);
                break;
            default:
                break;
            }
        }
        else
        {
            switch (menu_device_edit_selection_index)
            {
            case 1U:
                changed = Display_AdjustWrappedU8(&device->max_preset, 1U, 127U, delta);
                break;
            case 2U:
                changed = Display_AdjustWrappedU8(&device->channel, 1U, 16U, delta);
                break;
            default:
                break;
            }
        }
    }
    else if (menu_page == DISPLAY_MENU_PAGE_GLOBAL)
    {
        if (!global)
            return 0U;

        switch (menu_global_selection_index)
        {
        case 0U:
            changed = Display_AdjustWrappedU8(&global->startup_delay_seconds, 0U, 60U, delta);
            break;

        case 1U:
            changed = Display_AdjustWrappedU8(&global->screensaver_timeout_minutes, 1U, 60U, delta);
            break;

        case 2U:
        {
            uint8_t sync_style = (uint8_t)global->sync_style;

            changed = Display_AdjustWrappedU8(&sync_style,
                                              (uint8_t)RUNTIME_CONFIG_SYNC_STYLE_MIDI_CLOCK,
                                              (uint8_t)RUNTIME_CONFIG_SYNC_STYLE_TAP_TEMPO_CC,
                                              delta);
            if (changed)
                global->sync_style = (RuntimeConfigSyncStyle_t)sync_style;
            break;
        }

        case 3U:
        {
            uint8_t brightness_ui = Display_GetGlobalBrightnessUiValue(global->backlight_brightness);

            changed = Display_AdjustWrappedU8(&brightness_ui, 0U, 255U, delta);
            if (changed)
            {
                global->backlight_brightness = Display_GetBrightnessFromUiValue(brightness_ui);
                Display_ApplyConfiguredBacklightBrightnessNow();
            }
            break;
        }

        default:
            break;
        }
    }
    else
        return 0U;

    if (!changed)
        return 0U;

    RuntimeConfig_MarkDirty();
    Display_MenuRedrawCurrentValue();
    return 1U;
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
    uint8_t previous_right_first_item;
    uint8_t current_right_first_item;

    if (!preset)
        return 0U;

    previous_first_slot = main_info_first_slot;
    if (!Display_MainInfoScrollBy(delta))
        return 0U;

    previous_right_first_item = Display_GetMainInfoRightFirstItemForFirstSlot(previous_first_slot);
    current_right_first_item = Display_GetMainInfoRightFirstItem();

    Display_DrawMainInfoLeftRowsOnly(preset, previous_first_slot);
    if (Display_GetMainInfoRightFirstItemRenderState(previous_right_first_item)
        != Display_GetMainInfoRightFirstItemRenderState(current_right_first_item))
    {
        Display_DrawMainInfoRightRowsOnly(preset, previous_right_first_item);
    }

    return 1U;
}

void Display_PresetEditEnter(void)
{
    preset_edit_mode_active = 1U;
    preset_edit_cursor_index = 0U;
    preset_name_edit_active = 0U;
    preset_name_edit_cursor_index = 0U;
    main_info_first_slot = Display_GetPresetEditScrollFirstSlot(preset_edit_cursor_index);
    Display_DrawFootbar();
}

void Display_PresetEditExit(void)
{
    preset_edit_mode_active = 0U;
    preset_edit_cursor_index = 0U;
    preset_name_edit_active = 0U;
    preset_name_edit_cursor_index = 0U;
    main_info_first_slot = 0U;
    Display_DrawFootbar();
}

uint8_t Display_PresetEditIsActive(void)
{
    return preset_edit_mode_active;
}

void Display_PresetNameEditEnter(void)
{
    DisplayPresetEditField_t field = Display_PresetEditGetField();

    if (!preset_edit_mode_active || field.type != DISPLAY_PRESET_EDIT_FIELD_NAME)
        return;

    preset_name_edit_active = 1U;
    preset_name_edit_cursor_index = 0U;
    preset_name_full_refresh_pending = 1U;
}

void Display_PresetNameEditExit(void)
{
    preset_name_edit_active = 0U;
    preset_name_full_refresh_pending = 0U;
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
    /* Redraw only the two cells whose highlight state changed. */
    Display_DrawPresetNameCell(preset, Display_GetPresetNameCellIndex(preset, previous_index));
    Display_DrawPresetNameCell(preset, Display_GetPresetNameCellIndex(preset, preset_name_edit_cursor_index));
    return 1U;
}

uint8_t Display_PresetNameEditGetCursorIndex(void)
{
    return preset_name_edit_cursor_index;
}

uint8_t Display_PresetEditMoveCursor(int8_t delta)
{
    int16_t next_cursor;

    if (!preset_edit_mode_active || delta == 0)
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
    uint8_t previous_right_first_item;
    uint8_t current_right_first_item;
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
        previous_right_first_item = Display_GetMainInfoRightFirstItemForFirstSlot(previous_first_slot);
        current_right_first_item = Display_GetMainInfoRightFirstItem();

        Display_DrawMainInfoLeftRowsOnly(preset, previous_first_slot);

        if (Display_GetMainInfoRightFirstItemRenderState(previous_right_first_item)
            != Display_GetMainInfoRightFirstItemRenderState(current_right_first_item))
        {
            Display_DrawMainInfoRightRowsOnly(preset, previous_right_first_item);
        }
        else
        {
            if (previous_field.type == DISPLAY_PRESET_EDIT_FIELD_RELAY
                && previous_field.itemIndex >= current_right_first_item
                && previous_field.itemIndex < (uint8_t)(current_right_first_item + MAIN_INFO_ROW_COUNT))
            {
                Display_DrawMainInfoRelayField(preset,
                                               previous_field.itemIndex,
                                               main_info_row_y[previous_field.itemIndex - current_right_first_item],
                                               0U);
            }

            if (next_field.type == DISPLAY_PRESET_EDIT_FIELD_RELAY
                && next_field.itemIndex >= current_right_first_item
                && next_field.itemIndex < (uint8_t)(current_right_first_item + MAIN_INFO_ROW_COUNT))
            {
                Display_DrawMainInfoRelayField(preset,
                                               next_field.itemIndex,
                                               main_info_row_y[next_field.itemIndex - current_right_first_item],
                                               1U);
            }
        }
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
        {
            Display_DrawMainInfoProgramField(preset,
                                             previous_field.itemIndex,
                                             main_info_row_y[previous_field.itemIndex - previous_first_slot],
                                             0U);
        }
        break;

    case DISPLAY_PRESET_EDIT_FIELD_RELAY:
        if (previous_field.itemIndex < MAIN_INFO_ROW_COUNT)
        {
            Display_DrawMainInfoRelayField(preset,
                                           previous_field.itemIndex,
                                           main_info_row_y[previous_field.itemIndex],
                                           0U);
        }
        break;

    case DISPLAY_PRESET_EDIT_FIELD_CC_CHANNEL:
    case DISPLAY_PRESET_EDIT_FIELD_CC_NUMBER:
    case DISPLAY_PRESET_EDIT_FIELD_CC_VALUE:
        if ((PRESET_DEVICE_SLOTS + previous_field.itemIndex) >= previous_first_slot
            && (PRESET_DEVICE_SLOTS + previous_field.itemIndex) < (uint8_t)(previous_first_slot + MAIN_INFO_ROW_COUNT))
        {
            Display_DrawMainInfoCcField(preset,
                                        previous_field.itemIndex,
                                        previous_field.type,
                                        main_info_row_y[(PRESET_DEVICE_SLOTS + previous_field.itemIndex) - previous_first_slot],
                                        0U);
        }
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
    uint16_t row_y;

    if (!preset || !preset_edit_mode_active)
        return;

    field = Display_PresetEditGetField();
    row_index = 0xFFU;

    switch (field.type)
    {
    case DISPLAY_PRESET_EDIT_FIELD_NAME:
        if (Display_PresetNameEditIsActive()
            && !preset_name_full_refresh_pending
            && Display_GetPresetNameRenderLength(preset) == preset_name_last_render_length
            && Display_GetPresetNamePadLeft(preset) == preset_name_last_pad_left)
        {
            Display_DrawPresetNameCell(preset,
                                       Display_GetPresetNameCellIndex(preset, preset_name_edit_cursor_index));
            return;
        }

        Display_DrawPresetName(preset);
        return;

    case DISPLAY_PRESET_EDIT_FIELD_PROGRAM:
        if (field.itemIndex < main_info_first_slot)
            return;

        row_index = (uint8_t)(field.itemIndex - main_info_first_slot);
        if (row_index >= MAIN_INFO_ROW_COUNT)
            return;

        row_y = main_info_row_y[row_index];
        Display_DrawMainInfoProgramField(preset, field.itemIndex, row_y, 1U);
        return;

    case DISPLAY_PRESET_EDIT_FIELD_RELAY:
        if (field.itemIndex >= PRESET_RELAY_COUNT)
            return;

        row_index = field.itemIndex;
        if (row_index >= MAIN_INFO_ROW_COUNT)
            return;

        row_y = main_info_row_y[row_index];
    Display_DrawMainInfoRelayField(preset, field.itemIndex, row_y, 1U);
        return;

    case DISPLAY_PRESET_EDIT_FIELD_CC_CHANNEL:
    case DISPLAY_PRESET_EDIT_FIELD_CC_NUMBER:
    case DISPLAY_PRESET_EDIT_FIELD_CC_VALUE:
        row_index = (uint8_t)((PRESET_DEVICE_SLOTS + field.itemIndex) - main_info_first_slot);
        if (row_index >= MAIN_INFO_ROW_COUNT)
            return;

        row_y = main_info_row_y[row_index];
        Display_DrawMainInfoCcField(preset, field.itemIndex, field.type, row_y, 1U);
        return;

    default:
        return;
    }
}

/* ?????? Display_DrawMainScreen ??????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
 * Full refresh of the main screen for a given preset + BPM value.
 * Called by App_ActivatePreset() in presets.c and by the screensaver wakeup.
 *
 * Sections drawn:
 *   1. Footer bar (only if dirty ??? avoids a needless SPI burst every call).
 *   2. BPM display (top-right).
 *   3. Preset name (centred, padded to exactly 20 characters so the previous
 *      name is fully overwritten even if it was longer).
 *   4. Three info rows: one per device slot.
 *        Left  column: MIDI channel + program number sent to that device.
 *        Right column: relay state for the first two rows.
 *      program == 0xFF means that slot is unused ??? shown as "CH -: ---".
 * ????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */
void Display_DrawMainScreen(const Preset_t *p, uint16_t bpm)
{
    if (menu_mode_active)
    {
        Display_MenuRefresh();
        return;
    }

    if (main_layout_dirty)
    {
        ST7796_FillScreen(DISPLAY_BG_COLOUR);
        bpm_display_valid = 0U;
        Display_DrawMainLayout();
    }

    Display_UpdateBPM(bpm);
    Display_DrawMainModeHeader();
    Display_DrawPresetName(p);
    Display_WriteCenteredPaddedText32(MAIN_BANK_TEXT_Y,
                                      Presets_GetBankName(current_bank),
                                      MAIN_BANK_TEXT_CHARS,
                                      MAIN_BANK_FONT,
                                      MAIN_BANK_COLOUR);
    Display_DrawMainInfoRows(p);
}

void Display_RefreshPresetEditMode(const Preset_t *p, uint16_t bpm)
{
    if (menu_mode_active)
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

/* ?????? Display_UpdateBPM ?????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
 * Redraws only the BPM value in the top-right corner.
 * Called both from Display_DrawMainScreen and from BPM_Service()
 * (bpm_functions.c) on every tap so the number updates immediately without
 * redrawing the whole screen.
 * ????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */
void Display_UpdateBPM(uint16_t bpm)
{
    if (menu_mode_active)
        return;

    uint16_t display_bpm_x10 = (uint16_t)(bpm * 10U);
    uint8_t use_external = MidiClockGetExternalBpmX10(&display_bpm_x10);
    uint8_t sync_lost = MidiClockIsSyncLost();
    uint8_t was_sync_lost = bpm_display_sync_lost;
    /* UI redraw throttling only; external BPM timing and sync-loss detection
     * already use TIM2 inside midi_functions.c. */
    uint32_t now_ms = HAL_GetTick();
    uint8_t full_redraw;
    char buf[20];

    Display_UpdateTransportBarBeat();

    if (sync_lost)
    {
        if (bpm_display_valid && bpm_display_sync_lost)
        {
            return;
        }

        Display_ClearBpmArea();
        ST7796_WriteString32(BPM_SYNC_LOST_X, BPM_TEXT_Y, BPM_SYNC_LOST_TEXT, BPM_FONT, BPM_SYNC_LOST_COLOUR, BPM_BG_COLOUR);

        bpm_display_valid = 1U;
        bpm_display_external = 0U;
        bpm_display_sync_lost = 1U;
        bpm_display_value_x10 = 0U;
        bpm_display_external_update_tick = 0U;
        bpm_display_internal_text[0] = '\0';
        bpm_display_external_text[0] = '\0';
        bpm_display_internal_head_x = 0U;
        return;
    }

    bpm_display_sync_lost = 0U;

    if (!use_external)
    {
        if (bpm_display_valid
         && !was_sync_lost
         && !bpm_display_external
         && bpm_display_value_x10 == display_bpm_x10)
        {
            return;
        }

        full_redraw = (uint8_t)(!bpm_display_valid || was_sync_lost || bpm_display_external);
        if (full_redraw)
        {
            Display_ClearBpmArea();
        }

        Display_FormatBpmText(buf, sizeof(buf), BPM_TEXT_MODE_INTERNAL, bpm);
        uint8_t internal_head_len = (uint8_t)strlen(buf);
        uint16_t internal_head_x = (uint16_t)(BPM_INTERNAL_SUFFIX_TEXT_X - ((uint16_t)internal_head_len * BPM_FONT.width));

        if (full_redraw)
        {
            ST7796_WriteString32(BPM_INTERNAL_SUFFIX_TEXT_X,
                                 BPM_TEXT_Y,
                                 BPM_INTERNAL_SUFFIX_TEXT,
                                 BPM_FONT,
                                 BPM_INTERNAL_COLOUR,
                                 BPM_BG_COLOUR);
            ST7796_WriteString32(internal_head_x,
                                 BPM_TEXT_Y,
                                 buf,
                                 BPM_FONT,
                                 BPM_INTERNAL_COLOUR,
                                 BPM_BG_COLOUR);
        }

        if (!full_redraw)
        {
            if (internal_head_x != bpm_display_internal_head_x)
            {
                ST7796_DrawFilledRectangle(BPM_INTERNAL_HEAD_TEXT_X,
                                           BPM_TEXT_Y,
                                           BPM_INTERNAL_HEAD_AREA_W,
                                           BPM_FONT.height,
                                           BPM_BG_COLOUR);
                ST7796_WriteString32(internal_head_x,
                                     BPM_TEXT_Y,
                                     buf,
                                     BPM_FONT,
                                     BPM_INTERNAL_COLOUR,
                                     BPM_BG_COLOUR);
            }
            else
            {
                Display_UpdateBpmTextCells(internal_head_x,
                                           BPM_TEXT_Y,
                                           BPM_INTERNAL_HEAD_TEXT_CHARS,
                                           bpm_display_internal_text,
                                           buf,
                                           BPM_INTERNAL_COLOUR);
            }
        }

        bpm_display_internal_head_x = internal_head_x;
        strcpy(bpm_display_internal_text, buf);
        bpm_display_external_text[0] = '\0';

        bpm_display_valid = 1U;
        bpm_display_external = 0U;
        bpm_display_sync_lost = 0U;
        bpm_display_value_x10 = display_bpm_x10;
        bpm_display_external_update_tick = 0U;
        return;
    }


    full_redraw = (uint8_t)(!bpm_display_valid || was_sync_lost || !bpm_display_external);
    if (full_redraw)
    {
        Display_ClearBpmArea();
    }
    else
    {
        if ((now_ms - bpm_display_external_update_tick) < BPM_EXT_UPDATE_MIN_INTERVAL_MS)
        {
            return;
        }

        uint16_t delta_x10;
        uint16_t hysteresis_x10 = Display_GetExternalBpmHysteresisX10(bpm_display_value_x10);
        uint16_t upper_threshold_x10 = (uint16_t)(bpm_display_value_x10 + hysteresis_x10);
        uint16_t lower_threshold_x10 = (bpm_display_value_x10 > hysteresis_x10)
            ? (uint16_t)(bpm_display_value_x10 - hysteresis_x10)
            : 0U;

        if (display_bpm_x10 <= upper_threshold_x10 && display_bpm_x10 >= lower_threshold_x10)
        {
            return;
        }

        delta_x10 = (display_bpm_x10 >= bpm_display_value_x10)
            ? (uint16_t)(display_bpm_x10 - bpm_display_value_x10)
            : (uint16_t)(bpm_display_value_x10 - display_bpm_x10);

        if (delta_x10 < BPM_EXT_FORCE_UPDATE_DELTA_X10)
        {
            if (display_bpm_x10 > bpm_display_value_x10)
            {
                display_bpm_x10 = (uint16_t)(bpm_display_value_x10 + BPM_EXT_SLEW_STEP_X10);
            }
            else
            {
                display_bpm_x10 = (bpm_display_value_x10 > BPM_EXT_SLEW_STEP_X10)
                    ? (uint16_t)(bpm_display_value_x10 - BPM_EXT_SLEW_STEP_X10)
                    : 0U;
            }
        }
    }

    Display_FormatBpmText(buf, sizeof(buf), BPM_TEXT_MODE_EXTERNAL, display_bpm_x10);
    Display_UpdateBpmTextCells(BPM_EXT_TEXT_X,
                               BPM_TEXT_Y,
                               BPM_EXT_TEXT_CHARS,
                               full_redraw ? "" : bpm_display_external_text,
                               buf,
                               EXT_BPM_COLOUR);
    strcpy(bpm_display_external_text, buf);
    bpm_display_internal_text[0] = '\0';

    bpm_display_valid = 1U;
    bpm_display_external = 1U;
    bpm_display_sync_lost = 0U;
    bpm_display_value_x10 = display_bpm_x10;
    bpm_display_external_update_tick = now_ms;
}

/* ?????? Loading bar ???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
 * Draws a progress bar that fills left-to-right over duration_ms milliseconds.
 * This is a blocking call ??? it does not return until the timer expires.
 * Used during startup while the system waits for devices to power up.
 *
 * The bar is split into three text phases to keep the user entertained:
 *   0 %???33 % : "... waiting for DNA match"
 *   33%      : "DNA match found"         (shown for 1 s)
 *   33%+1 s  : "..accessing genetic markers"  (shown for 1 s)
 *   ~66%+    : text cleared
 *
 * Only the newly filled strip is drawn each iteration (fill > prev_fill),
 * so SPI traffic is proportional to progress not to loop frequency.
 */

static void Display_LoadingBarClearTextRow(void)
{
    ST7796_DrawFilledRectangle(0U, LOADING_BAR_TEXT_Y, ST7796_WIDTH, LOADING_BAR_TEXT_FONT.height, LOADING_BAR_BG_COLOUR);
}

static void Display_LoadingBarSetText(const char *text, uint16_t colour)
{
    size_t text_len = strlen(text);

    Display_LoadingBarClearTextRow();
    ST7796_WriteString((uint16_t)(ST7796_WIDTH - ((uint16_t)text_len * LOADING_BAR_TEXT_FONT.width) - LOADING_BAR_X),
                       LOADING_BAR_TEXT_Y,
                       text,
                       LOADING_BAR_TEXT_FONT,
                       colour,
                       LOADING_BAR_BG_COLOUR);
}

void Display_LoadingBar(uint32_t duration_ms)
{
    Display_LoadingBarSetText(LOADING_BAR_WAIT_TEXT, LOADING_BAR_TEXT_COLOUR);

    ST7796_DrawFilledRectangle(LOADING_BAR_X, LOADING_BAR_Y, LOADING_BAR_W, LOADING_BAR_H, LOADING_BAR_BG_COLOUR);

    uint32_t start     = HAL_GetTick();
    uint16_t prev_fill = 0U;   /* tracks how many pixels have been filled so far */
    uint8_t  phase     = 0U;   /* which text message is currently showing        */
    uint32_t phase_ts  = 0U;   /* HAL tick when the current phase started        */

    for (;;)
    {
        uint32_t elapsed = HAL_GetTick() - start;
        if (elapsed >= duration_ms) elapsed = duration_ms;  /* clamp at end */

        /* Fill only the new strip since last iteration ??? avoids redrawing
         * pixels that are already the correct colour. */
        uint16_t fill = (uint16_t)((elapsed * LOADING_BAR_W) / duration_ms);
        if (fill > prev_fill)
        {
            ST7796_DrawFilledRectangle(LOADING_BAR_X + prev_fill, LOADING_BAR_Y,
                                       fill - prev_fill, LOADING_BAR_H, LOADING_BAR_COLOUR);
            prev_fill = fill;
        }

        uint32_t now = HAL_GetTick();

        /* Phase transitions ??? check sequentially so they can't be skipped */
        if (phase == 0U && elapsed >= duration_ms / LOADING_BAR_PHASE_DIVISOR)
        {
            Display_LoadingBarSetText(LOADING_BAR_MATCH_TEXT, LOADING_BAR_TEXT_COLOUR);
            phase    = 1U;
            phase_ts = now;
        }
        if (phase == 1U && now - phase_ts >= LOADING_BAR_PHASE_HOLD_MS)
        {
            Display_LoadingBarSetText(LOADING_BAR_MARKERS_TEXT, LOADING_BAR_TEXT_COLOUR);
            phase    = 2U;
            phase_ts = now;
        }
        if (phase == 2U && now - phase_ts >= LOADING_BAR_PHASE_HOLD_MS)
        {
            Display_LoadingBarClearTextRow();
            phase = 3U;  /* text cleared ??? stay here until bar finishes */
        }

        if (elapsed >= duration_ms) break;
    }
}

/* ?????? Display_LoadingBarClear ???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
 * Clears the loading bar and shows a brief "mutation complete" message.
 * Called after Display_LoadingBar() returns, just before the fade-out.
 * ????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */
void Display_LoadingBarClear(void)
{
    ST7796_DrawFilledRectangle(LOADING_BAR_X, LOADING_BAR_Y, LOADING_BAR_W, LOADING_BAR_H, LOADING_BAR_BG_COLOUR);
    Display_LoadingBarSetText(LOADING_BAR_DONE_TEXT, LOADING_BAR_TEXT_COLOUR);
    HAL_Delay(LOADING_BAR_PHASE_HOLD_MS);  /* leave message visible for 1 s before fade */
    Display_LoadingBarClearTextRow();
}

/* ?????? Screensaver ???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
 * Idle mode is handled by fading the display backlight out after a period of
 * inactivity, then fading it back in on the next activity event.
 *
 * Activation: triggers after SS_TIMEOUT_MS of inactivity.
 *   Any call to Display_ScreensaverActivity() resets the inactivity timer.
 *
 * Deactivation: the first Display_ScreensaverUpdate() call after
 *   ss_last_activity has been refreshed redraws the main screen and exits.
 */

static uint32_t screensaver_last_activity_tick = 0U;   /* tick of last user interaction */
static uint8_t screensaver_active = 0U;                /* 1 while screensaver is running */

/* ?????? Display_ScreensaverActivity ???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
 * Records the current tick as the last user activity.
 * Call this from any event that should reset the screensaver timer:
 *   button presses (button_functions.c), tap tempo (main.cpp EXTI callback),
 *   preset changes (presets.c App_ActivatePreset).
 * ????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */
void Display_ScreensaverActivity(void)
{
    screensaver_last_activity_tick = HAL_GetTick();
}

uint8_t Display_ScreensaverIsActive(void)
{
    return screensaver_active;
}

void Display_ScreensaverDismiss(void)
{
    if (!screensaver_active)
        return;

    screensaver_active = 0U;
    main_layout_dirty = 0U;
    Display_BL_FadeIn();
}

/* ?????? Display_ScreensaverUpdate ?????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
 * Called from the main while(1) loop every iteration.
 * When inactive: checks if timeout has elapsed and fades the backlight out.
 * When active:   waits for activity and restores the display on wake.
 * ????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */
void Display_ScreensaverUpdate(const Preset_t *p, uint16_t bpm)
{
    uint32_t screensaver_timeout_ms = Display_GetConfiguredScreensaverTimeoutMs();
    uint32_t now = HAL_GetTick();

    if (!screensaver_active)
    {
        /* Not yet active ??? check if we've been idle long enough */
        if (now - screensaver_last_activity_tick >= screensaver_timeout_ms)
        {
            screensaver_active = 1U;
            main_layout_dirty = 0U;
            Display_BL_FadeOut();
        }
        return;
    }

    /* Screensaver is active ??? check for a wake event */
    if (now - screensaver_last_activity_tick < screensaver_timeout_ms)
    {
        /* Activity was recorded since we went to sleep ??? wake up */
        //Display_ScreensaverDismiss();
        Display_DrawMainScreen(p, bpm);
        return;
    }
}

