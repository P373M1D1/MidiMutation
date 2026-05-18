#ifndef APP_APP_BUTTON_COMBO_H
#define APP_APP_BUTTON_COMBO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t AppButtonCombo_HandleTapPress(uint32_t now, uint8_t mute_held);
uint8_t AppButtonCombo_HandleMutePress(uint32_t now, uint8_t tap_held);
uint8_t AppButtonCombo_HandleMuteRelease(uint32_t now);
uint8_t AppButtonCombo_Service(uint32_t now);
uint8_t AppButtonCombo_IsMuteActivationPending(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_BUTTON_COMBO_H */