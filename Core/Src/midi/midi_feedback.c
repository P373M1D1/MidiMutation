#include "midi/midi_feedback.h"

#include "led_functions.h"

void MidiFeedback_PulseTransportAnchorAt(uint32_t timestamp_us)
{
    LED_MidiClockPulseAtUs(timestamp_us);
    LED_MidiInPulse();
}

void MidiFeedback_PulseExternalClockBeatAt(uint32_t timestamp_us)
{
    LED_MidiClockPulseAtUs(timestamp_us);
}

void MidiFeedback_PulseInternalBeatAt(uint32_t timestamp_us)
{
    LED_BeatPulseAtUs(timestamp_us);
}