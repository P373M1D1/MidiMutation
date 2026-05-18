#ifndef APP_APP_DISPATCH_H
#define APP_APP_DISPATCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void AppDispatch_HandleEncoderTurnEvent(uint8_t encoder_source, int8_t delta);
void AppDispatch_ProcessPendingEvents(void);
void AppDispatch_SaveService(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_DISPATCH_H */