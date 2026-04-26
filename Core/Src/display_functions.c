#include "display_functions.h"
#include "button_functions.h"
#include "midi_functions.h"
#include "midi_devices.h"
#include "st7796.h"
#include "fonts.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>

/* ── display_functions.c ─────────────────────────────────────────────────────
 *
 * All visual output for the ST7796 480×320 TFT display and the DAC backlight.
 *
 * Hardware connections (configured in main.cpp MX_GPIO_Init / MX_SPI1_Init):
 *   SPI1  – display data bus
 *     SCK  = PA5   (SPI1_SCK,  AF5)
 *     MOSI = PA7   (SPI1_MOSI, AF5)
 *   Control pins (push-pull outputs, high speed):
 *     RST  = PF12  (ST7796_RST_Pin  / ST7796_RST_GPIO_Port)
 *     CS   = PD14  (ST7796_CS_Pin   / ST7796_CS_GPIO_Port)
 *     DC   = PD15  (ST7796_DC_Pin   / ST7796_DC_GPIO_Port)
 *   SPI1 baud rate = PCLK2 / 2 = 96 MHz / 2 = 48 MHz
 *     (PCLK2 = SYSCLK / 1 per main.cpp SystemClock_Config APB2 divider)
 *
 *   Backlight – DAC1 CH1 on PA4 (12-bit, 0–4095 → 0–3.3 V → LED driver)
 *     DAC and GPIOA clocks are enabled here in Display_BL_Init because
 *     the backlight must be brought up before ST7796_Init is called.
 *     (GPIOA clock is also enabled by MX_GPIO_Init in main.cpp; enabling
 *     it twice is harmless — the HAL macro is idempotent.)
 *
 * Screen coordinates: origin (0,0) is top-left, x→right, y→down.
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
 * ─────────────────────────────────────────────────────────────────────────── */

/* ── Backlight ───────────────────────────────────────────────────────────── */
/* DAC1 CH1 on PA4.  The DAC is 12-bit (0 = off, 4095 = full brightness).
 *
 * Fade timing:
 *   BL_SPIN_DELAY = 48 000 busy-wait cycles.
 *   At SYSCLK = 96 MHz each cycle ≈ 10.4 ns → 48 000 cycles ≈ 0.5 ms per step.
 *   100 steps × 0.5 ms = ~50 ms total fade duration.
 *
 * The spin loop uses a volatile counter to prevent the compiler from
 * optimising the delay away.
 */

#define BL_STEPS      100U
#define BL_SPIN_DELAY 48000U   /* busy-wait cycles @ 96 MHz ≈ 0.5 ms per step */
#define BL_BRIGHTNESS 2095U      /* max DAC value for full backlight brightness */

/* ── Display_BL_Init ─────────────────────────────────────────────────────────
 * Configures PA4 as an analog output and enables DAC1 channel 1.
 * Must be called before Display_BL_FadeIn / FadeOut.
 * Called early in main.cpp (before ST7796_Init) so the backlight can be
 * kept off while the display initialises, avoiding a white flash.
 * ─────────────────────────────────────────────────────────────────────────── */
void Display_BL_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();  /* PA4 – DAC1_OUT1 (backlight analog output) */
    __HAL_RCC_DAC_CLK_ENABLE();    /* DAC peripheral clock                       */

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin  = GPIO_PIN_4;
    gpio.Mode = GPIO_MODE_ANALOG;  /* analog mode disables the digital driver    */
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    DAC->CR      = DAC_CR_EN1;     /* enable channel 1, no trigger, no buffer    */
    DAC->DHR12R1 = 0U;             /* start with backlight fully off             */
}

/* ── Display_BL_FadeIn ───────────────────────────────────────────────────────
 * Ramps the DAC output from 0 to 4095 over ~50 ms.
 * Blocking – call only from main-loop context, not from an ISR.
 * ─────────────────────────────────────────────────────────────────────────── */
