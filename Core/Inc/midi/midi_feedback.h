#ifndef MIDI_FEEDBACK_H
#define MIDI_FEEDBACK_H

#ifdef __cplusplus
extern "C" {
#endif

void MidiFeedback_PulseTransportAnchor(void);
void MidiFeedback_PulseExternalClockBeat(void);
void MidiFeedback_PulseInternalBeat(void);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_FEEDBACK_H */