#include "presets.h"
#include "button_functions.h"
#include "display_functions.h"
#include "midi_devices.h"
#include "midi_functions.h"
#include "bpm_functions.h"
#include "stm32f4xx_hal.h"

/* ── Bank names ─────────────────────────────────────────────────────────────
 * You can change these to any theme: "A/B", "Clean/Dirty", etc. */
#define PRESET_BANK_INVALID_NAME      "(bank?)" /* fallback name returned for an out-of-range bank index */
#define PRESET_CC(channel, cc_number, value)  {channel, cc_number, value} /* helper for writing compact CC slot literals */
/* Shared "send nothing" CC list used by synthetic presets that exist only as
 * safe fallbacks or runtime overlays, not as real pedal-program recall data. */
#define PRESET_CC_EMPTY                       PRESET_CC(PRESET_CC_CHANNEL_UNUSED, PRESET_CC_NUMBER_UNUSED, 0U) /* one unused CC-slot initializer */
#define PRESET_CC_LIST_EMPTY                  { PRESET_CC_EMPTY, PRESET_CC_EMPTY, PRESET_CC_EMPTY, PRESET_CC_EMPTY } /* four-slot initializer for presets with no extra CC messages */
#define PRESET_RANDOM_LCG_SEED                0x6D2B79F5UL /* initial state for the random-preset pseudo-random generator */
#define PRESET_RANDOM_LCG_MULTIPLIER          1664525UL /* LCG multiplier used when generating random preset programs */
#define PRESET_RANDOM_LCG_INCREMENT           1013904223UL /* LCG increment used when generating random preset programs */

const char * const bank_names[PRESET_BANK_COUNT] = {
    "[Strain I]",
    "[Strain II]",
    "[Strain III]",
    "[Strain IV]"
};

const char *Presets_GetBankName(uint8_t bank)
{
    if (bank < PRESET_BANK_COUNT)
        return bank_names[bank];
    return PRESET_BANK_INVALID_NAME;
}

/* ── Application state owned by main.cpp ─────────────────────────────────── */
extern volatile uint16_t  g_bpm;
extern volatile uint32_t  bpm_save_tick;
extern const Preset_t    *active_preset;
extern uint8_t            active_preset_index;
volatile uint8_t          current_bank = 0U;

/* ── Preset table ────────────────────────────────────────────────────────────
 *
 * Each row: { "Name", {prg...}, {cc slots...}, {relay1, relay2} }
 *
 * Presets are stored banked so bank 2 / preset 1 is visually distinct in the
 * source from bank 1 / preset 1. External code still uses flat indices for now.
 *
 *   prg[0] → Empress Echosystem  (channel 1 on the shared MIDI out)
 *   prg[1] → Empress Reverb      (channel 2 on the shared MIDI out)
 *   prg[2] → spare               (channel 3 reserved)
 *
 *   pg      = Program Change number to send on load  (0xFF = skip)
 *   cc[N]   = extra CC messages sent on preset load (channel 0 / cc 0xFF = skip)
 *   relay   = relay state  0=open/bypass  1=closed/engaged
 *
 * ─────────────────────────────────────────────────────────────────────────── */