void Display_BL_FadeIn(void)
{
    for (uint32_t step = 0U; step <= BL_STEPS; step++)
    {
        DAC->DHR12R1 = (step * BL_BRIGHTNESS) / BL_STEPS;          /* linear ramp up    */
        for (volatile uint32_t d = 0U; d < BL_SPIN_DELAY; d++) {}
    }
}

/* ── Display_BL_FadeOut ──────────────────────────────────────────────────────
 * Ramps the DAC output from 4095 down to 0 over ~50 ms.
 * The loop counts down using an unsigned counter; the 'if (step==0) break'
 * guard prevents underflow wrap-around (uint32 wrapping to 0xFFFFFFFF).
 * ─────────────────────────────────────────────────────────────────────────── */
void Display_BL_FadeOut(void)
{
    for (uint32_t step = BL_STEPS; ; step--)
    {
        DAC->DHR12R1 = (step * BL_BRIGHTNESS) / BL_STEPS;          /* linear ramp down  */
        for (volatile uint32_t d = 0U; d < BL_SPIN_DELAY; d++) {}
        if (step == 0U) break;                              /* avoid uint underflow */
    }
}

/* ── Screen layout constants ─────────────────────────────────────────────── */
#define MAIN_FOOTBAR_Y       298U                        /* top of footer bar    */
#define MAIN_FOOTBAR_H       (ST7796_HEIGHT - MAIN_FOOTBAR_Y)  /* = 22 px        */
#define MAIN_FOOTBAR_COLOR   ST7796_DARKGRAY
#define MAIN_INFO_LEFT_X      30U                        /* left column x origin  */
#define MAIN_INFO_RIGHT_X    220U                        /* right column x origin */
#define MAIN_FOOTBAR_TEXT    "MIDI / RELAY STATUS"
#define BPM_FONT            Font_Consolas15x35
#define BPM_TEXT_Y          7U
#define BPM_INTERNAL_X      365U

/* Set to 1 whenever the static elements (footer bar) need to be redrawn –
 * e.g. after the screensaver has painted over them. */
static uint8_t main_layout_dirty = 1U;
static uint8_t vita_state_valid = 0U;
static uint8_t vita_state_active = 0U;
static uint8_t bpm_display_valid = 0U;
static uint8_t bpm_display_external = 0U;
static uint8_t bpm_display_sync_lost = 0U;
static uint16_t bpm_display_value_x10 = 0U;
static uint32_t bpm_display_external_update_tick = 0U;
static char bpm_display_internal_text[8] = "";
static char bpm_display_external_text[14] = "";

#define BPM_DISPLAY_AREA_X              280U
#define BPM_DISPLAY_AREA_W              200U
#define BPM_SYNC_LOST_X                 280U
#define BPM_INTERNAL_VALUE_X            365U
#define BPM_INTERNAL_VALUE_W            (BPM_FONT.width * 3U)
#define BPM_INTERNAL_SUFFIX_X           (BPM_INTERNAL_VALUE_X + BPM_INTERNAL_VALUE_W)
#define BPM_INTERNAL_COLOUR             ST7796_DARKGREEN
#define BPM_EXT_PREFIX_X                305U
#define BPM_EXT_VALUE_X                 365U
#define BPM_EXT_VALUE_W                 (BPM_FONT.width * 3U)
#define BPM_EXT_SUFFIX_X                (BPM_EXT_VALUE_X + BPM_EXT_VALUE_W)
#define BPM_EXT_HYSTERESIS_MIN_X10      1U
#define BPM_EXT_HYSTERESIS_BPS          20U
#define BPM_EXT_UPDATE_MIN_INTERVAL_MS  500U
#define BPM_EXT_FORCE_UPDATE_DELTA_X10  5U
#define BPM_EXT_SLEW_STEP_X10           1U
#define BPM_INTERNAL_TEXT_CHARS         7U
#define BPM_EXT_TEXT_CHARS              13U
#define BPM_EXT_TEXT_X                  ((uint16_t)(BPM_DISPLAY_AREA_X + BPM_DISPLAY_AREA_W - (BPM_EXT_TEXT_CHARS * BPM_FONT.width)))
#define EXT_BPM_COLOUR                  ST7796_COBALTBLUE

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
        ST7796_DrawFilledRectangle(char_x, y, BPM_FONT.width, BPM_FONT.height, ST7796_BLACK);
        if (new_ch != ' ')
        {
            ST7796_WriteChar32(char_x, y, new_ch, BPM_FONT, colour, ST7796_BLACK);
        }
    }
}

