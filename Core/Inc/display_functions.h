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
	DISPLAY_PRESET_EDIT_FIELD_FUNCTION_BUTTON,
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

/** Initializes the display backlight hardware. */
void Display_BL_Init(void);
/** Fades the display backlight in. */
void Display_BL_FadeIn(void);
/** Fades the display backlight out. */
void Display_BL_FadeOut(void);

/* -- Main/live screen ---------------------------------------------------------- */

/** Draws the full main screen for the supplied preset and BPM. */
void Display_DrawMainScreen(const Preset_t *p, uint16_t bpm);
/** Refreshes only the footer/soft-button hint bar. */
void Display_RefreshFootbar(void);
/** Refreshes only the main-screen content area. */
void Display_RefreshMainScreenContent(const Preset_t *p, uint16_t bpm);
/** Updates the BPM area of the main screen. */
void Display_UpdateBPM(uint16_t bpm);
/** Updates only the transport bar/beat area used on beat-edge refresh paths. */
void Display_UpdateTransportBarBeatFast(void);
/** Refreshes right-column transport rows (bar/beat + elapsed time). */
void Display_RefreshTransportInfoRows(void);
/** Returns true when the BPM header is currently showing SYNC. */
uint8_t Display_IsBpmHeaderSyncing(void);
/** Emits the BPM diagnostic line. */
void Display_BpmDiagnosticService(void);
/** Resets the main-info scroll position. */
void Display_MainInfoScrollReset(void);
/** Scrolls the main-info area by one step. */
uint8_t Display_MainInfoScrollBy(int8_t delta);
/** Scrolls and immediately refreshes the main-info area. */
uint8_t Display_MainInfoScrollAndRefresh(const Preset_t *p, int8_t delta);

/* -- Preset-edit mode ---------------------------------------------------------- */

/** Refreshes the preset-edit screen. */
void Display_RefreshPresetEditMode(const Preset_t *p, uint16_t bpm);
/** Enters preset-edit mode. */
void Display_PresetEditEnter(void);
/** Exits preset-edit mode. */
void Display_PresetEditExit(void);
/** Returns true when preset-edit mode is active. */
uint8_t Display_PresetEditIsActive(void);
/** Enters name-edit mode inside preset edit. */
void Display_PresetNameEditEnter(void);
/** Exits name-edit mode inside preset edit. */
void Display_PresetNameEditExit(void);
/** Returns true when preset-name editing is active. */
uint8_t Display_PresetNameEditIsActive(void);
/** Moves the preset-name cursor by one step. */
uint8_t Display_PresetNameEditMoveCursor(const Preset_t *p, int8_t delta);
/** Returns the current preset-name cursor index. */
uint8_t Display_PresetNameEditGetCursorIndex(void);
/** Enters the preset-init confirmation prompt. */
void Display_PresetInitConfirmEnter(void);
/** Exits the preset-init confirmation prompt. */
void Display_PresetInitConfirmExit(void);
/** Returns true when the preset-init prompt is active. */
uint8_t Display_PresetInitConfirmIsActive(void);
/** Moves the preset-edit cursor by one step. */
uint8_t Display_PresetEditMoveCursor(int8_t delta);
/** Moves the preset-edit cursor and refreshes the changed field. */
uint8_t Display_PresetEditMoveCursorAndRefresh(const Preset_t *p, int8_t delta);
/** Returns the currently selected preset-edit field. */
DisplayPresetEditField_t Display_PresetEditGetField(void);
/** Refreshes the field currently selected in preset edit. */
void Display_PresetEditRefreshCurrentField(const Preset_t *p);
/* Saving popup ownership lives in the display layer because it overlays either
 * the main screen or the menu body and must restore only the obscured region. */
