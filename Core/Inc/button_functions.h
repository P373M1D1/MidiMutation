#ifndef BUTTON_FUNCTIONS_H
#define BUTTON_FUNCTIONS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Button_Init(void);
void Button_HandlePress(uint16_t gpio_pin);
void Button_CheckAndHandle(void);
void activatePreset(uint8_t idx);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_FUNCTIONS_H