static void Display_FormatExternalBpmText(char *buffer, size_t buffer_size, uint16_t bpm_x10)
{
    char text[20];

    snprintf(text, sizeof(text), "EXT %u.%u BPM",
             (unsigned)(bpm_x10 / 10U),
             (unsigned)(bpm_x10 % 10U));

    snprintf(buffer, buffer_size, "%*s", (int)BPM_EXT_TEXT_CHARS, text);
}

static uint16_t Display_GetExternalBpmHysteresisX10(uint16_t reference_bpm_x10)
{
    uint32_t hysteresis_x10 = (((uint32_t)reference_bpm_x10 * BPM_EXT_HYSTERESIS_BPS) + 5000U) / 10000U;

    if (hysteresis_x10 < BPM_EXT_HYSTERESIS_MIN_X10)
        hysteresis_x10 = BPM_EXT_HYSTERESIS_MIN_X10;

    return (uint16_t)hysteresis_x10;
}

/* ── Display_DrawMainLayout ──────────────────────────────────────────────────
 * Draws the parts of the screen that don't change between presets:
 *   • Dark-grey footer bar at the bottom.
 *   • Centred "MIDI / RELAY STATUS" label inside the bar.
 * Called automatically by Display_DrawMainScreen when main_layout_dirty is set.
 * ─────────────────────────────────────────────────────────────────────────── */
static void Display_DrawMainLayout(void)
{
    ST7796_DrawFilledRectangle(0U, MAIN_FOOTBAR_Y, ST7796_WIDTH, MAIN_FOOTBAR_H, MAIN_FOOTBAR_COLOR);

    /* Centre the label: total pixel width = number_of_chars × font_char_width */
    ST7796_WriteString((uint16_t)((ST7796_WIDTH - ((sizeof(MAIN_FOOTBAR_TEXT) - 1U) * Font_11x18.width)) / 2U),
                       (uint16_t)(MAIN_FOOTBAR_Y + ((MAIN_FOOTBAR_H - Font_11x18.height) / 2U)),
                       MAIN_FOOTBAR_TEXT,
                       Font_11x18,
                       ST7796_LIGHTGRAY,
                       MAIN_FOOTBAR_COLOR);
    main_layout_dirty = 0U;
}

/* ── Display_DrawMainScreen ──────────────────────────────────────────────────
 * Full refresh of the main screen for a given preset + BPM value.
 * Called by App_ActivatePreset() in presets.c and by the screensaver wakeup.
 *
 * Sections drawn:
 *   1. Footer bar (only if dirty — avoids a needless SPI burst every call).
 *   2. BPM display (top-right).
 *   3. Preset name (centred, padded to exactly 20 characters so the previous
 *      name is fully overwritten even if it was longer).
 *   4. Three info rows: one per device slot.
 *        Left  column: MIDI channel + program number sent to that device.
 *        Right column: relay state for the first two rows.
 *      program == 0xFF means that slot is unused — shown as "CH -: ---".
 * ─────────────────────────────────────────────────────────────────────────── */
