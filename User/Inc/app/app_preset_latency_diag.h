#ifndef APP_APP_PRESET_LATENCY_DIAG_H
#define APP_APP_PRESET_LATENCY_DIAG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void AppPresetLatencyDiag_OnPresetButtonPress(uint8_t button_index,
                                              uint8_t preset_index,
                                              uint32_t tick_ms);
void AppPresetLatencyDiag_OnEnc2PresetStep(int8_t delta,
                                           uint8_t preset_index,
                                           uint32_t tick_ms);
void AppPresetLatencyDiag_OnRandomButtonPress(uint8_t button_index,
                                              uint32_t tick_ms);
void AppPresetLatencyDiag_OnMuteButtonPress(uint8_t button_index,
                                            uint32_t tick_ms);
void AppPresetLatencyDiag_OnActivationStart(void);
void AppPresetLatencyDiag_OnActivationApplied(void);
void AppPresetLatencyDiag_OnOverlayResolved(uint8_t is_mute_overlay);
void AppPresetLatencyDiag_OnLedIndicatorUpdated(void);
void AppPresetLatencyDiag_OnDisplayRefreshComplete(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_PRESET_LATENCY_DIAG_H */
