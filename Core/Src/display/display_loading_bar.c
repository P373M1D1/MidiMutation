#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "display_functions.h"
#include "display/display_internal.h"
#include "display/display_layout.h"
#include "stm32f4xx_hal.h"
#include "st7796.h"

/* Startup loading bar shown during boot while peripherals settle.
 *
 * Colours here are intentionally fixed and do not follow runtime themes, so a
 * later theme edit cannot accidentally make the boot splash unreadable. The
 * implementation is still blocking because boot flow currently expects a simple
 * splash-delay stage before the main UI comes up. */

#define LOADING_SCREEN_BAR_COLOUR  WHITE
#define LOADING_SCREEN_TEXT_COLOUR WHITE

typedef struct
{
    const char *text;
} LoadingStartupLine_t;

/* Startup loading messages are stored as one struct per line so adding another
 * option only requires appending one entry to this table. */
static const LoadingStartupLine_t loading_startup_lines[] = {
    { "Tightening Orion's Belt" },
    { "Emptying the Big Dipper" },
    { "Locating Southern Cross" },
    { "Aligning Cassiopeia" },
    { "Counting Perseid Meteors" },
    { "Polishing Polaris" },
    { "Untangling Andromeda" },
    { "Charting Lyra" },
    { "Calibrating Nebula Drift" },
    { "Checking Saturn's Rings" },
};

#define LOADING_STARTUP_LINE_COUNT ((uint32_t)(sizeof(loading_startup_lines) / sizeof(loading_startup_lines[0])))

static const char *Display_LoadingBarSelectStartupLine(void)
{
    uint32_t seed = HAL_GetTick() ^ SysTick->VAL ^ (uint32_t)(uintptr_t)&loading_startup_lines[0];
    uint32_t index = (LOADING_STARTUP_LINE_COUNT > 0U) ? (seed % LOADING_STARTUP_LINE_COUNT) : 0U;

    return loading_startup_lines[index].text;
}

static void Display_LoadingBarSetText(const char *text, uint16_t colour)
{
    if (!text)
        return;

    size_t text_len = strlen(text);
    uint16_t text_w = (uint16_t)(text_len * LOADING_BAR_TEXT_FONT.width);
    uint16_t text_margin = (uint16_t)(LOADING_BAR_TEXT_FONT.width * 2U);
    uint16_t text_x = (text_w + text_margin < ST7796_WIDTH)
                          ? (uint16_t)(ST7796_WIDTH - text_w - text_margin)
                          : 0U;

    ST7796_WriteStringTransparent(text_x,
                                  LOADING_BAR_TEXT_Y,
                                  text,
                                  LOADING_BAR_TEXT_FONT,
                                  colour);
}

void Display_LoadingBar(uint32_t duration_ms, void (*service_hook)(void))
{
    uint16_t inner_x = (uint16_t)(LOADING_BAR_X + 1U);
    uint16_t inner_y = (uint16_t)(LOADING_BAR_Y + 1U);
    uint16_t inner_w = (uint16_t)(LOADING_BAR_W - 2U);
    uint16_t inner_h = (uint16_t)(LOADING_BAR_H - 2U);

    if (duration_ms == 0U)
        duration_ms = 1U;

    Display_LoadingBarSetText(Display_LoadingBarSelectStartupLine(), LOADING_SCREEN_TEXT_COLOUR);

    //ST7796_DrawRectangle(LOADING_BAR_X,
    //                     LOADING_BAR_Y,
    //                     (uint16_t)(LOADING_BAR_X + LOADING_BAR_W - 1U),
    //                     (uint16_t)(LOADING_BAR_Y + LOADING_BAR_H - 1U),
    //                     LOADING_SCREEN_BAR_COLOUR);

    uint32_t start = HAL_GetTick();
    uint16_t prev_fill = 0U;

    for (;;)
    {
        if (service_hook)
            service_hook();

        uint32_t elapsed = HAL_GetTick() - start;
        if (elapsed >= duration_ms)
            elapsed = duration_ms;

        if (elapsed > 0U)
        {
            uint16_t fill = (uint16_t)((elapsed * inner_w) / duration_ms);

            if (fill > prev_fill)
            {
                ST7796_DrawFilledRectangle((uint16_t)(inner_x + prev_fill),
                                           inner_y,
                                           (uint16_t)(fill - prev_fill),
                                           inner_h,
                                           LOADING_SCREEN_BAR_COLOUR);
                prev_fill = fill;
            }
        }

        if (elapsed >= duration_ms)
            break;
    }
}

void Display_LoadingBarClear(void)
{
    /* Keep splash text/background intact until startup fade-out removes it. */
}