void Display_DrawMainScreen(const Preset_t *p, uint16_t bpm)
{
    char buf[32];

    if (main_layout_dirty)
    {
        ST7796_FillScreen(ST7796_BLACK);
        bpm_display_valid = 0U;
        Display_DrawMainLayout();
    }

    Display_UpdateBPM(bpm);

    /* Preset name – padded to exactly 20 chars so the old name is always
     * fully erased (the font background colour fills unused pixels in each
     * character cell, so no separate erase rectangle is needed). */
    {
        char    padded[21];
        uint8_t len   = (uint8_t)strnlen(p->name, 20U);
        uint8_t pad_l = (uint8_t)((20U - len) / 2U);   /* left padding to centre */
        uint8_t pad_r = (uint8_t)(20U - len - pad_l);   /* right padding          */
        memset(padded,               ' ', pad_l);
        memcpy(padded + pad_l,       p->name, len);
        memset(padded + pad_l + len, ' ', pad_r);
        padded[20] = '\0';
        ST7796_WriteString32(10U, 85U, padded, Font_Consolas23x49, ST7796_WHITE, ST7796_BLACK);
    }

    {
        const char *bank_name = Presets_GetBankName(current_bank);
        char padded_bank[PRESET_BANK_NAME_MAXLEN + 1U];
        uint8_t bank_len = (uint8_t)strnlen(bank_name, PRESET_BANK_NAME_MAXLEN);
        uint8_t pad_l = (uint8_t)((PRESET_BANK_NAME_MAXLEN - bank_len) / 2U);
        uint8_t pad_r = (uint8_t)(PRESET_BANK_NAME_MAXLEN - bank_len - pad_l);
        memset(padded_bank, ' ', pad_l);
        memcpy(padded_bank + pad_l, bank_name, bank_len);
        memset(padded_bank + pad_l + bank_len, ' ', pad_r);
        padded_bank[PRESET_BANK_NAME_MAXLEN] = '\0';
        ST7796_WriteString32((uint16_t)((ST7796_WIDTH - (PRESET_BANK_NAME_MAXLEN * Font_Consolas15x35.width)) / 2U),
                             145U,
                             padded_bank,
                             Font_Consolas15x35,
                             ST7796_DARKGRAY,
                             ST7796_BLACK);
    }

    /* Three device-info rows, one per preset slot (Echosystem, Reverb, spare).
     * row_y values are chosen so the 35-px-tall font rows sit tightly inside
     * the 184–291 px band without overlapping. */
    static const uint16_t row_y[3] = {184U, 220U, 256U};
    for (uint8_t i = 0U; i < PRESET_DEVICE_SLOTS; i++)
    {
        const MidiDevice_t *dev = MidiDevices_Get(i);
        uint8_t program = p->prg[i].program;
        if (program != 0xFFU) {
            // Format: "CH n: ppp" (ppp = program number, always 3 chars)
            snprintf(buf, sizeof(buf), "CH %u: %3u", dev->channel, program);
            // Find where the program number starts in the string
            char *prog_ptr = buf + strlen(buf) - 3;
            // If duplicate, draw the number region with black-on-darkgray
            if (Presets_DeviceProgramIsShared(i, program)) {
                // Draw the prefix ("CH n: ") as usual
                char prefix[16];
                size_t prefix_len = prog_ptr - buf;
                strncpy(prefix, buf, prefix_len);
                prefix[prefix_len] = '\0';
                ST7796_WriteString32(MAIN_INFO_LEFT_X, row_y[i], prefix, Font_Consolas15x35, ST7796_DARKGRAY, ST7796_BLACK);
                // Draw the number with black text on dark gray background, offset by prefix width
                uint16_t prefix_px = Font_Consolas15x35.width * (uint16_t)prefix_len;
                ST7796_WriteString32(MAIN_INFO_LEFT_X + prefix_px, row_y[i], prog_ptr, Font_Consolas15x35, ST7796_BLACK, ST7796_DARKGRAY);
            } else {
                // Normal: all darkgray on black
                ST7796_WriteString32(MAIN_INFO_LEFT_X, row_y[i], buf, Font_Consolas15x35, ST7796_DARKGRAY, ST7796_BLACK);
            }
        } else {
            snprintf(buf, sizeof(buf), "CH -: ---");
            ST7796_WriteString32(MAIN_INFO_LEFT_X, row_y[i], buf, Font_Consolas15x35, ST7796_DARKGRAY, ST7796_BLACK);
        }

        if (i < PRESET_RELAY_COUNT) {
            snprintf(buf, sizeof(buf), "Relay_%u: %-6s", i + 1U,
                     p->relay[i] ? "closed" : "open");
            ST7796_WriteString32(MAIN_INFO_RIGHT_X, row_y[i], buf, Font_Consolas15x35, ST7796_DARKGRAY, ST7796_BLACK);
        } else if (i == PRESET_RELAY_COUNT) {
            const char *prefix = "Vita: ";
            uint8_t state_active = Button_SpecialFunctionsActive();
            const char *state = state_active ? "undead" : "dead";
            uint16_t prefix_px = Font_Consolas15x35.width * (uint16_t)strlen(prefix);
            uint16_t state_x = MAIN_INFO_RIGHT_X + prefix_px;
            uint16_t state_w = Font_Consolas15x35.width * (uint16_t)strlen(state);

            ST7796_WriteString32(MAIN_INFO_RIGHT_X, row_y[i], prefix, Font_Consolas15x35, ST7796_DARKGRAY, ST7796_BLACK);
            if (vita_state_valid && vita_state_active && !state_active) {
                ST7796_DrawFilledRectangle(state_x + state_w, row_y[i],
                                           Font_Consolas15x35.width * 2U,
                                           Font_Consolas15x35.height,
                                           ST7796_BLACK);
            }
            ST7796_WriteString32(state_x, row_y[i], state,
                                 Font_Consolas15x35,
                                 state_active ? ST7796_WHITE : ST7796_DARKGRAY,
                                 state_active ? ST7796_DARKRED : ST7796_BLACK);
            if (state_active) {
                ST7796_DrawFilledRectangle(state_x, row_y[i], state_w, 2U, ST7796_BLACK);
                ST7796_DrawFilledRectangle(state_x, row_y[i] + Font_Consolas15x35.height - 2U,
                                           state_w, 2U, ST7796_BLACK);
            }
            vita_state_valid = 1U;
            vita_state_active = state_active;
        } else {
            snprintf(buf, sizeof(buf), "                ");
            ST7796_WriteString32(MAIN_INFO_RIGHT_X, row_y[i], buf, Font_Consolas15x35, ST7796_DARKGRAY, ST7796_BLACK);
        }
    }
}

