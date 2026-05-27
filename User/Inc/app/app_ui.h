#ifndef APP_APP_UI_H
#define APP_APP_UI_H

#include <stdint.h>
#include "presets.h"

#ifdef __cplusplus
extern "C" {
#endif

const Preset_t *AppUi_GetCurrentDisplayPreset(void);
uint8_t AppUi_PresetEditCurrentPresetIsEditable(void);
uint8_t AppUi_PresetEditApplyDelta(int8_t delta);
uint8_t AppUi_PresetEditEnter(void);
void AppUi_PresetEditExit(void);
uint8_t AppUi_PresetEditSendCurrentPreset(void);
uint8_t AppUi_PresetEditResetCurrentPresetToDefaults(void);
uint8_t AppUi_PresetEditEnterFunctionButtonEditor(void);
uint8_t AppUi_PresetEditBackOutOneLevel(void);
void AppUi_PresetEditMarkDirty(void);
void AppUi_RequestPresetEditModeRefresh(void);
void AppUi_RequestPresetEditFieldRefresh(void);
void AppUi_RequestStatusStripRefresh(void);
void AppUi_RequestActiveDisplayRefresh(void);
void AppUi_RequestLiveContentRefresh(void);
void AppUi_RequestMainScreenRefresh(void);
void AppUi_ServiceRender(void);
void AppUi_ServiceMenuPreviewHold(uint8_t encoder2_switch_pressed);
void AppUi_MenuSaveIfDirty(void);
uint8_t AppUi_MenuBackOutOneLevel(void);
uint8_t AppUi_MenuEnter(void);
uint8_t AppUi_MenuEnterMetronomeQuickAccess(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_UI_H */