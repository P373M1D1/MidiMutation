#include "app/app_startup_display.h"

#include "app/app_sd_card.h"
#include "app/app_startup_clock.h"
#include "display_functions.h"
#include "fonts.h"
#include "led_functions.h"
#include "runtime_config.h"
#include "st7796.h"

#define APP_STARTUP_SPLASH_X 0U
#define APP_STARTUP_SPLASH_Y 0U
#define APP_STARTUP_SPLASH_FILE_NAME "startup.rgb"
#define APP_STARTUP_SPLASH_RGB565_PIXEL_BYTES 2U
#define APP_STARTUP_SPLASH_RGB888_PIXEL_BYTES 3U
#define APP_STARTUP_SPLASH_RGB565_ROW_BYTES ((uint32_t)ST7796_WIDTH * APP_STARTUP_SPLASH_RGB565_PIXEL_BYTES)
#define APP_STARTUP_SPLASH_RGB888_ROW_BYTES ((uint32_t)ST7796_WIDTH * APP_STARTUP_SPLASH_RGB888_PIXEL_BYTES)
#define APP_STARTUP_SPLASH_RGB565_FILE_BYTES (APP_STARTUP_SPLASH_RGB565_ROW_BYTES * (uint32_t)ST7796_HEIGHT)
#define APP_STARTUP_SPLASH_RGB888_FILE_BYTES (APP_STARTUP_SPLASH_RGB888_ROW_BYTES * (uint32_t)ST7796_HEIGHT)

#define APP_STARTUP_STATUS_TEXT_X 10U
#define APP_STARTUP_STATUS_TEXT_Y 10U
#define APP_STARTUP_STATUS_FONT Font_7x10
#define APP_STARTUP_STATUS_HEALTH_Y (APP_STARTUP_STATUS_TEXT_Y + APP_STARTUP_STATUS_FONT.height + 2U)
#define APP_STARTUP_STATUS_SUBTEXT_Y (APP_STARTUP_STATUS_HEALTH_Y + APP_STARTUP_STATUS_FONT.height + 2U)
#define APP_STARTUP_STATUS_FG_COLOUR WHITE
#define APP_STARTUP_STATUS_TEXT_BUFFER_SIZE 32U
#define APP_STARTUP_LOADING_BAR_MS_DEFAULT 1000U

#define APP_STARTUP_SYSTEM_CLOCK_PROMOTION_RETRY_MS 50U

typedef enum
{
    APP_STARTUP_SPLASH_FORMAT_NONE = 0,
    APP_STARTUP_SPLASH_FORMAT_RGB565_LE,
    APP_STARTUP_SPLASH_FORMAT_RGB888
} AppStartupSplashFormat_t;

static uint16_t app_startup_splash_rgb565_row[ST7796_WIDTH];
static uint8_t app_startup_splash_rgb888_row[APP_STARTUP_SPLASH_RGB888_ROW_BYTES];

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

static AppStartupSplashFormat_t AppStartupDisplay_GetSplashFormat(uint32_t file_size)
{
    if (file_size == APP_STARTUP_SPLASH_RGB888_FILE_BYTES)
        return APP_STARTUP_SPLASH_FORMAT_RGB888;

    if (file_size == APP_STARTUP_SPLASH_RGB565_FILE_BYTES)
        return APP_STARTUP_SPLASH_FORMAT_RGB565_LE;

    return APP_STARTUP_SPLASH_FORMAT_NONE;
}

static uint16_t AppStartupDisplay_Rgb888ToRgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint16_t)(((uint16_t)(red & 0xF8U) << 8)
                    | ((uint16_t)(green & 0xFCU) << 3)
                    | ((uint16_t)blue >> 3));
}

static uint16_t AppStartupDisplay_SwapRgb565RedBlue(uint16_t pixel)
{
    return (uint16_t)((pixel & 0x07E0U)
                    | ((pixel & 0xF800U) >> 11)
                    | ((pixel & 0x001FU) << 11));
}

