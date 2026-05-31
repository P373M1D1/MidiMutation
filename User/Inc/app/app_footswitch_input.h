#ifndef APP_APP_FOOTSWITCH_INPUT_H
#define APP_APP_FOOTSWITCH_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void AppFootswitchInput_HandleGpioExti(uint16_t gpio_pin);
void AppFootswitchInput_ProcessPending(void);
uint8_t AppFootswitchInput_ReadPressed(uint8_t index);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_FOOTSWITCH_INPUT_H */