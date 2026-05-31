#ifndef APP_APP_STARTUP_CLOCK_H
#define APP_APP_STARTUP_CLOCK_H

#include <stdint.h>

#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t AppStartupClock_HardwareUsesHse(void);
HAL_StatusTypeDef AppStartupClock_PromoteToHse(void);
HAL_StatusTypeDef AppStartupClock_RestoreHsiPll(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_STARTUP_CLOCK_H */