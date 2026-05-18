#ifndef APP_APP_TEMPO_H
#define APP_APP_TEMPO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void AppTempo_HandleTapPress(uint32_t now);
void AppTempo_ApplyEncoderStep(int8_t step);
void AppTempo_ExternalClockHoldoverMirrorService(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_TEMPO_H */