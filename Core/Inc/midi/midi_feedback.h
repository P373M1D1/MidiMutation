#ifndef MIDI_FEEDBACK_H
#define MIDI_FEEDBACK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void MidiFeedback_PulseTransportAnchorAt(uint32_t timestamp_us);
void MidiFeedback_PulseExternalClockBeatAt(uint32_t timestamp_us);
void MidiFeedback_PulseInternalBeatAt(uint32_t timestamp_us);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_FEEDBACK_H */