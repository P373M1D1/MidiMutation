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
    RuntimeConfig_Init();
    AppEvent_Init();
    AppStartupDisplay_Run();
    AppStartupBootstrap_Run();
}