#include "midi_devices.h"

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
static const MidiDevice_t device_table[MIDI_DEVICE_COUNT] = {
    /* 0 ── Empress Echosystem ── shared MIDI out, channel 1 ──────────────── */
    {
        .channel    = 1U,
        .engage     = { .cc = 60U, .value = 127U },
        .bypass     = { .cc = 60U, .value = 0U   },
        .tap_tempo  = { .cc = 35U, .value = 64U  },
        .max_preset = 35U,
    },
    /* 1 ── Empress Reverb ──── shared MIDI out, channel 2 ───────────────── */
    /*  Engage/Bypass  CC 60 — 127 = engage, 0 = bypass                     */
    /*  Left Stomp     CC 35 — value 64 = quick tap / select pulse           */
    /*  Recall Preset  CC 11 — value = preset number (0–35)                  */
    {
        .channel    = 2U,
        .engage     = { .cc = 60U, .value = 127U },
        .bypass     = { .cc = 60U, .value = 0U   },
        .tap_tempo  = { .cc = 35U, .value = 64U  },
        .max_preset = 35U,
    },
    /* 2–7 blank slots — reserve unique channels for future devices ──────── INFO: Channel Numbers have only been added to check if the scrolling works*/
    { .channel = 3U, .engage = {0xFFU,0U}, .bypass = {0xFFU,0U}, .tap_tempo = {0xFFU,0U}, .max_preset = 127U },
    { .channel = 4U, .engage = {0xFFU,0U}, .bypass = {0xFFU,0U}, .tap_tempo = {0xFFU,0U}, .max_preset = 127U },
    { .channel = 5U, .engage = {0xFFU,0U}, .bypass = {0xFFU,0U}, .tap_tempo = {0xFFU,0U}, .max_preset = 127U },
    { .channel = 6U, .engage = {0xFFU,0U}, .bypass = {0xFFU,0U}, .tap_tempo = {0xFFU,0U}, .max_preset = 127U },
    { .channel = 7U, .engage = {0xFFU,0U}, .bypass = {0xFFU,0U}, .tap_tempo = {0xFFU,0U}, .max_preset = 127U },
    { .channel = 8U, .engage = {0xFFU,0U}, .bypass = {0xFFU,0U}, .tap_tempo = {0xFFU,0U}, .max_preset = 127U },
};

/* Fallback returned when index is out of range */
static const MidiDevice_t blank_device = {
    .channel    = 0U,
    .engage     = { 0xFFU, 0U },
    .bypass     = { 0xFFU, 0U },
    .tap_tempo  = { 0xFFU, 0U },
    .max_preset = 0U,
};

/* -------------------------------------------------------------------------- */

const MidiDevice_t *MidiDevices_Get(uint8_t index)
{
    if (index >= MIDI_DEVICE_COUNT)
        return &blank_device;
    return &device_table[index];
}

uint8_t MidiDevices_Count(void)
{
    return MIDI_DEVICE_COUNT;
}
