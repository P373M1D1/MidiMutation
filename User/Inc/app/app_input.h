#ifndef APP_APP_INPUT_H
#define APP_APP_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void AppInput_Init(void);
void AppInput_ProcessPending(void);
void AppInput_HandleGpioExti(uint16_t gpio_pin);
uint8_t AppInput_Encoder2SwitchIsPressed(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_INPUT_H */