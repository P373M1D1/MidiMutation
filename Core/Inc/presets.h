#ifndef PRESETS_H
#define PRESETS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of presets in the table.
 * Sized for future banks – 16 slots = 2 banks of 8. */
#define PRESET_COUNT         16U

/**
 * Number of device slots per preset.
 * Slot N maps directly to MidiDevices_Get(N) — no channel field needed here,
 * the channel lives in the device table.
 */
#define PRESET_DEVICE_SLOTS  3U

/**
 * @brief  Per-device MIDI data for one preset.
 *
 *  program  — Program Change number to send when this preset loads (0–127).
 *             0xFF = do not send a Program Change to this device.
 */
typedef struct {
    uint8_t program;
} PresetDevice_t;

/** Number of independent relay outputs. */
#define PRESET_RELAY_COUNT  3U

/**
 * @brief  One preset.
 *
 *  name      — display name, max 20 chars + NUL.
 *  dev[N]    — MIDI data for device N; index matches MidiDevices_Get(N).
 *              dev[0] = Echosystem (ch1), dev[1] = Reverb (ch2), dev[2] = spare.
 *  relay[N]  — state of relay N, independent of any MIDI device.
 *              0 = open (bypass), 1 = closed (engaged).
 */
typedef struct {
    char           name[21];
    PresetDevice_t dev[PRESET_DEVICE_SLOTS];
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

#ifdef __cplusplus
}
#endif

#endif /* PRESETS_H */
