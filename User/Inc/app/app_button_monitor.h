#ifndef APP_APP_BUTTON_MONITOR_H
#define APP_APP_BUTTON_MONITOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void AppButtonMonitor_Init(void);
uint8_t AppButtonMonitor_HandleFootswitchPress(uint8_t index);
uint8_t AppButtonMonitor_HandleTapPress(void);
void AppButtonMonitor_LogMuteState(const char *tag,
								   uint32_t now,
								   uint8_t logical_pressed);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_BUTTON_MONITOR_H */