#include "midi/midi_feedback.h"

#include "led_functions.h"

void MidiFeedback_PulseTransportAnchor(void)
{
    LED_MidiClockPulse();
    LED_MidiInPulse();
}

void MidiFeedback_PulseExternalClockBeat(void)
{
    LED_MidiClockPulse();
}

void MidiFeedback_PulseInternalBeat(void)
{
    LED_BeatPulse();
}