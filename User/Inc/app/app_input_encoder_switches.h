#ifndef APP_APP_INPUT_ENCODER_SWITCHES_H
#define APP_APP_INPUT_ENCODER_SWITCHES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void AppInputEncoderSwitches_Init(void);
void AppInputEncoderSwitches_Sample(void);
void AppInputEncoderSwitches_ProcessPending(void);
uint8_t AppInputEncoderSwitches_HandleExti(uint16_t gpio_pin);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_INPUT_ENCODER_SWITCHES_H */