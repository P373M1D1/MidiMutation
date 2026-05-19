#ifndef MIDI_CLOCK_ESTIMATOR_H
#define MIDI_CLOCK_ESTIMATOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void MidiClockEstimator_Reset(void);
void MidiClockEstimator_NotePulseInterval(uint32_t interval_us);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_CLOCK_ESTIMATOR_H */