#ifndef DISPLAY_FUNCTIONS_H
#define DISPLAY_FUNCTIONS_H /* include guard for high-level display declarations */

/* Public facade for display subsystem entry points used outside display modules.
 * Keep internal helpers out of this header; use display/display_internal.h
 * (added as Phase 1 scaffolding) for private cross-file display internals.
 */

#include <stdint.h>
#include "presets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -- Preset-edit field descriptors (public; consumed by app dispatcher) -------- */

/* Logical cursor targets on the preset-edit screen.
 * The app layer asks the display which kind of field is active so encoder/input
 * code can decide whether to change a name cell, program number, relay state,
 * CC field, or the preset-init action. */
typedef enum {
	DISPLAY_PRESET_EDIT_FIELD_NONE = 0,
	DISPLAY_PRESET_EDIT_FIELD_NAME,
	DISPLAY_PRESET_EDIT_FIELD_PROGRAM,
	DISPLAY_PRESET_EDIT_FIELD_RELAY,
	DISPLAY_PRESET_EDIT_FIELD_CC_CHANNEL,
	DISPLAY_PRESET_EDIT_FIELD_CC_NUMBER,
	DISPLAY_PRESET_EDIT_FIELD_CC_VALUE,
	DISPLAY_PRESET_EDIT_FIELD_INIT,
} DisplayPresetEditFieldType_t;

typedef struct {
	DisplayPresetEditFieldType_t type;
	uint8_t itemIndex;
} DisplayPresetEditField_t;

/* -- Backlight (DAC1 CH1 on PA4) ---------------------------------------------- */

void Display_BL_Init(void);
void Display_BL_FadeIn(void);
void Display_BL_FadeOut(void);

/* -- Main/live screen ---------------------------------------------------------- */

void Display_DrawMainScreen(const Preset_t *p, uint16_t bpm);
void Display_UpdateBPM(uint16_t bpm);
void Display_MainInfoScrollReset(void);
uint8_t Display_MainInfoScrollBy(int8_t delta);
uint8_t Display_MainInfoScrollAndRefresh(const Preset_t *p, int8_t delta);

/* -- Preset-edit mode ---------------------------------------------------------- */

void Display_RefreshPresetEditMode(const Preset_t *p, uint16_t bpm);
void Display_PresetEditEnter(void);
void Display_PresetEditExit(void);
uint8_t Display_PresetEditIsActive(void);
void Display_PresetNameEditEnter(void);
void Display_PresetNameEditExit(void);
uint8_t Display_PresetNameEditIsActive(void);
uint8_t Display_PresetNameEditMoveCursor(const Preset_t *p, int8_t delta);
uint8_t Display_PresetNameEditGetCursorIndex(void);
void Display_PresetInitConfirmEnter(void);
void Display_PresetInitConfirmExit(void);
uint8_t Display_PresetInitConfirmIsActive(void);
uint8_t Display_PresetEditMoveCursor(int8_t delta);
uint8_t Display_PresetEditMoveCursorAndRefresh(const Preset_t *p, int8_t delta);
DisplayPresetEditField_t Display_PresetEditGetField(void);
void Display_PresetEditRefreshCurrentField(const Preset_t *p);
/* Saving popup ownership lives in the display layer because it overlays either
 * the main screen or the menu body and must restore only the obscured region. */
void Display_ShowSavingPopup(void);
void Display_HideSavingPopup(const Preset_t *p);

/* -- Menu mode ----------------------------------------------------------------- */

void Display_MenuEnter(void);
void Display_MenuExit(void);
uint8_t Display_MenuIsActive(void);
void Display_MenuRefresh(void);
void Display_MenuHome(void);
uint8_t Display_MenuSubEditorIsActive(void);
uint8_t Display_MenuMoveSelection(int8_t delta);
uint8_t Display_MenuActivate(void);
uint8_t Display_MenuBack(void);
void Display_MenuTextEditExit(void);
uint8_t Display_MenuTextEditIsActive(void);
uint8_t Display_MenuTextEditMoveCursor(int8_t delta);
/* Value adjustment may mutate runtime config immediately and can trigger a full
 * redraw for theme changes, so callers should treat this as more than a pure
 * formatting helper. */
uint8_t Display_MenuAdjustValue(int8_t delta);

/* -- Screensaver/loading ------------------------------------------------------- */

/** Draws a progress bar in the lower quarter and blocks for duration_ms. */
void Display_LoadingBar(uint32_t duration_ms);
/** Clears the loading bar area to the current theme background. */
void Display_LoadingBarClear(void);

/** Call on any user input to reset the inactivity timer. */
void Display_ScreensaverActivity(void);

/** Returns 1 while the backlight idle mode is currently active. */
uint8_t Display_ScreensaverIsActive(void);

/** Immediately dismiss the backlight idle mode if active without drawing the main screen. */
void Display_ScreensaverDismiss(void);

/** Call every main-loop iteration. Activates after the inactivity timeout;
 *  fades the backlight out and returns 1 once when the next activity should
 *  wake the main screen so the caller can schedule the redraw. */
uint8_t Display_ScreensaverUpdate(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_FUNCTIONS_H */
