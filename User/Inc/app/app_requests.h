#ifndef APP_APP_REQUESTS_H
#define APP_APP_REQUESTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void App_QueueEncoderPressEvent(uint8_t press_mask, uint32_t tick);
void App_QueueEncoderTurnEvent(uint8_t encoder_source, int8_t delta, uint32_t tick);
void App_QueueBankStepEvent(int8_t delta, uint8_t step_mode);
void App_QueuePresetActivateEvent(uint8_t preset_index);
uint8_t App_TakePendingPresetActivate(uint8_t *preset_index);
void App_QueueScreensaverWakeEvent(void);
void App_QueueScreensaverActivityEvent(void);
void App_AcknowledgeScreensaverWakeEvent(void);
void App_AcknowledgeScreensaverActivityEvent(void);
void App_QueuePeriodicUiServiceEvent(void);
void App_AcknowledgePeriodicUiServiceEvent(void);
void App_QueueRedrawMainScreenEvent(void);
void App_AcknowledgeRedrawMainScreenEvent(void);
void App_QueueSaveRequestEvent(uint8_t save_kind);
void App_AcknowledgeSaveRequestEvent(uint8_t save_kind);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_REQUESTS_H */