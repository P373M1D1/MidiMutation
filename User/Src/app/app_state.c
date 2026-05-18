#include "app/app_state.h"
#include "bpm_functions.h"
#include <stddef.h>

volatile uint16_t g_bpm = BPM_DEFAULT;
volatile uint32_t bpm_save_tick = 0U;
const Preset_t *active_preset = NULL;
uint8_t active_preset_index = PRESET_DEFAULT;
volatile uint8_t current_bank = 0U;

uint16_t AppState_GetTempoBpm(void)
{
	return g_bpm;
}

void AppState_SetTempoBpm(uint16_t bpm)
{
	g_bpm = bpm;
}

uint32_t AppState_GetRuntimeStateSaveTick(void)
{
	return bpm_save_tick;
}

void AppState_ScheduleRuntimeStateSaveAt(uint32_t save_tick)
{
	bpm_save_tick = save_tick;
}

void AppState_ClearRuntimeStateSaveSchedule(void)
{
	bpm_save_tick = 0U;
}

uint8_t AppState_GetCurrentBank(void)
{
	return current_bank;
}

void AppState_SelectBank(uint8_t bank_index)
{
	current_bank = bank_index;
}

const Preset_t *AppState_GetActivePreset(void)
{
	return active_preset;
}

uint8_t AppState_IsActivePreset(const Preset_t *preset)
{
	return (active_preset == preset) ? 1U : 0U;
}

void AppState_ActivatePresetSelection(const Preset_t *preset, uint8_t preset_index)
{
	active_preset = preset;
	active_preset_index = preset_index;
}

void AppState_SetActiveOverlayPreset(const Preset_t *preset)
{
	active_preset = preset;
}

void AppState_SetActivePreset(const Preset_t *preset)
{
	active_preset = preset;
}

uint8_t AppState_GetActivePresetIndex(void)
{
	return active_preset_index;
}

void AppState_SetActivePresetIndex(uint8_t preset_index)
{
	active_preset_index = preset_index;
}