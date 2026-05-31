#ifndef APP_APP_SAVE_SERVICE_H
#define APP_APP_SAVE_SERVICE_H

#include <stdint.h>

#include "app_event.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t AppSaveService_HandleEvent(const AppEvent_t *event);
void AppSaveService_Service(void);
uint8_t AppSaveService_HasPendingWork(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_SAVE_SERVICE_H */