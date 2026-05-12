#ifndef DISPLAY_VALUE_HELPERS_H
#define DISPLAY_VALUE_HELPERS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Internal value/edit helpers shared by renderer and menu-value modules. */
uint16_t Display_GetBrightnessFromUiValue(uint8_t ui_value);
uint8_t Display_GetGlobalBrightnessUiValue(uint16_t brightness);
void Display_ApplyConfiguredBacklightBrightnessNow(void);
void Display_ForceFullDisplayRedraw(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_VALUE_HELPERS_H */