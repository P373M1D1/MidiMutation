#ifndef APP_APP_STARTUP_H
#define APP_APP_STARTUP_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t AppStartup_GetLoadingBarDurationMs(void);
void AppStartup_DrawPersistentStoreStatus(void);
void AppStartup_DrawClockSource(void);
void AppStartup_ServiceClockPromotion(void);
void AppStartup_AttemptClockPromotion(void);
HAL_StatusTypeDef AppStartup_RestoreHsiPll(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_STARTUP_H */