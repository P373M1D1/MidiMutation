#ifndef APP_APP_STATE_H
#define APP_APP_STATE_H

#include <stdint.h>
#include "presets.h"

#ifdef __cplusplus
extern "C" {
#endif

extern volatile uint16_t g_bpm;
extern volatile uint32_t bpm_save_tick;
extern const Preset_t *active_preset;
extern uint8_t active_preset_index;
extern volatile uint8_t current_bank;

uint16_t AppState_GetTempoBpm(void);
void AppState_SetTempoBpm(uint16_t bpm);
uint32_t AppState_GetRuntimeStateSaveTick(void);
void AppState_ScheduleRuntimeStateSaveAt(uint32_t save_tick);
void AppState_ClearRuntimeStateSaveSchedule(void);
uint8_t AppState_GetCurrentBank(void);
void AppState_SelectBank(uint8_t bank_index);
const Preset_t *AppState_GetActivePreset(void);
uint8_t AppState_IsActivePreset(const Preset_t *preset);
void AppState_ActivatePresetSelection(const Preset_t *preset, uint8_t preset_index);
void AppState_SetActiveOverlayPreset(const Preset_t *preset);
void AppState_SetActivePreset(const Preset_t *preset);
uint8_t AppState_GetActivePresetIndex(void);
void AppState_SetActivePresetIndex(uint8_t preset_index);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_STATE_H */