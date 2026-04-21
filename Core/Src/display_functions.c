#include "display_functions.h"
#include "umbrella_image.h"
#include "midi_devices.h"
#include "st7796.h"
#include "fonts.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>

/* ── Backlight ───────────────────────────────────────────────────────────── */
/* DAC1 CH1 on PA4, 12-bit (0..4095).
 * 100 fade steps × 48000-cycle spin ≈ 0.5 ms each → ~50 ms total.          */

#define BL_STEPS      100U
#define BL_SPIN_DELAY 48000U   /* cycles @ 96 MHz ≈ 0.5 ms */

void Display_BL_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_DAC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin  = GPIO_PIN_4;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    DAC->CR      = DAC_CR_EN1;
    DAC->DHR12R1 = 0U;   /* start with backlight off */
}

void Display_BL_FadeIn(void)
{
    for (uint32_t step = 0U; step <= BL_STEPS; step++)
    {
        DAC->DHR12R1 = (step * 4095U) / BL_STEPS;
        for (volatile uint32_t d = 0U; d < BL_SPIN_DELAY; d++) {}
    }
}

void Display_BL_FadeOut(void)
{
    for (uint32_t step = BL_STEPS; ; step--)
    {
        DAC->DHR12R1 = (step * 4095U) / BL_STEPS;
        for (volatile uint32_t d = 0U; d < BL_SPIN_DELAY; d++) {}
        if (step == 0U) break;
    }
}

/* ── Screen layout (480 × 320 px, landscape) ─────────────────────────────── */
/*
 *   y=  0 ..  34  :  BPM value, Font_Consolas15x35, right-aligned
 *   y= 85 .. 133  :  Preset name, Font_Consolas23x49, centred (padded to 20)
 *   y=184 .. 291  :  3 tightly-spaced info rows, Font_Consolas15x35
 *                      left  (x= 30) : CH1:   5
 *                      right (x=220) : Relay_1: open/closed
 *   y=298 .. 319  :  thin grey foot bar
 */

#define MAIN_FOOTBAR_Y       298U
#define MAIN_FOOTBAR_H       (ST7796_HEIGHT - MAIN_FOOTBAR_Y)
#define MAIN_FOOTBAR_COLOR   ST7796_DARKGRAY
#define MAIN_INFO_LEFT_X      30U
#define MAIN_INFO_RIGHT_X    220U
#define MAIN_FOOTBAR_TEXT    "MIDI / RELAY STATUS"

static uint8_t main_layout_dirty = 1U;

static void Display_DrawMainLayout(void)
{
    ST7796_DrawFilledRectangle(0U, MAIN_FOOTBAR_Y, ST7796_WIDTH, MAIN_FOOTBAR_H, MAIN_FOOTBAR_COLOR);
    ST7796_WriteString((uint16_t)((ST7796_WIDTH - ((sizeof(MAIN_FOOTBAR_TEXT) - 1U) * Font_11x18.width)) / 2U),
                       (uint16_t)(MAIN_FOOTBAR_Y + ((MAIN_FOOTBAR_H - Font_11x18.height) / 2U)),
                       MAIN_FOOTBAR_TEXT,
                       Font_11x18,
                       ST7796_LIGHTGRAY,
                       MAIN_FOOTBAR_COLOR);
    main_layout_dirty = 0U;
}

void Display_DrawMainScreen(const Preset_t *p, uint16_t bpm)
{
    char buf[32];

    if (main_layout_dirty)
        Display_DrawMainLayout();

    Display_UpdateBPM(bpm);

    /* Preset name – centred, padded to exactly 20 chars */
    {
        char    padded[21];
        uint8_t len   = (uint8_t)strnlen(p->name, 20U);
        uint8_t pad_l = (uint8_t)((20U - len) / 2U);
        uint8_t pad_r = (uint8_t)(20U - len - pad_l);
        memset(padded,               ' ', pad_l);
        memcpy(padded + pad_l,       p->name, len);
        memset(padded + pad_l + len, ' ', pad_r);
        padded[20] = '\0';
        ST7796_WriteString32(10U, 85U, padded, Font_Consolas23x49, ST7796_WHITE, ST7796_BLACK);
    }

    /* Info rows */
    static const uint16_t row_y[3] = {184U, 220U, 256U};
    for (uint8_t i = 0U; i < PRESET_DEVICE_SLOTS; i++)
    {
        const MidiDevice_t *dev = MidiDevices_Get(i);
        if (p->dev[i].program != 0xFFU)
            snprintf(buf, sizeof(buf), "CH %u: %3u", dev->channel, p->dev[i].program);
        else
            snprintf(buf, sizeof(buf), "CH -: ---");
        ST7796_WriteString32(MAIN_INFO_LEFT_X, row_y[i], buf, Font_Consolas15x35, ST7796_WHITE, ST7796_BLACK);

        snprintf(buf, sizeof(buf), "Relay_%u: %s", i + 1U,
                 p->relay[i] ? "closed" : "open");
        ST7796_WriteString32(MAIN_INFO_RIGHT_X, row_y[i], buf, Font_Consolas15x35, ST7796_WHITE, ST7796_BLACK);
    }
}

