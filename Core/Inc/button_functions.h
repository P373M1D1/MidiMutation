#ifndef BUTTON_FUNCTIONS_H
#define BUTTON_FUNCTIONS_H /* include guard for button/footswitch declarations */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Button_HandleInterrupt(uint16_t gpio_pin);
void Button_ProcessInterruptEvent(uint8_t index, uint8_t is_pressed, uint32_t now);
void Button_ProcessPendingEvents(void);
uint8_t Button_HandleTapPress(uint32_t now);
uint8_t Button_HandleMutePress(uint32_t now);
void Button_CancelTapBankCombo(void);
uint8_t Button_IsTapHeld(void);
uint8_t Button_IsMuteHeld(void);
uint8_t Button_SpecialFunctionsActive(void);
void Button_ResetSpecialFunctions(void);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_FUNCTIONS_H
