#ifndef DISPLAY_PALETTE_REGISTRY_H
#define DISPLAY_PALETTE_REGISTRY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Named access to the ST7796 RGB565 palette browse order.
 * The registry is generated from the driver's color macro list, then exposed
 * as a deduped USER-theme browse list that stays grouped by colour family and
 * sorted dark-to-bright within each family. */

uint16_t DisplayPalette_GetCount(void);
const char *DisplayPalette_GetName(uint16_t index);
uint16_t DisplayPalette_GetValue(uint16_t index);
uint16_t DisplayPalette_FindIndexByValue(uint16_t value);
uint16_t DisplayPalette_StepIndex(uint16_t current_index, int8_t delta);
uint16_t DisplayPalette_StepHueIndex(uint16_t current_index, int8_t delta);
uint16_t DisplayPalette_StepBrightnessIndex(uint16_t current_index, int8_t delta);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_PALETTE_REGISTRY_H */