void Display_UpdateBPM(uint16_t bpm)
{
    char buf[10];
    snprintf(buf, sizeof(buf), "%3u BPM", (unsigned)bpm);
    ST7796_WriteString32(365U, 7U, buf, Font_Consolas15x35, ST7796_DARKGRAY, ST7796_BLACK);
}

/* ── Loading bar ────────────────────────────────────────────────────────────
 * Draws a progress bar in the lower quarter of the screen and blocks for
 * duration_ms, filling it left-to-right over that time.                     */

#define LB_X        10U    /* left margin (10px from edge) */
#define LB_Y       262U    /* top of bar (lower quarter)   */
#define LB_W       460U    /* total bar width (480-10-10)  */
#define LB_H        28U    /* bar height                   */
#define LB_COLOR  0x000EU  /* dark red BGR565              */

/* text row above the bar — right-aligned, 7px char width */
#define LB_TXT_Y    246U
#define LB_TXT_X(chars)  ((uint16_t)(480U - (uint16_t)(chars) * 7U - 10U))

static void lb_set_text(const char *txt, uint8_t len, uint16_t color)
{
    /* Erase the full text row first, then write new message */
    ST7796_DrawFilledRectangle(0U, LB_TXT_Y, 480U, 10U, ST7796_BLACK);
    ST7796_WriteString(LB_TXT_X(len), LB_TXT_Y, txt, Font_7x10, color, ST7796_BLACK);
}

void Display_LoadingBar(uint32_t duration_ms)
{
    lb_set_text("... waiting for DNA match", 25, ST7796_WHITE);

    /* Black background for bar */
    ST7796_DrawFilledRectangle(LB_X, LB_Y, LB_W, LB_H, ST7796_BLACK);

    uint32_t start     = HAL_GetTick();
    uint16_t prev_fill = 0U;
    uint8_t  phase     = 0U;
    uint32_t phase_ts  = 0U;

    for (;;)
    {
        uint32_t elapsed = HAL_GetTick() - start;
        if (elapsed >= duration_ms) elapsed = duration_ms;

        uint16_t fill = (uint16_t)((elapsed * LB_W) / duration_ms);
        if (fill > prev_fill)
        {
            ST7796_DrawFilledRectangle(LB_X + prev_fill, LB_Y,
                                       fill - prev_fill, LB_H, LB_COLOR);
            prev_fill = fill;
        }

        uint32_t now = HAL_GetTick();

        /* ~33% — show "DNA match found" in white */
        if (phase == 0U && elapsed >= duration_ms / 3U)
        {
            lb_set_text("DNA match found", 15, ST7796_WHITE);
            phase    = 1U;
            phase_ts = now;
        }
        /* after 1000 ms — show "..accessing genetic markers" */
        if (phase == 1U && now - phase_ts >= 1000U)
        {
            lb_set_text("..accessing genetic markers", 27, ST7796_WHITE);
            phase    = 2U;
            phase_ts = now;
        }
        /* after another 1000 ms — clear text */
        if (phase == 2U && now - phase_ts >= 1000U)
        {
            ST7796_DrawFilledRectangle(0U, LB_TXT_Y, 480U, 10U, ST7796_BLACK);
            phase = 3U;
        }

        if (elapsed >= duration_ms) break;
    }

}

void Display_LoadingBarClear(void)
{
    ST7796_DrawFilledRectangle(LB_X, LB_Y, LB_W, LB_H, ST7796_BLACK);
    lb_set_text("mutation complete", 17, ST7796_WHITE);
    HAL_Delay(1000U);
    ST7796_DrawFilledRectangle(0U, LB_TXT_Y, 480U, 10U, ST7796_BLACK);
}

/* ── Screensaver ─────────────────────────────────────────────────────────────
 * DVD-style bouncing sprite.  Changes colour on every wall bounce.
 * Flicker-free: only the strips vacated by the sprite each step are erased,
 * then the sprite is drawn at its new position — no full-box erase.
 * Swap ST7796_DrawFilledRectangle for ST7796_DrawImage once image is ready.
 */

