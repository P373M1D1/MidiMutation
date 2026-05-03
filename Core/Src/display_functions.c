#include "display_functions.h"
#include "button_functions.h"
#include "midi_functions.h"
#include "midi_devices.h"
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
#define BL_BRIGHTNESS 2095U     /* max DAC value for full backlight brightness */

#define DISPLAY_BG_COLOUR              BLACK               // default background colour for full-screen clears and text backgrounds

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
    for (uint32_t step = 0U; step <= BL_STEPS; step++)
    {
        DAC->DHR12R1 = (step * BL_BRIGHTNESS) / BL_STEPS;          /* linear ramp up    */
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
    for (uint32_t step = BL_STEPS; ; step--)
    {
        DAC->DHR12R1 = (step * BL_BRIGHTNESS) / BL_STEPS;          /* linear ramp down  */
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
#define MAIN_FOOTBAR_LEFT_TEXT         "SCROLL/ EDIT"              // label for the left footer region under encoder 1
#define MAIN_FOOTBAR_CENTER_TEXT       "VALUE / MENU"               // currently unused middle footer region label
#define MAIN_FOOTBAR_RIGHT_TEXT        "TEMPO / EXIT"               // label for the right footer region under the tempo encoder
#define MAIN_INFO_LEFT_X               30U                  // x origin of the left info column (MIDI programs)
#define MAIN_INFO_RIGHT_X              220U                 // x origin of the right info column (relay / special state)
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
#define MAIN_INFO_EDIT_INITIAL_PROGRAM_COUNT 3U             // the first three program slots stay on the first page before relay editing
#define MAIN_INFO_EDIT_FIELD_COUNT     (PRESET_DEVICE_SLOTS + PRESET_RELAY_COUNT + (PRESET_CC_SLOT_COUNT * 3U)) // number of editable fields in preset edit mode
#define MAIN_INFO_SHARED_PAD_CHARS     2U                   // extra chars cleared when special-function text shrinks
#define MAIN_UNUSED_PROGRAM            0xFFU                // sentinel meaning no MIDI program is assigned to that slot
#define MAIN_EMPTY_RIGHT_INFO_TEXT     "                "   // blank filler used to clear an unused right-side row
#define MAIN_SCROLL_INDICATOR_X        8U                   // x position of the device-list scroll indicator triangles
#define MAIN_SCROLL_INDICATOR_W        9U                   // width of the scroll indicator triangles
#define MAIN_SCROLL_INDICATOR_H        5U                   // height of the scroll indicator triangles
#define MAIN_SCROLL_INDICATOR_COLOUR   MAIN_INFO_TEXT_COLOUR // colour of the up/down scroll indicators
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
#define MAIN_SPECIAL_FUNCTION_BUTTON_PREFIX            "Vita: " // label shown ahead of the special-function-button state
#define MAIN_SPECIAL_FUNCTION_BUTTON_ACTIVE_TEXT       "undead" // text shown when the special-function button mode is active
#define MAIN_SPECIAL_FUNCTION_BUTTON_INACTIVE_TEXT     "dead"   // text shown when the special-function button mode is inactive
#define MAIN_SPECIAL_FUNCTION_BUTTON_ACTIVE_COLOUR     WHITE // text colour for the active special-function-button state
#define MAIN_SPECIAL_FUNCTION_BUTTON_INACTIVE_COLOUR   CHARCOAL // text colour for the inactive special-function-button state
#define MAIN_SPECIAL_FUNCTION_BUTTON_ACTIVE_BG         DARK_RED // highlight background behind the active special-function-button state
#define MAIN_SPECIAL_FUNCTION_BUTTON_INACTIVE_BG       DISPLAY_BG_COLOUR // background behind the inactive special-function-button state
#define MAIN_SPECIAL_FUNCTION_BUTTON_PREFIX_COLOUR     MAIN_INFO_TEXT_COLOUR // colour of the special-function-button label prefix
#define MAIN_SPECIAL_FUNCTION_BUTTON_PREFIX_BG         MAIN_INFO_TEXT_BG_COLOUR // background behind the special-function-button label prefix
#define MAIN_SPECIAL_FUNCTION_BUTTON_BORDER_COLOUR     DISPLAY_BG_COLOUR // top/bottom border colour used to style the active special-function state
#define MAIN_INFO_HIGHLIGHT_BORDER_H   2U                   // thickness of the top and bottom highlight bars around active state text

static const uint16_t main_info_row_y[MAIN_INFO_ROW_COUNT] = {184U, 220U, 256U};

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
static char transport_barbeat_text[4] = "";
static uint8_t main_info_first_slot = 0U;
static uint8_t preset_edit_mode_active = 0U;
static uint8_t preset_edit_cursor_index = 0U;

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
#define TRANSPORT_BARBEAT_TEXT_CHARS    3U                   // fixed width for values like "1.1" or "-.-"
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

#define SCREENSAVER_TIMEOUT_MS          (10UL * 60UL * 1000UL) // idle time before the backlight-only screensaver activates

static DisplayPresetEditField_t Display_GetPresetEditFieldForCursor(uint8_t cursor_index)
{
    DisplayPresetEditField_t field = { DISPLAY_PRESET_EDIT_FIELD_NONE, 0U };

    if (cursor_index < MAIN_INFO_EDIT_INITIAL_PROGRAM_COUNT)
    {
        field.type = DISPLAY_PRESET_EDIT_FIELD_PROGRAM;
        field.itemIndex = cursor_index;
        return field;
    }

    cursor_index = (uint8_t)(cursor_index - MAIN_INFO_EDIT_INITIAL_PROGRAM_COUNT);
    if (cursor_index < PRESET_RELAY_COUNT)
    {
        field.type = DISPLAY_PRESET_EDIT_FIELD_RELAY;
        field.itemIndex = cursor_index;
        return field;
    }

    cursor_index = (uint8_t)(cursor_index - PRESET_RELAY_COUNT);
    if (cursor_index < (PRESET_DEVICE_SLOTS - MAIN_INFO_EDIT_INITIAL_PROGRAM_COUNT))
    {
        field.type = DISPLAY_PRESET_EDIT_FIELD_PROGRAM;
        field.itemIndex = (uint8_t)(MAIN_INFO_EDIT_INITIAL_PROGRAM_COUNT + cursor_index);
        return field;
    }

    cursor_index = (uint8_t)(cursor_index - (PRESET_DEVICE_SLOTS - MAIN_INFO_EDIT_INITIAL_PROGRAM_COUNT));
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

    if (field.type == DISPLAY_PRESET_EDIT_FIELD_PROGRAM)
    {
        if (field.itemIndex < MAIN_INFO_ROW_COUNT)
            return 0U;

        return (uint8_t)(field.itemIndex - (MAIN_INFO_ROW_COUNT - 1U));
    }

    if (field.type == DISPLAY_PRESET_EDIT_FIELD_RELAY)
        return 0U;

    return (uint8_t)((PRESET_DEVICE_SLOTS - (MAIN_INFO_ROW_COUNT - 1U)) + field.itemIndex);
}

static uint8_t Display_PresetEditFieldsMatch(DisplayPresetEditField_t first,
                                             DisplayPresetEditField_t second)
{
    return (first.type == second.type && first.itemIndex == second.itemIndex) ? 1U : 0U;
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
    char next_text[4];

    external_signal_present = MidiClockIsExternalSignalPresent();
    sync_lost = MidiClockIsSyncLost();

    if (MidiClockGetBarBeat(&bar, &beat))
    {
        next_text[0] = (char)('0' + bar);
        next_text[1] = '.';
        next_text[2] = (char)('0' + beat);
        next_text[3] = '\0';
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

static uint8_t Display_GetMainInfoRightFirstItem(void)
{
    uint8_t program_scroll_max = Display_GetMainInfoProgramScrollMax();

    return (main_info_first_slot > program_scroll_max)
        ? (uint8_t)(main_info_first_slot - program_scroll_max)
        : 0U;
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

static void Display_WriteCenteredPaddedText32(uint16_t y,
                                              const char *text,
                                              uint8_t width_chars,
                                              FontDef32 font,
                                              uint16_t colour)
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
                         DISPLAY_BG_COLOUR);
}

static void Display_DrawMainInfoProgramRow(const Preset_t *preset,
                                           uint8_t slot_index,
                                           uint16_t row_y)
{
    char prefix[16];
    const MidiDevice_t *device = MidiDevices_Get(slot_index);
    uint8_t channel = device ? device->channel : MAIN_UNUSED_PROGRAM;
    uint16_t prefix_px;

    if (channel == MAIN_UNUSED_PROGRAM)
    {
        ST7796_WriteString32(MAIN_INFO_LEFT_X,
                             row_y,
                             "CH -: ---",
                             MAIN_INFO_FONT,
                             MAIN_INFO_TEXT_COLOUR,
                             MAIN_INFO_TEXT_BG_COLOUR);
        return;
    }

    snprintf(prefix, sizeof(prefix), "CH %u: ", channel);
    prefix_px = MAIN_INFO_FONT.width * (uint16_t)strlen(prefix);

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
            ST7796_WriteString32(MAIN_INFO_LEFT_X + prefix_px,
                                 row_y,
                                 program_text,
                                 MAIN_INFO_FONT,
                                 MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR,
                                 program_is_shared ? MAIN_INFO_EDIT_CURSOR_SHARED_BG_COLOUR : MAIN_INFO_EDIT_CURSOR_BG_COLOUR);
            return;
        }

        if (program_is_shared)
        {
            ST7796_WriteString32(MAIN_INFO_LEFT_X + prefix_px,
                                 row_y,
                                 program_text,
                                 MAIN_INFO_FONT,
                                 MAIN_INFO_SHARED_TEXT_COLOUR,
                                 MAIN_INFO_SHARED_BG_COLOUR);
            return;
        }

        ST7796_WriteString32(MAIN_INFO_LEFT_X + prefix_px,
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
    value_x = (uint16_t)(MAIN_INFO_LEFT_X + (strlen("CH 0: ") * MAIN_INFO_FONT.width));

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

    cc = &preset->cc[cc_index];
    switch (field_type)
    {
    case DISPLAY_PRESET_EDIT_FIELD_CC_CHANNEL:
        if (cc->channel == PRESET_CC_CHANNEL_UNUSED)
            strcpy(field_text, "--");
        else
            snprintf(field_text, sizeof(field_text), "%02u", cc->channel);
        field_x = (uint16_t)(MAIN_INFO_LEFT_X + (strlen("CH ") * MAIN_INFO_FONT.width));
        break;

    case DISPLAY_PRESET_EDIT_FIELD_CC_NUMBER:
        if (cc->cc_number == PRESET_CC_NUMBER_UNUSED)
            strcpy(field_text, "---");
        else
            snprintf(field_text, sizeof(field_text), "%3u", cc->cc_number);
        field_x = (uint16_t)(MAIN_INFO_LEFT_X + ((strlen("CH ") + MAIN_INFO_CC_CHANNEL_DIGITS + strlen(" CC:")) * MAIN_INFO_FONT.width));
        break;

    case DISPLAY_PRESET_EDIT_FIELD_CC_VALUE:
        if (cc->value == PRESET_CC_VALUE_UNUSED)
            strcpy(field_text, "---");
        else
            snprintf(field_text, sizeof(field_text), "%3u", cc->value);
        field_x = (uint16_t)(MAIN_INFO_LEFT_X + ((strlen("CH ") + MAIN_INFO_CC_CHANNEL_DIGITS + strlen(" CC:") + MAIN_INFO_CC_VALUE_DIGITS + strlen(" Value:")) * MAIN_INFO_FONT.width));
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
    const char *channel_prefix = "CH ";
    const char *cc_prefix = " CC:";
    const char *value_prefix = " Value:";
    char channel_text[MAIN_INFO_CC_CHANNEL_DIGITS + 2U];
    char cc_number_text[MAIN_INFO_CC_VALUE_DIGITS + 1U];
    char value_text[MAIN_INFO_CC_VALUE_DIGITS + 1U];
    uint16_t draw_x = MAIN_INFO_LEFT_X;
    const PresetCCSlot_t *cc = &preset->cc[cc_index];
    DisplayPresetEditField_t edit_field = Display_PresetEditGetField();
    uint8_t highlight_channel = (edit_field.type == DISPLAY_PRESET_EDIT_FIELD_CC_CHANNEL && edit_field.itemIndex == cc_index) ? 1U : 0U;
    uint8_t highlight_cc_number = (edit_field.type == DISPLAY_PRESET_EDIT_FIELD_CC_NUMBER && edit_field.itemIndex == cc_index) ? 1U : 0U;
    uint8_t highlight_value = (edit_field.type == DISPLAY_PRESET_EDIT_FIELD_CC_VALUE && edit_field.itemIndex == cc_index) ? 1U : 0U;

    if (cc->channel == PRESET_CC_CHANNEL_UNUSED)
        strcpy(channel_text, "--");
    else
        snprintf(channel_text, sizeof(channel_text), "%02u", cc->channel);

    if (cc->cc_number == PRESET_CC_NUMBER_UNUSED)
        strcpy(cc_number_text, "---");
    else
        snprintf(cc_number_text, sizeof(cc_number_text), "%3u", cc->cc_number);

    if (cc->value == PRESET_CC_VALUE_UNUSED)
        strcpy(value_text, "---");
    else
        snprintf(value_text, sizeof(value_text), "%3u", cc->value);

    ST7796_WriteString32(draw_x,
                         row_y,
                         channel_prefix,
                         MAIN_INFO_FONT,
                         MAIN_INFO_TEXT_COLOUR,
                         MAIN_INFO_TEXT_BG_COLOUR);
    draw_x = (uint16_t)(draw_x + (strlen(channel_prefix) * MAIN_INFO_FONT.width));

    Display_DrawMainInfoCcField(preset,
                                cc_index,
                                DISPLAY_PRESET_EDIT_FIELD_CC_CHANNEL,
                                row_y,
                                highlight_channel);
    draw_x = (uint16_t)(draw_x + (strlen(channel_text) * MAIN_INFO_FONT.width));

    ST7796_WriteString32(draw_x,
                         row_y,
                         cc_prefix,
                         MAIN_INFO_FONT,
                         MAIN_INFO_TEXT_COLOUR,
                         MAIN_INFO_TEXT_BG_COLOUR);
    draw_x = (uint16_t)(draw_x + (strlen(cc_prefix) * MAIN_INFO_FONT.width));

    Display_DrawMainInfoCcField(preset,
                                cc_index,
                                DISPLAY_PRESET_EDIT_FIELD_CC_NUMBER,
                                row_y,
                                highlight_cc_number);
    draw_x = (uint16_t)(draw_x + (strlen(cc_number_text) * MAIN_INFO_FONT.width));

    ST7796_WriteString32(draw_x,
                         row_y,
                         value_prefix,
                         MAIN_INFO_FONT,
                         MAIN_INFO_TEXT_COLOUR,
                         MAIN_INFO_TEXT_BG_COLOUR);
    draw_x = (uint16_t)(draw_x + (strlen(value_prefix) * MAIN_INFO_FONT.width));

    Display_DrawMainInfoCcField(preset,
                                cc_index,
                                DISPLAY_PRESET_EDIT_FIELD_CC_VALUE,
                                row_y,
                                highlight_value);
}

static void Display_DrawMainInfoSpecialState(uint16_t row_y)
{
    uint8_t state_active = Button_SpecialFunctionsActive();
    const char *state = state_active ? MAIN_SPECIAL_FUNCTION_BUTTON_ACTIVE_TEXT
                                     : MAIN_SPECIAL_FUNCTION_BUTTON_INACTIVE_TEXT;
    uint16_t prefix_px = MAIN_INFO_FONT.width * (uint16_t)strlen(MAIN_SPECIAL_FUNCTION_BUTTON_PREFIX);
    uint16_t state_x = MAIN_INFO_RIGHT_X + prefix_px;
    uint16_t state_w = MAIN_INFO_FONT.width * (uint16_t)strlen(state);

    ST7796_WriteString32(MAIN_INFO_RIGHT_X,
                         row_y,
                         MAIN_SPECIAL_FUNCTION_BUTTON_PREFIX,
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

static void Display_DrawMainInfoRows(const Preset_t *preset)
{
    uint8_t right_first_item = Display_GetMainInfoRightFirstItem();

    for (uint8_t index = 0U; index < MAIN_INFO_ROW_COUNT; ++index)
    {
        uint8_t info_index = (uint8_t)(main_info_first_slot + index);
        uint16_t row_y = main_info_row_y[index];
        ST7796_DrawFilledRectangle(0U, row_y, ST7796_WIDTH, MAIN_INFO_FONT.height, DISPLAY_BG_COLOUR);

        if (info_index < PRESET_DEVICE_SLOTS)
            Display_DrawMainInfoProgramRow(preset, info_index, row_y);
        else
            Display_DrawMainInfoCcRow(preset, (uint8_t)(info_index - PRESET_DEVICE_SLOTS), row_y);

        Display_DrawMainInfoRightRow(preset, (uint8_t)(right_first_item + index), row_y);
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
    ST7796_DrawFilledRectangle(0U, MAIN_FOOTBAR_Y, ST7796_WIDTH, MAIN_FOOTBAR_H, MAIN_FOOTBAR_COLOR);

    Display_DrawFootbarLabel(0U, MAIN_FOOTBAR_LEFT_TEXT);
    Display_DrawFootbarLabel(1U, MAIN_FOOTBAR_CENTER_TEXT);
    Display_DrawFootbarLabel(2U, MAIN_FOOTBAR_RIGHT_TEXT);
    main_layout_dirty = 0U;
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

void Display_PresetEditEnter(void)
{
    preset_edit_mode_active = 1U;
    preset_edit_cursor_index = 0U;
    main_info_first_slot = Display_GetPresetEditScrollFirstSlot(preset_edit_cursor_index);
}

void Display_PresetEditExit(void)
{
    preset_edit_mode_active = 0U;
}

uint8_t Display_PresetEditIsActive(void)
{
    return preset_edit_mode_active;
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
    if (main_layout_dirty)
    {
        ST7796_FillScreen(DISPLAY_BG_COLOUR);
        bpm_display_valid = 0U;
        Display_DrawMainLayout();
    }

    Display_UpdateBPM(bpm);

    Display_WriteCenteredPaddedText32(MAIN_PRESET_TEXT_Y,
                                      p->name,
                                      MAIN_PRESET_TEXT_CHARS,
                                      MAIN_PRESET_FONT,
                                      MAIN_PRESET_COLOUR);
    Display_WriteCenteredPaddedText32(MAIN_BANK_TEXT_Y,
                                      Presets_GetBankName(current_bank),
                                      MAIN_BANK_TEXT_CHARS,
                                      MAIN_BANK_FONT,
                                      MAIN_BANK_COLOUR);
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
    uint32_t now = HAL_GetTick();

    if (!screensaver_active)
    {
        /* Not yet active ??? check if we've been idle long enough */
        if (now - screensaver_last_activity_tick >= SCREENSAVER_TIMEOUT_MS)
        {
            screensaver_active = 1U;
            main_layout_dirty = 0U;
            Display_BL_FadeOut();
        }
        return;
    }

    /* Screensaver is active ??? check for a wake event */
    if (now - screensaver_last_activity_tick < SCREENSAVER_TIMEOUT_MS)
    {
        /* Activity was recorded since we went to sleep ??? wake up */
        //Display_ScreensaverDismiss();
        Display_DrawMainScreen(p, bpm);
        return;
    }
}

