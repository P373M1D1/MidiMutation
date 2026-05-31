#ifndef APP_MIDI_CLOCK_SYNC_H
#define APP_MIDI_CLOCK_SYNC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void AppMidiClock_TrackExternalPulseInterval(uint32_t interval_us);

#ifdef __cplusplus
}
#endif

#endif /* APP_MIDI_CLOCK_SYNC_H */