#ifndef DISPLAY_MAIN_TITLE_H
#define DISPLAY_MAIN_TITLE_H

#include <stdint.h>

#include "presets.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t Display_GetPresetNameEditMaxIndex(const Preset_t *preset);
void Display_DrawCurrentBankNameLine(void);
void Display_DrawPresetName(const Preset_t *preset);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MAIN_TITLE_H */