//     name                  Echosystem  Reverb  spare      cc1..cc4               relay1 relay2
static const Preset_t preset_table[PRESET_BANK_COUNT][PRESETS_PER_BANK] = {
    {
        { "Soft Reverb",	{{11}, {11}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{1, 0} },
        { "Perfect Tape",	{{ 7}, {11}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{1, 0} },
        { "Deep Cave",	{{11}, {12}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "Press Tap to Hold",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "Stars at Night",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "Fade to Pad",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "Empty Preset",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "Empty Preset",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
    },
    {
        { "Yoooo",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "Paaaaaaaa ",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "triiickkk!!",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "was ",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "geeeeeeeeeht",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "aaaaaaaabb!!",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "Preset 7",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "Preset 8",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
    },
    {
        { "und",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "ey",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "...",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "external tempo geht",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "sogar mit error",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "woohooo!!",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "Preset 7",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "Preset 8",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
    },
    {
        { "Preset 1",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "Preset 2",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "Preset 3",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "Preset 4",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "Preset 5",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "Preset 6",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "Preset 7",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
        { "Preset 8",	{{0xFF}, {0xFF}, {0xFF}},	{{0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}, {0U, 0xFFU, 0U}},	{0, 0} },
    },
};

/* Fallback returned when index is out of range.
 * Every per-device program slot is explicitly marked PRESET_PROGRAM_UNUSED
 * because Preset_t has no single "disabled" flag for the whole preset: each
 * device slot must say "send nothing" on its own. Keeping CCs empty and both
 * relays open makes an out-of-range lookup visually harmless and side-effect free.
 */
static const Preset_t blank_preset = {
    .name  = "---",
    .prg   = { {PRESET_PROGRAM_UNUSED}, {PRESET_PROGRAM_UNUSED}, {PRESET_PROGRAM_UNUSED} },
    .cc    = PRESET_CC_LIST_EMPTY,
    .relay = { PRESET_RELAY_OPEN, PRESET_RELAY_OPEN },
};

/* Runtime-built shell used by Presets_ActivateRandom().
 * It also starts fully "unused" for the same reason: until Presets_ActivateRandom()
 * fills the real device slots, this preset must not accidentally send stale
 * Program Changes, CCs, or relay changes. The spare slot remains unused even
 * after generation because random mode currently mutates only the real pedals.
 */
static Preset_t random_preset = {
    .name  = "Mutate Preset",
    .prg   = { {PRESET_PROGRAM_UNUSED}, {PRESET_PROGRAM_UNUSED}, {PRESET_PROGRAM_UNUSED} },
    .cc    = PRESET_CC_LIST_EMPTY,
    .relay = { PRESET_RELAY_OPEN, PRESET_RELAY_OPEN },
};

/* Overlay preset used by Presets_ActivateMute().
 * Mute should not recall pedal patches or fire extra CCs, so every device slot
 * stays PRESET_PROGRAM_UNUSED and the CC list is empty. Both relays default to
 * open/bypass so engaging mute leaves the hardware path in the safest neutral state.
 */
static const Preset_t mute_preset = {
    .name  = "Mute / Bypass",
    .prg   = { {PRESET_PROGRAM_UNUSED}, {PRESET_PROGRAM_UNUSED}, {PRESET_PROGRAM_UNUSED} },
    .cc    = PRESET_CC_LIST_EMPTY,
    .relay = { PRESET_RELAY_OPEN, PRESET_RELAY_OPEN },
};

/* -------------------------------------------------------------------------- */

/* Shared activation path for normal presets, random preset, and mute preset.
 * update_index controls whether this activation should become the persisted
 * "current preset" or just temporarily repaint/run an overlay preset. */
static void App_ActivatePresetData(const Preset_t *preset, uint8_t update_index, uint8_t idx)
{
    if (!preset)
        return;

    Button_ResetSpecialFunctions();
    Display_ScreensaverDismiss();

    if (update_index)
        active_preset_index = idx;

    active_preset = preset;
    Midi_LoadPreset(active_preset);
    Display_DrawMainScreen(active_preset, g_bpm);
    Display_ScreensaverActivity();

    if (update_index) {
        bpm_save_tick = HAL_GetTick() + BPM_SAVE_DELAY_MS;
    }
}

static uint8_t Presets_NextRandomProgram(uint8_t max_preset)
{
    /* Simple on-device LCG mixed with the current HAL tick so successive
     * button presses do not walk the exact same short sequence after boot. */
    static uint32_t random_state = PRESET_RANDOM_LCG_SEED;

    random_state = (random_state * PRESET_RANDOM_LCG_MULTIPLIER)
                 + PRESET_RANDOM_LCG_INCREMENT
                 + HAL_GetTick();
    return (uint8_t)(random_state % ((uint32_t)max_preset + 1UL));
}

static const Preset_t *Presets_GetFlat(uint8_t index)
{
    /* Presets are stored banked for readability in the source table, but most
     * runtime code still addresses them through a flat 0..PRESET_COUNT-1 index. */
    uint8_t bank = index / PRESETS_PER_BANK;
    uint8_t slot = index % PRESETS_PER_BANK;

    if (bank >= PRESET_BANK_COUNT)
        return &blank_preset;

    return &preset_table[bank][slot];
}

/* -------------------------------------------------------------------------- */

bool Presets_DeviceProgramIsShared(uint8_t slot, uint8_t program)
{
    if (program == PRESET_PROGRAM_UNUSED || slot >= PRESET_DEVICE_SLOTS)
        return false;

    uint8_t count = 0U;
    for (uint8_t bank = 0U; bank < PRESET_BANK_COUNT; bank++)
    {
        for (uint8_t preset = 0U; preset < PRESETS_PER_BANK; preset++)
        {
            if (preset_table[bank][preset].prg[slot].program == program)
                count++;

            if (count > 1U)
                return true;
        }
    }

    return false;
}

/* -------------------------------------------------------------------------- */

const Preset_t *Presets_Get(uint8_t index)
{
    if (index >= PRESET_COUNT)
        return &blank_preset;
    return Presets_GetFlat(index);
}

uint8_t Presets_Count(void)
{
    return PRESET_COUNT;
}

/* -------------------------------------------------------------------------- */

void App_ActivatePreset(uint8_t idx)
{
    const Preset_t *preset = NULL;

    if (idx >= Presets_Count())
        return;

    preset = Presets_Get(idx);
    if (active_preset == preset)
        return;

    App_ActivatePresetData(preset, 1U, idx);
}

void Presets_ActivateRandom(void)
{
    const MidiDevice_t *first_device = MidiDevices_Get(0U);
    const MidiDevice_t *second_device = MidiDevices_Get(1U);

    /* Random preset picks one legal program per real device and leaves the
     * spare slot intentionally unused. */
    random_preset.prg[0].program = Presets_NextRandomProgram(first_device->max_preset);
    random_preset.prg[1].program = Presets_NextRandomProgram(second_device->max_preset);
    random_preset.prg[2].program = PRESET_PROGRAM_UNUSED;

    App_ActivatePresetData(&random_preset, 0U, 0U);
}

void Presets_RedrawActiveDisplay(void)
{
    /* Special-functions state lives in button_functions.c; presets only need
     * to redraw the current screen so the right-side status text changes. */
    if (active_preset)
        Display_DrawMainScreen(active_preset, g_bpm);
}

void Presets_ActivateMute(void)
{
    App_ActivatePresetData(&mute_preset, 0U, 0U);
}
