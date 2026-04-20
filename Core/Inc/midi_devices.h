#ifndef MIDI_DEVICES_H
#define MIDI_DEVICES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of devices in the table */
#define MIDI_DEVICE_COUNT  8U

/**
 * @brief  CC message descriptor – a controller number paired with the value
 *         to send when that action is triggered.
 */
typedef struct {
    uint8_t cc;     /* Controller number (0–127); 0xFF = not supported */
    uint8_t value;  /* Value to transmit (0–127)                        */
} MidiCC_t;

/**
 * @brief  One MIDI device.
 *
 *  channel         — MIDI channel (1–16)
 *  engage          — CC + value sent to engage/activate the device
 *  bypass          — CC + value sent to bypass/deactivate the device
 *  tap_tempo       — CC + value sent on each tap (value 64 = quick tap pulse
 *                    on Empress devices; 0xFF cc = not supported)
 *  max_preset      — highest Program Change number the device accepts
 *                    (used by any future random-preset function)
 */
typedef struct {
    uint8_t  midi_port;   /* Index passed to MIDI_InitPort() / MIDI_SendXxx() */
    uint8_t  channel;
    MidiCC_t engage;
    MidiCC_t bypass;
    MidiCC_t tap_tempo;
    uint8_t  max_preset;
} MidiDevice_t;

/**
 * @brief  Return a pointer to the device at @p index.
 *         Returns a pointer to a blank device if @p index is out of range.
 */
const MidiDevice_t *MidiDevices_Get(uint8_t index);

/**
 * @brief  Return the number of devices defined in the table.
 */
uint8_t MidiDevices_Count(void);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_DEVICES_H */