static void AppStartupDisplay_PrepareSplashRowForPanel(void)
{
    for (uint16_t x = 0U; x < ST7796_WIDTH; ++x)
        app_startup_splash_rgb565_row[x] =
            AppStartupDisplay_SwapRgb565RedBlue(app_startup_splash_rgb565_row[x]);
}

static uint8_t AppStartupDisplay_ReadSplashRowRgb565(AppSdCardFile_t *file)
{
    return AppSdCard_ReadFile(file,
                              (uint8_t *)app_startup_splash_rgb565_row,
                              APP_STARTUP_SPLASH_RGB565_ROW_BYTES)
        == APP_STARTUP_SPLASH_RGB565_ROW_BYTES;
}

static uint8_t AppStartupDisplay_ReadSplashRowRgb888(AppSdCardFile_t *file)
{
    if (AppSdCard_ReadFile(file,
                           app_startup_splash_rgb888_row,
                           APP_STARTUP_SPLASH_RGB888_ROW_BYTES)
        != APP_STARTUP_SPLASH_RGB888_ROW_BYTES)
    {
        return 0U;
    }

    for (uint16_t x = 0U; x < ST7796_WIDTH; ++x)
    {
        const uint32_t source_index = (uint32_t)x * APP_STARTUP_SPLASH_RGB888_PIXEL_BYTES;
        app_startup_splash_rgb565_row[x] =
            AppStartupDisplay_Rgb888ToRgb565(app_startup_splash_rgb888_row[source_index],
                                             app_startup_splash_rgb888_row[source_index + 1U],
                                             app_startup_splash_rgb888_row[source_index + 2U]);
    }

    return 1U;
}

static uint8_t AppStartupDisplay_ReadSplashRow(AppSdCardFile_t *file,
                                               AppStartupSplashFormat_t format)
{
    uint8_t row_read = 0U;

    if (format == APP_STARTUP_SPLASH_FORMAT_RGB888)
        row_read = AppStartupDisplay_ReadSplashRowRgb888(file);
    else if (format == APP_STARTUP_SPLASH_FORMAT_RGB565_LE)
        row_read = AppStartupDisplay_ReadSplashRowRgb565(file);

    if (row_read)
        AppStartupDisplay_PrepareSplashRowForPanel();

    return row_read;
}

static uint8_t AppStartupDisplay_DrawSplashFromSd(void)
{
    AppSdCardFile_t file;
    AppStartupSplashFormat_t format;

    AppSdCard_InitAndProbe();

    if (!AppSdCard_IsFilesystemReady())
        return 0U;

    if (!AppSdCard_OpenFile(&file, APP_STARTUP_SPLASH_FILE_NAME))
        return 0U;

    format = AppStartupDisplay_GetSplashFormat(file.file_size);
    if (format == APP_STARTUP_SPLASH_FORMAT_NONE)
        return 0U;

    if (!ST7796_BeginImageWrite(APP_STARTUP_SPLASH_X,
                                APP_STARTUP_SPLASH_Y,
                                ST7796_WIDTH,
                                ST7796_HEIGHT))
    {
        return 0U;
    }

    for (uint16_t y = 0U; y < ST7796_HEIGHT; ++y)
    {
        if (!AppStartupDisplay_ReadSplashRow(&file, format))
        {
            ST7796_EndImageWrite();
            return 0U;
        }

        ST7796_WriteImagePixels(app_startup_splash_rgb565_row, ST7796_WIDTH);
    }

    ST7796_EndImageWrite();
    return 1U;
}

static void AppStartupDisplay_DrawSplash(void)
{
    if (!AppStartupDisplay_DrawSplashFromSd())
        ST7796_FillScreen(BLACK);
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

    RuntimeConfig_FormatPersistentStoreHealthText(status_text, sizeof(status_text));
    ST7796_WriteStringTransparent(APP_STARTUP_STATUS_TEXT_X,
                                  APP_STARTUP_STATUS_HEALTH_Y,
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
    AppStartupDisplay_DrawSplash();
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
