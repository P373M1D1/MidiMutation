#include "midi_devices.h"
#include "runtime_config.h"

/* ── Device table ────────────────────────────────────────────────────────────
 *
 * Empress Echosystem MIDI CC reference (firmware 2.42):
 *
 *   Engage / Bypass  CC 60  — value 127 = engage, value 0 = bypass
 *   Left Stomp (Tap) CC 35  — value 64 = quick tap pulse
 *   Recall Preset    CC 11  — value = preset number (0–35)
 *   Max presets      35     — (0-indexed, so 36 slots total)
 *
 * channel: set to match the MIDI channel configured on the pedal itself.
 * All devices share the same physical MIDI output; the channel is now the
 * only routing distinction between devices.
 *
 * ─────────────────────────────────────────────────────────────────────────── */
static MidiDevice_t runtime_device_cache[MIDI_DEVICE_COUNT];

/* Fallback returned when index is out of range */
static const MidiDevice_t blank_device = {
    .channel    = 0U,
    .engage     = { 0xFFU, 0U },
    .bypass     = { 0xFFU, 0U },
    .tap_tempo  = { 0xFFU, 0U },
    .max_preset = 0U,
};

static void MidiDevices_SyncCacheEntry(uint8_t index)
{
    const RuntimeConfigDevice_t *config = RuntimeConfig_GetDevice(index);

    runtime_device_cache[index].channel = config->channel;
    runtime_device_cache[index].engage = config->active;
    runtime_device_cache[index].bypass = config->bypass;
    runtime_device_cache[index].tap_tempo = config->tap_tempo;
    runtime_device_cache[index].max_preset = config->max_preset;
}

/* -------------------------------------------------------------------------- */

const MidiDevice_t *MidiDevices_Get(uint8_t index)
{
    if (index >= MIDI_DEVICE_COUNT)
        return &blank_device;

    MidiDevices_SyncCacheEntry(index);
    return &runtime_device_cache[index];
}

const char *MidiDevices_GetName(uint8_t index)
{
    return RuntimeConfig_GetDevice(index)->name;
}

uint8_t MidiDevices_Count(void)
{
    return MIDI_DEVICE_COUNT;
}
