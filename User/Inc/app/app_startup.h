#ifndef APP_APP_STARTUP_H
#define APP_APP_STARTUP_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

HAL_StatusTypeDef AppStartup_RestoreHsiPll(void);
void AppStartup_Run(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_STARTUP_H */