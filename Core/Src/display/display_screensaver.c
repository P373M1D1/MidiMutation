#include "display_functions.h"
#include "display/display_internal.h"
#include "runtime_config.h"
#include "stm32f4xx_hal.h"

/* Inactivity-driven screensaver/backlight policy.
 *
 * This module does not draw a separate screensaver scene; it simply tracks the
 * last activity time, decides when the UI should be considered idle, and fades
 * the backlight out/in. The main screen is marked dirty so normal rendering can
 * restore the UI when activity returns. */

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
    /* Any input source calls this; the module only needs the latest timestamp,
     * not the identity of the activity that kept the UI awake. */
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
        /* Return 1 exactly on wake so the caller can schedule a main-screen
         * redraw after the backlight fade-in without polling extra state. */
        Display_ScreensaverDismiss();
        return 1U;
    }

    return 0U;
}
