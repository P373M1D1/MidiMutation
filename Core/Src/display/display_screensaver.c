#include "display_functions.h"
#include "display/display_internal.h"
#include "runtime_config.h"
#include "stm32f4xx_hal.h"

#define SCREENSAVER_TIMEOUT_MIN_DEFAULT 10UL

static uint32_t screensaver_last_activity_tick = 0U;
static uint8_t screensaver_active = 0U;

static uint32_t Display_GetConfiguredScreensaverTimeoutMs(void)
{
    const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();
    uint32_t timeout_minutes = SCREENSAVER_TIMEOUT_MIN_DEFAULT;

    if (global && global->screensaver_timeout_minutes > 0U)
        timeout_minutes = global->screensaver_timeout_minutes;

    return timeout_minutes * 60UL * 1000UL;
}

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
    Display_ClearMainLayoutDirty();
    Display_BL_FadeIn();
}

uint8_t Display_ScreensaverUpdate(void)
{
    uint32_t screensaver_timeout_ms = Display_GetConfiguredScreensaverTimeoutMs();
    uint32_t now = HAL_GetTick();

    if (!screensaver_active)
    {
        if (now - screensaver_last_activity_tick >= screensaver_timeout_ms)
        {
            screensaver_active = 1U;
            Display_ClearMainLayoutDirty();
            Display_BL_FadeOut();
        }
        return 0U;
    }

    if (now - screensaver_last_activity_tick < screensaver_timeout_ms)
    {
        Display_ScreensaverDismiss();
        return 1U;
    }

    return 0U;
}
