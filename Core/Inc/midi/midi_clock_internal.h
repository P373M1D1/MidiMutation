#ifndef MIDI_CLOCK_INTERNAL_H
#define MIDI_CLOCK_INTERNAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIDI_CLOCK_LOOPBACK_MONITOR_ONLY 1U

void MidiClock_ResetInternalPulseCount(void);
void MidiClock_AlignInternalPhaseToExternal(uint32_t now_us,
											uint32_t last_pulse_us,
											uint32_t external_pulse_count);

#if !MIDI_CLOCK_LOOPBACK_MONITOR_ONLY
void MidiClock_ResetOutputPhase(void);
void MidiClock_TrackExternalPulseInterval(uint32_t interval_us);
#endif

#ifdef __cplusplus
}
#endif

#endif /* MIDI_CLOCK_INTERNAL_H */