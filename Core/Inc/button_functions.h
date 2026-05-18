#ifndef BUTTON_FUNCTIONS_H
#define BUTTON_FUNCTIONS_H /* include guard for button/footswitch declarations */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t Button_ProcessInterruptEvent(uint8_t index, uint8_t is_pressed, uint32_t now);
void Button_ProcessPendingEvents(void);
uint8_t Button_IsMuteHeld(void);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_FUNCTIONS_H
