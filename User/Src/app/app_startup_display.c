#include "app/app_startup_display.h"

#include "app/app_startup_clock.h"
#include "display_functions.h"
#include "fonts.h"
#include "image.h"
#include "led_functions.h"
#include "runtime_config.h"
#include "st7796.h"

#define APP_STARTUP_SPLASH_X 0U
#define APP_STARTUP_SPLASH_Y 0U

#define APP_STARTUP_STATUS_TEXT_X 10U
#define APP_STARTUP_STATUS_TEXT_Y 10U
#define APP_STARTUP_STATUS_FONT Font_7x10
#define APP_STARTUP_STATUS_SUBTEXT_Y (APP_STARTUP_STATUS_TEXT_Y + APP_STARTUP_STATUS_FONT.height + 2U)
#define APP_STARTUP_STATUS_FG_COLOUR WHITE
#define APP_STARTUP_STATUS_TEXT_BUFFER_SIZE 16U
#define APP_STARTUP_LOADING_BAR_MS_DEFAULT 1000U

#define APP_STARTUP_SYSTEM_CLOCK_PROMOTION_RETRY_MS 50U

static const char *AppStartupDisplay_GetClockSourceStatusText(void)
{
    return AppStartupClock_HardwareUsesHse() ? "clock source: HSE" : "clock source: HSI";
}

static uint32_t AppStartupDisplay_GetLoadingBarDurationMs(void)
{
    const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();

    if (!global)
        return APP_STARTUP_LOADING_BAR_MS_DEFAULT;

    return (uint32_t)global->startup_delay_seconds * 1000UL;
}

static void AppStartupDisplay_DrawPersistentStoreStatus(void)
{
    char status_text[APP_STARTUP_STATUS_TEXT_BUFFER_SIZE];

    RuntimeConfig_FormatPersistentStoreStatusText(status_text, sizeof(status_text));
    ST7796_WriteStringTransparent(APP_STARTUP_STATUS_TEXT_X,
                                  APP_STARTUP_STATUS_TEXT_Y,
                                  status_text,
                                  APP_STARTUP_STATUS_FONT,
                                  APP_STARTUP_STATUS_FG_COLOUR);
}

static void AppStartupDisplay_DrawClockSource(void)
{
    ST7796_WriteStringTransparent(APP_STARTUP_STATUS_TEXT_X,
                                  APP_STARTUP_STATUS_SUBTEXT_Y,
                                  AppStartupDisplay_GetClockSourceStatusText(),
                                  APP_STARTUP_STATUS_FONT,
                                  APP_STARTUP_STATUS_FG_COLOUR);
}

static void AppStartupDisplay_ServiceClockPromotion(void)
{
    static uint32_t last_promotion_attempt_tick = 0U;
    static uint8_t last_drawn_clock_source = 0xFFU;
    uint8_t hardware_uses_hse = AppStartupClock_HardwareUsesHse();
    uint32_t now = HAL_GetTick();

    if (last_drawn_clock_source != hardware_uses_hse)
    {
        AppStartupDisplay_DrawClockSource();
        last_drawn_clock_source = hardware_uses_hse;
    }

    if (hardware_uses_hse)
        return;

    if ((now - last_promotion_attempt_tick) < APP_STARTUP_SYSTEM_CLOCK_PROMOTION_RETRY_MS)
        return;

    last_promotion_attempt_tick = now;
    (void)AppStartupClock_PromoteToHse();

    hardware_uses_hse = AppStartupClock_HardwareUsesHse();
    if (last_drawn_clock_source != hardware_uses_hse)
    {
        AppStartupDisplay_DrawClockSource();
        last_drawn_clock_source = hardware_uses_hse;
    }
}

static void AppStartupDisplay_AttemptClockPromotion(void)
{
    (void)AppStartupClock_PromoteToHse();
}

void AppStartupDisplay_Run(void)
{
    ST7796_InitControlPins();
    LED_InitBoardOutputs();
    Display_BL_Init();
    ST7796_Init();
    ST7796_DrawImageSwapRB(APP_STARTUP_SPLASH_X,
                           APP_STARTUP_SPLASH_Y,
                           IMAGE_WIDTH,
                           IMAGE_HEIGHT,
                           image_data);
    Display_BL_FadeIn();
    AppStartupDisplay_DrawPersistentStoreStatus();
    AppStartupDisplay_DrawClockSource();
    Display_LoadingBar(AppStartupDisplay_GetLoadingBarDurationMs(), AppStartupDisplay_ServiceClockPromotion);
    Display_LoadingBarClear();
    AppStartupDisplay_AttemptClockPromotion();
    AppStartupDisplay_DrawClockSource();
    Display_BL_FadeOut();
    ST7796_FillScreen(BLACK);
    Display_BL_FadeIn();
}