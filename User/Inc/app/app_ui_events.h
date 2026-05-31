#ifndef APP_APP_UI_EVENTS_H
#define APP_APP_UI_EVENTS_H

#include <stdint.h>

#include "app_event.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t AppUiEvents_HandleEvent(const AppEvent_t *event);
void AppUiEvents_HandleEncoderTurn(uint8_t encoder_source, int8_t delta);
void AppUiEvents_HandleEncoderPress(uint8_t press_mask);
void AppUiEvents_PreparePresetActivation(uint8_t exit_preset_edit);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_UI_EVENTS_H */