#ifndef BUTTON_FUNCTIONS_H
#define BUTTON_FUNCTIONS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Button_Init(void);
void Button_HandlePress(uint16_t gpio_pin);
void Button_CheckAndHandle(void);
uint8_t Button_HandleTapPress(uint32_t now);
uint8_t Button_HandleMutePress(uint32_t now);
void Button_CancelTapBankCombo(void);
uint8_t Button_IsTapHeld(void);
uint8_t Button_IsMuteHeld(void);
uint8_t Button_SpecialFunctionsActive(void);
void Button_ResetSpecialFunctions(void);
extern volatile uint8_t current_bank;
void activatePreset(uint8_t idx);
void activateRandom(void);
void activateSpecialFunctions(void);
void deactivateSpecialFunctions(void);
void activateMute(void);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_FUNCTIONS_H