#define SS_TIMEOUT_MS   (10UL * 60UL * 1000UL)  /* 10 minutes */
#define SS_BOX_W        UMBRELLA_W       /* sprite size           */
#define SS_BOX_H        UMBRELLA_H
#define SS_STEP_MS      40U              /* move every 40 ms      */
#define SS_VX            6               /* pixels per step       */
#define SS_VY            4

static uint32_t  ss_last_activity = 0U;
static uint8_t   ss_active        = 0U;
static uint32_t  ss_last_move     = 0U;
static int16_t   ss_x             = 0;
static int16_t   ss_y             = 0;
static int8_t    ss_vx            = SS_VX;
static int8_t    ss_vy            = SS_VY;

void Display_ScreensaverActivity(void)
{
    ss_last_activity = HAL_GetTick();
}

void Display_ScreensaverUpdate(const Preset_t *p, uint16_t bpm)
{
    uint32_t now = HAL_GetTick();

    if (!ss_active)
    {
        if (now - ss_last_activity >= SS_TIMEOUT_MS)
        {
            ss_active    = 1U;
            ss_x         = (ST7796_WIDTH  - SS_BOX_W) / 2;
            ss_y         = (ST7796_HEIGHT - SS_BOX_H) / 2;
            ss_vx        = SS_VX;
            ss_vy        = SS_VY;
            ss_last_move = now;
            main_layout_dirty = 1U;
            ST7796_FillScreen(ST7796_BLACK);
            ST7796_DrawImage((uint16_t)ss_x, (uint16_t)ss_y,
                             SS_BOX_W, SS_BOX_H, umbrella_data);
        }
        return;
    }

    /* Wake on activity */
    if (now - ss_last_activity < SS_TIMEOUT_MS)
    {
        ss_active = 0U;
        main_layout_dirty = 1U;
        ST7796_FillScreen(ST7796_BLACK);
        Display_DrawMainScreen(p, bpm);
        return;
    }

    if (now - ss_last_move < SS_STEP_MS) return;
    ss_last_move = now;

    int16_t old_x = ss_x;
    int16_t old_y = ss_y;

    ss_x += ss_vx;
    ss_y += ss_vy;

    /* Bounce — change colour on each wall hit */
    uint8_t bounced = 0U;
    if (ss_x <= 0)                                    { ss_x = 0;                               ss_vx = -ss_vx; bounced = 1U; }
    if (ss_x + (int16_t)SS_BOX_W >= ST7796_WIDTH)    { ss_x = ST7796_WIDTH  - (int16_t)SS_BOX_W; ss_vx = -ss_vx; bounced = 1U; }
    if (ss_y <= 0)                                    { ss_y = 0;                               ss_vy = -ss_vy; bounced = 1U; }
    if (ss_y + (int16_t)SS_BOX_H >= ST7796_HEIGHT)   { ss_y = ST7796_HEIGHT - (int16_t)SS_BOX_H; ss_vy = -ss_vy; bounced = 1U; }

    (void)bounced;

    /* Partial erase: only wipe the thin strip the sprite moved away from.
     * This removes the flicker caused by a full erase before redraw.        */
    int16_t dx = ss_x - old_x;
    int16_t dy = ss_y - old_y;

    if (dx > 0)  /* moved right – erase left strip */
        ST7796_DrawFilledRectangle((uint16_t)old_x, (uint16_t)ss_y, (uint16_t)dx, SS_BOX_H, ST7796_BLACK);
    else if (dx < 0)  /* moved left – erase right strip */
        ST7796_DrawFilledRectangle((uint16_t)(ss_x + SS_BOX_W), (uint16_t)ss_y, (uint16_t)(-dx), SS_BOX_H, ST7796_BLACK);

    if (dy > 0)  /* moved down – erase top strip */
        ST7796_DrawFilledRectangle((uint16_t)old_x, (uint16_t)old_y, SS_BOX_W, (uint16_t)dy, ST7796_BLACK);
    else if (dy < 0)  /* moved up – erase bottom strip */
        ST7796_DrawFilledRectangle((uint16_t)old_x, (uint16_t)(ss_y + SS_BOX_H), SS_BOX_W, (uint16_t)(-dy), ST7796_BLACK);

    /* Draw sprite at new position */
    ST7796_DrawImage((uint16_t)ss_x, (uint16_t)ss_y,
                     SS_BOX_W, SS_BOX_H, umbrella_data);
}
