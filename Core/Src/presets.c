#include "presets.h"
#include "display_functions.h"
#include "midi_functions.h"
#include "bpm_functions.h"
#include "stm32f4xx_hal.h"
#include <string.h>

/* ── Application state owned by main.cpp ─────────────────────────────────── */
extern volatile uint16_t  g_bpm;
extern volatile uint32_t  bpm_save_tick;
extern const Preset_t    *active_preset;
extern uint8_t            active_preset_index;

/* ── Preset table ────────────────────────────────────────────────────────────
 *
 * Each row: { "Name", {ech_pg, ech_rel}, {rev_pg, rev_rel}, {spare} }
 *
 *   dev[0] → Empress Echosystem  (port 0, UART4)
 *   dev[1] → Empress Reverb      (port 1, UART5)
 *   dev[2] → spare
 *
 *   pg  = Program Change number to send on load  (0xFF = skip)
 *   rel = relay state  0=open/bypass  1=closed/engaged
 *
 * ─────────────────────────────────────────────────────────────────────────── */
//     name                  Echosystem  Reverb  spare   relay1 relay2 relay3
//                             ch1 pg ch2 pg  pg      r1  r2  r3
static const Preset_t preset_table[PRESET_COUNT] = {
    { "Soft Reverb",          {{ 11}, {11}, {0xFF}},  {1, 0, 0} },
    { "Perfect Tape",         {{ 7}, {11}, {0xFF}},  {1, 0, 0} },
    { "Deep Cave",            {{11},{12},{0xFF}}, {0, 0, 0} },
    { "Tap to Freeze",        {{0xFF},{0xFF},{0xFF}}, {0, 0, 0} },
    { "Preset 05",            {{0xFF},{0xFF},{0xFF}}, {0, 0, 0} },
    { "Preset 06",            {{0xFF},{0xFF},{0xFF}}, {0, 0, 0} },
    { "Preset 07",            {{0xFF},{0xFF},{0xFF}}, {0, 0, 0} },
    { "Preset 08",            {{0xFF},{0xFF},{0xFF}}, {0, 0, 0} },
    { "Preset 09",            {{0xFF},{0xFF},{0xFF}}, {0, 0, 0} },
    { "Preset 10",            {{0xFF},{0xFF},{0xFF}}, {0, 0, 0} },
    { "Preset 11",            {{0xFF},{0xFF},{0xFF}}, {0, 0, 0} },
    { "Preset 12",            {{0xFF},{0xFF},{0xFF}}, {0, 0, 0} },
    { "Preset 13",            {{0xFF},{0xFF},{0xFF}}, {0, 0, 0} },
    { "Preset 14",            {{0xFF},{0xFF},{0xFF}}, {0, 0, 0} },
    { "Preset 15",            {{0xFF},{0xFF},{0xFF}}, {0, 0, 0} },
    { "Preset 16",            {{0xFF},{0xFF},{0xFF}}, {0, 0, 0} },
};

/* Fallback returned when index is out of range */
static const Preset_t blank_preset = {
    .name  = "---",
    .dev   = { {0xFFU}, {0xFFU}, {0xFFU} },
    .relay = { 0U, 0U, 0U },
};

/* -------------------------------------------------------------------------- */

const Preset_t *Presets_Get(uint8_t index)
{
    if (index >= PRESET_COUNT)
        return &blank_preset;
    return &preset_table[index];
}

uint8_t Presets_Count(void)
{
    return PRESET_COUNT;
}

/* -------------------------------------------------------------------------- */

void App_ActivatePreset(uint8_t idx)
{
    if (idx >= Presets_Count())
        return;

    active_preset_index = idx;
    active_preset = Presets_Get(idx);
    Midi_LoadPreset(active_preset);
    Display_DrawMainScreen(active_preset, g_bpm);
    Display_ScreensaverActivity();
    bpm_save_tick = HAL_GetTick() + BPM_SAVE_DELAY_MS;
}
