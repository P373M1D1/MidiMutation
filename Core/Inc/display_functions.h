#ifndef DISPLAY_FUNCTIONS_H
#define DISPLAY_FUNCTIONS_H

/*
 * High-level display routines: backlight control and main screen rendering.
 * Wraps ST7796 primitives and DAC backlight; keeps main.cpp free of details.
 */

#include <stdint.h>
#include "presets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Backlight (DAC1 CH1 on PA4) ─────────────────────────────────────────── */

void Display_BL_Init(void);
void Display_BL_FadeIn(void);
void Display_BL_FadeOut(void);

/* ── Screen content ──────────────────────────────────────────────────────── */

void Display_DrawMainScreen(const Preset_t *p, uint16_t bpm);
void Display_UpdateBPM(uint16_t bpm);

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
