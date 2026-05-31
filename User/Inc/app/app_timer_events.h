#ifndef APP_APP_TIMER_EVENTS_H
#define APP_APP_TIMER_EVENTS_H

#include "app_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Handles coarse periodic timer events published by the runtime scheduler. */
uint8_t AppTimerEvents_HandleEvent(const AppEvent_t *event);
/* Clears the pending save-timeout flag after the save service consumes it. */
void AppTimerEvents_AcknowledgeSaveTimeoutEvent(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_TIMER_EVENTS_H */