/* ── Display_UpdateBPM ───────────────────────────────────────────────────────
 * Redraws only the BPM value in the top-right corner.
 * Called both from Display_DrawMainScreen and from Handle_Tap_Tempo()
 * (bpm_functions.c) on every tap so the number updates immediately without
 * redrawing the whole screen.
 * ─────────────────────────────────────────────────────────────────────────── */
void Display_UpdateBPM(uint16_t bpm)
{
    uint16_t display_bpm_x10 = (uint16_t)(bpm * 10U);
    uint8_t use_external = MidiClockGetExternalBpmX10(&display_bpm_x10);
    uint8_t sync_lost = MidiClockIsSyncLost();
    uint8_t was_sync_lost = bpm_display_sync_lost;
    uint32_t now_ms = HAL_GetTick();
    uint8_t full_redraw;
    char buf[20];

    if (sync_lost)
    {
        if (bpm_display_valid && bpm_display_sync_lost)
        {
            return;
        }

        ST7796_DrawFilledRectangle(BPM_DISPLAY_AREA_X, BPM_TEXT_Y, BPM_DISPLAY_AREA_W, BPM_FONT.height, ST7796_BLACK);
        ST7796_WriteString32(BPM_SYNC_LOST_X, BPM_TEXT_Y, "EXT SYNC LOST", BPM_FONT, ST7796_RED, ST7796_BLACK);

        bpm_display_valid = 1U;
        bpm_display_external = 0U;
        bpm_display_sync_lost = 1U;
        bpm_display_value_x10 = 0U;
        bpm_display_external_update_tick = 0U;
        bpm_display_internal_text[0] = '\0';
        bpm_display_external_text[0] = '\0';
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
            ST7796_DrawFilledRectangle(BPM_DISPLAY_AREA_X, BPM_TEXT_Y, BPM_DISPLAY_AREA_W, BPM_FONT.height, ST7796_BLACK);
        }

        snprintf(buf, sizeof(buf), "%3u BPM", (unsigned)bpm);
        Display_UpdateBpmTextCells(BPM_INTERNAL_VALUE_X,
                                   BPM_TEXT_Y,
                                   BPM_INTERNAL_TEXT_CHARS,
                                   full_redraw ? "" : bpm_display_internal_text,
                                   buf,
                                   BPM_INTERNAL_COLOUR);
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
        ST7796_DrawFilledRectangle(BPM_DISPLAY_AREA_X, BPM_TEXT_Y, BPM_DISPLAY_AREA_W, BPM_FONT.height, ST7796_BLACK);
    }
    else
    {
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
        if (delta_x10 < BPM_EXT_FORCE_UPDATE_DELTA_X10
         && (now_ms - bpm_display_external_update_tick) < BPM_EXT_UPDATE_MIN_INTERVAL_MS)
        {
            return;
        }

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

    Display_FormatExternalBpmText(buf, sizeof(buf), display_bpm_x10);
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

/* ── Loading bar ─────────────────────────────────────────────────────────────
 * Draws a progress bar that fills left-to-right over duration_ms milliseconds.
 * This is a blocking call — it does not return until the timer expires.
 * Used during startup while the system waits for devices to power up.
 *
 * The bar is split into three text phases to keep the user entertained:
 *   0 %–33 % : "... waiting for DNA match"
 *   33%      : "DNA match found"         (shown for 1 s)
 *   33%+1 s  : "..accessing genetic markers"  (shown for 1 s)
 *   ~66%+    : text cleared
 *
 * Only the newly filled strip is drawn each iteration (fill > prev_fill),
 * so SPI traffic is proportional to progress not to loop frequency.
 */

#define LB_X        10U    /* left margin (10 px from screen edge)  */
#define LB_Y       262U    /* top of bar, lower quarter of screen   */
#define LB_W       460U    /* total bar width (480 - 10 left - 10 right) */
#define LB_H        28U    /* bar height in pixels                  */
#define LB_COLOR  ST7796_DARKRED  /* dark red in RGB565 format             */

/* Text row sits just above the bar */
#define LB_TXT_Y    246U
/* Right-align text: start x = screen_width - (chars × char_width) - margin */
#define LB_TXT_X(chars)  ((uint16_t)(480U - (uint16_t)(chars) * 7U - 10U))

/* Erase the full text row then write a new message right-aligned. */
static void lb_set_text(const char *txt, uint8_t len, uint16_t color)
{
    ST7796_DrawFilledRectangle(0U, LB_TXT_Y, 480U, 10U, ST7796_BLACK);  /* clear row */
    ST7796_WriteString(LB_TXT_X(len), LB_TXT_Y, txt, Font_7x10, color, ST7796_BLACK);
}

void Display_LoadingBar(uint32_t duration_ms)
{
    lb_set_text("... waiting for DNA match", 25, ST7796_WHITE);

    ST7796_DrawFilledRectangle(LB_X, LB_Y, LB_W, LB_H, ST7796_BLACK);  /* empty bar */

    uint32_t start     = HAL_GetTick();
    uint16_t prev_fill = 0U;   /* tracks how many pixels have been filled so far */
    uint8_t  phase     = 0U;   /* which text message is currently showing        */
    uint32_t phase_ts  = 0U;   /* HAL tick when the current phase started        */

    for (;;)
    {
        uint32_t elapsed = HAL_GetTick() - start;
        if (elapsed >= duration_ms) elapsed = duration_ms;  /* clamp at end */

        /* Fill only the new strip since last iteration — avoids redrawing
         * pixels that are already the correct colour. */
        uint16_t fill = (uint16_t)((elapsed * LB_W) / duration_ms);
        if (fill > prev_fill)
        {
            ST7796_DrawFilledRectangle(LB_X + prev_fill, LB_Y,
                                       fill - prev_fill, LB_H, LB_COLOR);
            prev_fill = fill;
        }

        uint32_t now = HAL_GetTick();

        /* Phase transitions — check sequentially so they can't be skipped */
        if (phase == 0U && elapsed >= duration_ms / 3U)
        {
            lb_set_text("DNA match found", 15, ST7796_WHITE);
            phase    = 1U;
            phase_ts = now;
        }
        if (phase == 1U && now - phase_ts >= 1000U)
        {
            lb_set_text("..accessing genetic markers", 27, ST7796_WHITE);
            phase    = 2U;
            phase_ts = now;
        }
        if (phase == 2U && now - phase_ts >= 1000U)
        {
            ST7796_DrawFilledRectangle(0U, LB_TXT_Y, 480U, 10U, ST7796_BLACK);
            phase = 3U;  /* text cleared — stay here until bar finishes */
        }

        if (elapsed >= duration_ms) break;
    }
}

/* ── Display_LoadingBarClear ─────────────────────────────────────────────────
 * Clears the loading bar and shows a brief "mutation complete" message.
 * Called after Display_LoadingBar() returns, just before the fade-out.
 * ─────────────────────────────────────────────────────────────────────────── */
void Display_LoadingBarClear(void)
{
    ST7796_DrawFilledRectangle(LB_X, LB_Y, LB_W, LB_H, ST7796_BLACK);
    lb_set_text("mutation complete", 17, ST7796_WHITE);
    HAL_Delay(1000U);  /* leave message visible for 1 s before fade */
    ST7796_DrawFilledRectangle(0U, LB_TXT_Y, 480U, 10U, ST7796_BLACK);
}

/* ── Screensaver ─────────────────────────────────────────────────────────────
 * Idle mode is handled by fading the display backlight out after a period of
 * inactivity, then fading it back in on the next activity event.
 *
 * Activation: triggers after SS_TIMEOUT_MS of inactivity.
 *   Any call to Display_ScreensaverActivity() resets the inactivity timer.
 *
 * Deactivation: the first Display_ScreensaverUpdate() call after
 *   ss_last_activity has been refreshed redraws the main screen and exits.
 */

#define SS_TIMEOUT_MS   (10UL * 60UL * 1000UL)  /* 10 minutes of inactivity */ 
//#define SS_TIMEOUT_MS   (5000UL)  /* 5 second of inactivity */ 

static uint32_t  ss_last_activity = 0U;   /* tick of last user interaction */
static uint8_t   ss_active        = 0U;   /* 1 while screensaver is running */

/* ── Display_ScreensaverActivity ─────────────────────────────────────────────
 * Records the current tick as the last user activity.
 * Call this from any event that should reset the screensaver timer:
 *   button presses (button_functions.c), tap tempo (main.cpp EXTI callback),
 *   preset changes (presets.c App_ActivatePreset).
 * ─────────────────────────────────────────────────────────────────────────── */
void Display_ScreensaverActivity(void)
{
    ss_last_activity = HAL_GetTick();
}

uint8_t Display_ScreensaverIsActive(void)
{
    return ss_active;
}

void Display_ScreensaverDismiss(void)
{
    if (!ss_active)
        return;

    ss_active = 0U;
    main_layout_dirty = 1U;
    Display_BL_FadeIn();
}

/* ── Display_ScreensaverUpdate ───────────────────────────────────────────────
 * Called from the main while(1) loop every iteration.
 * When inactive: checks if timeout has elapsed and fades the backlight out.
 * When active:   waits for activity and restores the display on wake.
 * ─────────────────────────────────────────────────────────────────────────── */
void Display_ScreensaverUpdate(const Preset_t *p, uint16_t bpm)
{
    uint32_t now = HAL_GetTick();

    if (!ss_active)
    {
        /* Not yet active — check if we've been idle long enough */
        if (now - ss_last_activity >= SS_TIMEOUT_MS)
        {
            ss_active    = 1U;
            main_layout_dirty = 1U;
            Display_BL_FadeOut();
        }
        return;
    }

    /* Screensaver is active — check for a wake event */
    if (now - ss_last_activity < SS_TIMEOUT_MS)
    {
        /* Activity was recorded since we went to sleep — wake up */
        Display_ScreensaverDismiss();
        Display_DrawMainScreen(p, bpm);
        return;
    }
}
