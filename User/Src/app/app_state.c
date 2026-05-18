#include "app/app_state.h"
#include "bpm_functions.h"
#include <stddef.h>

volatile uint16_t g_bpm = BPM_DEFAULT;
volatile uint8_t bpm_dirty = 0U;
volatile uint32_t bpm_save_tick = 0U;
const Preset_t *active_preset = NULL;
uint8_t active_preset_index = PRESET_DEFAULT;