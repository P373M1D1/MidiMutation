#ifndef APP_APP_UI_H
#define APP_APP_UI_H

#include <stdint.h>
#include "presets.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Worst-case execution time (µs) per render path in AppUi_ServiceRender.
 * All fields are lifetime maxes, never reset. Used by LATENCYDIAG to isolate
 * which display operation is responsible for observed foreground stalls.
 */
typedef struct
{
    uint32_t beat_max_us;       /* beat-synchronous fast bar update       */
    uint32_t main_max_us;       /* Display_DrawMainScreen (full)           */
    uint32_t active_max_us;     /* Display_DrawMainScreen (active-preset)  */
    uint32_t live_max_us;       /* Display_RefreshMainScreenContent        */
    uint32_t edit_mode_max_us;  /* Display_RefreshPresetEditMode           */
    uint32_t edit_field_max_us; /* Display_PresetEditRefreshCurrentField   */
    uint32_t status_max_us;     /* Display_UpdateBPM                      */
} AppUiRenderTimings_t;

const Preset_t *AppUi_GetCurrentDisplayPreset(void);
uint8_t AppUi_PresetEditCurrentPresetIsEditable(void);
uint8_t AppUi_PresetEditApplyDelta(int8_t delta);
uint8_t AppUi_PresetEditEnter(void);
void AppUi_PresetEditExit(void);
uint8_t AppUi_PresetEditSendCurrentPreset(void);
uint8_t AppUi_PresetEditToggleLearningSession(void);
void AppUi_PresetEditLearningService(void);
uint8_t AppUi_PresetEditResetCurrentPresetToDefaults(void);
uint8_t AppUi_PresetEditEnterFunctionButtonEditor(void);
uint8_t AppUi_PresetEditBackOutOneLevel(void);
void AppUi_PresetEditMarkDirty(void);
uint8_t AppUi_RandomSaveCanStart(void);
uint8_t AppUi_RandomSaveIsInProgress(void);
uint8_t AppUi_RandomSaveIsAwaitingOverwriteConfirm(void);
uint8_t AppUi_RandomSaveStartSelection(void);
uint8_t AppUi_RandomSaveHandlePresetSlotPress(uint8_t slot_index);
uint8_t AppUi_RandomSaveConfirmOverwrite(void);
uint8_t AppUi_RandomSaveCancel(void);
void AppUi_RequestPresetEditModeRefresh(void);
void AppUi_RequestPresetEditFieldRefresh(void);
void AppUi_RequestStatusStripRefresh(void);
void AppUi_RequestBeatSynchronousStatusStripRefresh(void);
uint8_t AppUi_ServiceBeatSynchronousStatusStrip(void);
void AppUi_RequestActiveDisplayRefresh(void);
void AppUi_RequestLiveContentRefresh(void);
void AppUi_RequestMainScreenRefresh(void);
void AppUi_ServiceRender(void);
void AppUi_ServiceMenuPreviewHold(uint8_t encoder2_switch_pressed);
void AppUi_MenuSaveIfDirty(void);
uint8_t AppUi_MenuBackOutOneLevel(void);
uint8_t AppUi_MenuEnter(void);
uint8_t AppUi_MenuEnterMetronomeQuickAccess(void);
/* Returns lifetime worst-case per-render-path timings. */
void AppUi_GetRenderTimings(AppUiRenderTimings_t *timings);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_UI_H */