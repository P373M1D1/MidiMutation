#include <string.h>

#include "display_functions.h"
#include "display/display_compose_helpers.h"
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

#define LOADING_SCREEN_BG_COLOUR   BLACK
#define LOADING_SCREEN_BAR_COLOUR  DARK_RED
#define LOADING_SCREEN_TEXT_COLOUR WHITE

static void Display_LoadingBarClearTextRow(void)
{
    Display_ComposeFillRect(ST7796_WIDTH,
                            LOADING_BAR_TEXT_FONT.height,
                            0U,
                            0U,
                            ST7796_WIDTH,
                            LOADING_BAR_TEXT_FONT.height,
                            LOADING_SCREEN_BG_COLOUR);
    Display_ComposeBlit(0U,
                        LOADING_BAR_TEXT_Y,
                        ST7796_WIDTH,
                        LOADING_BAR_TEXT_FONT.height);
}

static void Display_LoadingBarSetText(const char *text, uint16_t colour)
{
    size_t text_len = strlen(text);
    uint16_t text_x = (uint16_t)(ST7796_WIDTH - ((uint16_t)text_len * LOADING_BAR_TEXT_FONT.width) - LOADING_BAR_X);

    /* Boot text is right-aligned to the bar end so changing message lengths do
     * not shift the visual relationship between the text row and the bar. */
    Display_ComposeFillRect(ST7796_WIDTH,
                            LOADING_BAR_TEXT_FONT.height,
                            0U,
                            0U,
                            ST7796_WIDTH,
                            LOADING_BAR_TEXT_FONT.height,
                            LOADING_SCREEN_BG_COLOUR);
    Display_ComposeString16(ST7796_WIDTH,
                            LOADING_BAR_TEXT_FONT.height,
                            text_x,
                            0U,
                            text,
                            LOADING_BAR_TEXT_FONT,
                            colour,
                            LOADING_SCREEN_BG_COLOUR);
    Display_ComposeBlit(0U,
                        LOADING_BAR_TEXT_Y,
                        ST7796_WIDTH,
                        LOADING_BAR_TEXT_FONT.height);
}

void Display_LoadingBar(uint32_t duration_ms, void (*service_hook)(void))
{
    Display_LoadingBarSetText(LOADING_BAR_WAIT_TEXT, LOADING_SCREEN_TEXT_COLOUR);

    ST7796_DrawFilledRectangle(LOADING_BAR_X,
                               LOADING_BAR_Y,
                               LOADING_BAR_W,
                               LOADING_BAR_H,
                               LOADING_SCREEN_BG_COLOUR);

    uint32_t start = HAL_GetTick();
    uint16_t prev_fill = 0U;
    uint8_t phase = 0U;
    uint32_t phase_ts = 0U;

    for (;;)
    {
        if (service_hook)
            service_hook();

        uint32_t elapsed = HAL_GetTick() - start;
        if (elapsed >= duration_ms)
            elapsed = duration_ms;

        if (elapsed > 0U)
        {
            uint16_t fill = (uint16_t)((elapsed * LOADING_BAR_W) / duration_ms);

            if (fill > prev_fill)
            {
                ST7796_DrawFilledRectangle((uint16_t)(LOADING_BAR_X + prev_fill),
                                           LOADING_BAR_Y,
                                           (uint16_t)(fill - prev_fill),
                                           LOADING_BAR_H,
                                           LOADING_SCREEN_BAR_COLOUR);
                prev_fill = fill;
            }
        }

        uint32_t now = HAL_GetTick();

        /* Text phases are time-based rather than fill-based so boot copy stays
         * readable even if panel SPI speed or duration_ms changes later. */
        if (phase == 0U && elapsed >= duration_ms / LOADING_BAR_PHASE_DIVISOR)
        {
            Display_LoadingBarSetText(LOADING_BAR_MATCH_TEXT, LOADING_SCREEN_TEXT_COLOUR);
            phase = 1U;
            phase_ts = now;
        }
        if (phase == 1U && now - phase_ts >= LOADING_BAR_PHASE_HOLD_MS)
        {
            Display_LoadingBarSetText(LOADING_BAR_MARKERS_TEXT, LOADING_SCREEN_TEXT_COLOUR);
            phase = 2U;
            phase_ts = now;
        }
        if (phase == 2U && now - phase_ts >= LOADING_BAR_PHASE_HOLD_MS)
        {
            Display_LoadingBarClearTextRow();
            phase = 3U;
        }

        if (elapsed >= duration_ms)
            break;
    }
}

void Display_LoadingBarClear(void)
{
    ST7796_DrawFilledRectangle(LOADING_BAR_X,
                               LOADING_BAR_Y,
                               LOADING_BAR_W,
                               LOADING_BAR_H,
                               LOADING_SCREEN_BG_COLOUR);
    Display_LoadingBarSetText(LOADING_BAR_DONE_TEXT, LOADING_SCREEN_TEXT_COLOUR);
    HAL_Delay(LOADING_BAR_PHASE_HOLD_MS);
    Display_LoadingBarClearTextRow();
}