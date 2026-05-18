#ifndef APP_APP_TEMPO_H
#define APP_APP_TEMPO_H

#include <stdint.h>

#include "app_event.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t AppTempo_HandleEvent(const AppEvent_t *event);
void AppTempo_ApplyEncoderStep(int8_t step);
void AppTempo_ExternalClockHoldoverMirrorService(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_TEMPO_H */