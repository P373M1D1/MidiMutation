#ifndef DISPLAY_PALETTE_REGISTRY_H
#define DISPLAY_PALETTE_REGISTRY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Named access to the full ST7796 RGB565 palette.
 * The registry is generated from the driver's color macro list so menu code
 * can step by user-visible names without duplicating palette definitions. */

uint16_t DisplayPalette_GetCount(void);
const char *DisplayPalette_GetName(uint16_t index);
uint16_t DisplayPalette_GetValue(uint16_t index);
uint16_t DisplayPalette_FindIndexByValue(uint16_t value);
uint16_t DisplayPalette_StepIndex(uint16_t current_index, int8_t delta);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_PALETTE_REGISTRY_H */