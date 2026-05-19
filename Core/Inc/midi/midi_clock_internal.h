#ifndef MIDI_CLOCK_INTERNAL_H
#define MIDI_CLOCK_INTERNAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIDI_CLOCK_LOOPBACK_MONITOR_ONLY 1U

void MidiClock_ResetInternalPulseCount(void);

#if !MIDI_CLOCK_LOOPBACK_MONITOR_ONLY
void MidiClock_ResetOutputPhase(void);
void MidiClock_TrackExternalPulseInterval(uint32_t interval_us);
#endif

#ifdef __cplusplus
}
#endif

#endif /* MIDI_CLOCK_INTERNAL_H */