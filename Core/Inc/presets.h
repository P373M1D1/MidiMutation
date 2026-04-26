#ifndef PRESETS_H
#define PRESETS_H /* include guard for preset/bank declarations */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Regular presets are grouped in banks of eight to match the numbered
 * preset footswitches. PRESET_COUNT remains the flat total used by the
 * current activation and persistence logic. */
#define PRESETS_PER_BANK     8U  /* number of numbered preset footswitch slots in each bank */
#define PRESET_BANK_COUNT    4U  /* number of banks compiled into the preset table */
#define PRESET_COUNT         (PRESETS_PER_BANK * PRESET_BANK_COUNT) /* total flat preset count across all banks */
#define PRESET_BANK_NAME_MAXLEN  16U /* maximum displayed character width reserved for a bank name */

/* Shared sentinel values used by preset data tables and activation logic. */
#define PRESET_PROGRAM_UNUSED     0xFFU /* sentinel meaning this device slot sends no Program Change */
#define PRESET_CC_CHANNEL_UNUSED  0U    /* sentinel meaning this CC slot is unused */
#define PRESET_CC_NUMBER_UNUSED   PRESET_PROGRAM_UNUSED /* sentinel CC number for an unused CC slot */
#define PRESET_RELAY_OPEN         0U    /* relay state value for open/bypass */
#define PRESET_RELAY_CLOSED       1U    /* relay state value for closed/engaged */

extern const char * const bank_names[PRESET_BANK_COUNT];
extern volatile uint8_t current_bank;

/**
 * @brief  Return the name of the given bank (0-based).
 *         Returns a fallback if out of range.
 */
const char *Presets_GetBankName(uint8_t bank);

/**
 * Number of device slots per preset.
 * Slot N maps directly to MidiDevices_Get(N) — no channel field needed here,
 * the channel lives in the device table.
 */
#define PRESET_DEVICE_SLOTS  3U /* number of per-device program slots stored in each preset */

/**
 * @brief  Per-device MIDI data for one preset.
 *
 *  program  — Program Change number to send when this preset loads (0–127).
 *             PRESET_PROGRAM_UNUSED = do not send a Program Change.
 */
typedef struct {
    uint8_t program;
} PresetDevice_t;

/** Number of extra per-preset MIDI CC messages. */
#define PRESET_CC_SLOT_COUNT  4U /* number of extra CC messages stored in each preset */

/**
 * @brief  One per-preset MIDI CC message.
 *
 *  channel    — MIDI channel to send on (1-16).
 *               PRESET_CC_CHANNEL_UNUSED = unused slot.
 *  cc_number  — CC number to send (0-127).
 *               PRESET_CC_NUMBER_UNUSED = unused slot.
 *  value      — CC value to send (0-127).
 */
typedef struct {
    uint8_t channel;
    uint8_t cc_number;
    uint8_t value;
} PresetCCSlot_t;

/** Number of independent relay outputs. */
#define PRESET_RELAY_COUNT  2U /* number of relay outputs tracked per preset */

/**
 * @brief  One preset.
 *
 *  name      — display name, max 20 chars + NUL.
 *  prg[N]    — Program Change data for device N; index matches MidiDevices_Get(N).
 *              prg[0] = Echosystem (ch1), prg[1] = Reverb (ch2), prg[2] = spare (ch3).
 *  cc[N]     — extra CC messages to send when this preset is activated.
 *  relay[N]  — state of relay N, independent of any MIDI device.
 *              PRESET_RELAY_OPEN = open/bypass,
 *              PRESET_RELAY_CLOSED = closed/engaged.
 */
typedef struct {
    char           name[21];
    PresetDevice_t prg[PRESET_DEVICE_SLOTS];
    PresetCCSlot_t cc[PRESET_CC_SLOT_COUNT];
    uint8_t        relay[PRESET_RELAY_COUNT];
} Preset_t;

/**
 * @brief  Return a pointer to the preset at @p index.
 *         Returns a pointer to a blank preset if @p index is out of range.
 */
const Preset_t *Presets_Get(uint8_t index);

/**
 * @brief  Return the number of presets defined in the table.
 */
uint8_t Presets_Count(void);

/**
 * @brief  Returns true if the given program number is used in more than one preset
 *         for the given device slot (ignores PRESET_PROGRAM_UNUSED).
 * @param  slot      Device slot index (0..PRESET_DEVICE_SLOTS-1).
 * @param  program   Program number to check (0..127).
 */
bool Presets_DeviceProgramIsShared(uint8_t slot, uint8_t program);

/**
 * @brief  Activate preset @p idx: update active state, send MIDI program
 *         changes, and refresh the display.
 */
void App_ActivatePreset(uint8_t idx);

/**
 * @brief  Activate the random preset.
 */
void Presets_ActivateRandom(void);

/**
 * @brief  Redraw the current screen after the special-functions state changes.
 */
void Presets_RedrawActiveDisplay(void);

/**
 * @brief  Activate the mute preset.
 */
void Presets_ActivateMute(void);

#ifdef __cplusplus
}
#endif

#endif /* PRESETS_H */
