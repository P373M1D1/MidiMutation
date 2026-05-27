#include "app/app_startup.h"

#include "app/app_startup_bootstrap.h"
#include "app/app_startup_clock.h"
#include "app/app_startup_display.h"
#include "app_event.h"
#include "runtime_config.h"

HAL_StatusTypeDef AppStartup_RestoreHsiPll(void)
{
    return AppStartupClock_RestoreHsiPll();
}

void AppStartup_Run(void)
{
    /* Bring persistence up first so later startup stages can consume restored
     * settings instead of hard-coded defaults. */
    RuntimeConfig_Init();
    /* Event queues exist before display/bootstrap so those stages can post the
     * same app-level requests used during normal runtime. */
    AppEvent_Init();
    /* Display startup owns splash/initial redraw sequencing before bootstrap
     * starts talking to the rest of the board and external devices. */
    AppStartupDisplay_Run();
    AppStartupBootstrap_Run();
}