/** Shows the saving popup overlay. */
void Display_ShowSavingPopup(void);
/** Hides the saving popup overlay and restores the obscured content. */
void Display_HideSavingPopup(const Preset_t *p);
/** Shows the manual BACKUP TO SD progress popup. */
void Display_ShowBackupPopup(void);
/** Shows a manual backup status popup using the same badge area. */
void Display_ShowBackupPopupMessage(const char *message);
/** Hides the backup/status popup overlay and restores obscured content. */
void Display_HideBackupPopup(const Preset_t *p);
/** Shows the timebend popup overlay. */
void Display_ShowTimebendPopup(void);
/** Hides the timebend popup overlay and restores the obscured content. */
void Display_HideTimebendPopup(const Preset_t *p);
/** Shows the preset-edit LEARNING popup overlay. */
void Display_ShowLearningPopup(void);
/** Hides the preset-edit LEARNING popup overlay and restores obscured content. */
void Display_HideLearningPopup(const Preset_t *p);

/* -- Menu mode ----------------------------------------------------------------- */

/** Enters the top-level menu. */
void Display_MenuEnter(void);
/** Enters the metronome quick-access page. */
uint8_t Display_MenuEnterMetronomeQuickAccess(void);
/** Enters the preset function-button editor for one preset. */
void Display_MenuEnterPresetFunctionButtonEditor(uint8_t preset_index);
/** Exits the menu. */
void Display_MenuExit(void);
/** Returns true when the menu is active. */
uint8_t Display_MenuIsActive(void);
/** Returns true when a confirm action page is active. */
uint8_t Display_MenuConfirmActionIsActive(void);
/** Refreshes the current menu page. */
void Display_MenuRefresh(void);
/** Moves back to the menu home page. */
void Display_MenuHome(void);
/** Returns true when the preview overlay may be shown. */
uint8_t Display_MenuPreviewCanShow(void);
/** Returns true when the menu preview overlay is active. */
uint8_t Display_MenuPreviewIsActive(void);
/** Shows the menu preview overlay. */
void Display_MenuPreviewEnter(const Preset_t *p, uint16_t bpm);
/** Hides the menu preview overlay. */
void Display_MenuPreviewExit(void);
/** Returns true when a submenu is active. */
uint8_t Display_MenuSubEditorIsActive(void);
/** Returns true when the user-theme editor is active. */
uint8_t Display_MenuUserThemeEditIsActive(void);
/** Moves the menu selection. */
uint8_t Display_MenuMoveSelection(int8_t delta);
/** Activates the currently selected menu item. */
uint8_t Display_MenuActivate(void);
/** Handles one step of menu back-navigation. */
uint8_t Display_MenuBack(void);
/** Exits text-edit mode within the menu. */
void Display_MenuTextEditExit(void);
/** Returns true when menu text-edit mode is active. */
uint8_t Display_MenuTextEditIsActive(void);
/** Moves the text-edit cursor. */
uint8_t Display_MenuTextEditMoveCursor(int8_t delta);
/* Value adjustment may mutate runtime config immediately and can trigger a full
 * redraw for theme changes, so callers should treat this as more than a pure
 * formatting helper. */
/** Adjusts the value of the currently selected menu field. */
uint8_t Display_MenuAdjustValue(int8_t delta);
/** Adjusts the user-theme hue. */
uint8_t Display_MenuAdjustUserThemeHue(int8_t delta);
/** Adjusts the user-theme brightness. */
uint8_t Display_MenuAdjustUserThemeBrightness(int8_t delta);
/** Returns true when ENC2 can toggle learn mode in the active menu context. */
uint8_t Display_MenuCanToggleLearn(void);
/** Toggles one-shot MIDI-learn mode for the active editable field. */
uint8_t Display_MenuToggleLearn(void);
/** Applies one-shot learn if armed and a new MIDI CC message arrived. */
void Display_MenuApplyMidiLearnIfPending(void);

/* -- Loading ------------------------------------------------------------------ */

/** Draws a progress bar in the lower quarter and blocks for duration_ms.
 * The optional service_hook is called from the blocking loop so startup code
 * can poll hardware or refresh small status text while the splash is visible. */
/** Draws the startup loading bar and services the supplied hook while waiting. */
void Display_LoadingBar(uint32_t duration_ms, void (*service_hook)(void));
/** Clears the loading bar area to the current theme background. */
/** Clears the startup loading-bar area. */
void Display_LoadingBarClear(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_FUNCTIONS_H */
