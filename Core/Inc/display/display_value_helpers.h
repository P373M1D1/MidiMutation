#ifndef DISPLAY_VALUE_HELPERS_H
#define DISPLAY_VALUE_HELPERS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shared value/edit helpers used by renderer and menu-value modules.
 * These are intentionally tiny, but they sit on important boundaries: UI
 * brightness mapping and whole-screen invalidation after theme changes. */
uint16_t Display_GetBrightnessFromUiValue(uint8_t ui_value);
/* Inverse mapping is approximate by design: it finds the nearest UI step for a
 * stored raw brightness value so the GLOBAL menu can round-trip cleanly. */
uint8_t Display_GetGlobalBrightnessUiValue(uint16_t brightness);
void Display_ApplyConfiguredBacklightBrightnessNow(void);
void Display_ForceFullDisplayRedraw(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_VALUE_HELPERS_H */