#ifndef DISPLAY_FUNCTIONS_H
#define DISPLAY_FUNCTIONS_H /* include guard for high-level display declarations */

/*
 * High-level display routines: backlight control and main screen rendering.
 * Wraps ST7796 primitives and DAC backlight; keeps main.cpp free of details.
 */

#include <stdint.h>
#include "presets.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	DISPLAY_PRESET_EDIT_FIELD_NONE = 0,
	DISPLAY_PRESET_EDIT_FIELD_PROGRAM,
	DISPLAY_PRESET_EDIT_FIELD_RELAY,
	DISPLAY_PRESET_EDIT_FIELD_CC_CHANNEL,
	DISPLAY_PRESET_EDIT_FIELD_CC_NUMBER,
	DISPLAY_PRESET_EDIT_FIELD_CC_VALUE,
} DisplayPresetEditFieldType_t;

typedef struct {
	DisplayPresetEditFieldType_t type;
	uint8_t itemIndex;
} DisplayPresetEditField_t;

/* ── Backlight (DAC1 CH1 on PA4) ─────────────────────────────────────────── */

void Display_BL_Init(void);
void Display_BL_FadeIn(void);
void Display_BL_FadeOut(void);

/* ── Screen content ──────────────────────────────────────────────────────── */

void Display_DrawMainScreen(const Preset_t *p, uint16_t bpm);
void Display_UpdateBPM(uint16_t bpm);
void Display_MainInfoScrollReset(void);
uint8_t Display_MainInfoScrollBy(int8_t delta);
void Display_PresetEditEnter(void);
void Display_PresetEditExit(void);
uint8_t Display_PresetEditIsActive(void);
uint8_t Display_PresetEditMoveCursor(int8_t delta);
uint8_t Display_PresetEditMoveCursorAndRefresh(const Preset_t *p, int8_t delta);
DisplayPresetEditField_t Display_PresetEditGetField(void);
void Display_PresetEditRefreshCurrentField(const Preset_t *p);

/* ── Screensaver (backlight idle mode) ───────────────────────────────────── */

/** Draws a progress bar in the lower quarter and blocks for duration_ms. */
void Display_LoadingBar(uint32_t duration_ms);
/** Clears the loading bar area to black. */
void Display_LoadingBarClear(void);

/** Call on any user input to reset the inactivity timer. */
void Display_ScreensaverActivity(void);

/** Returns 1 while the backlight idle mode is currently active. */
uint8_t Display_ScreensaverIsActive(void);

/** Immediately dismiss the backlight idle mode if active without drawing the main screen. */
void Display_ScreensaverDismiss(void);

/** Call every main-loop iteration. Activates after the inactivity timeout;
 *  fades the backlight out and restores the main screen on the next activity event. */
void Display_ScreensaverUpdate(const Preset_t *p, uint16_t bpm);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_FUNCTIONS_H */
