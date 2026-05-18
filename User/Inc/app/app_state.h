#ifndef APP_APP_STATE_H
#define APP_APP_STATE_H

#include <stdint.h>
#include "presets.h"

#ifdef __cplusplus
extern "C" {
#endif

extern volatile uint16_t g_bpm;
extern volatile uint8_t bpm_dirty;
extern volatile uint32_t bpm_save_tick;
extern const Preset_t *active_preset;
extern uint8_t active_preset_index;

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_STATE_H */