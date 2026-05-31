#ifndef APP_APP_ACTIVATION_H
#define APP_APP_ACTIVATION_H

#include <stdint.h>

#include "app_event.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t AppActivation_HandleEvent(const AppEvent_t *event);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_ACTIVATION_H */