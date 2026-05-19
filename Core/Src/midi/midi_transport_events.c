#include "midi/midi_transport_internal.h"

#include "midi/midi_clock_estimator.h"
#include "midi/midi_clock_internal.h"
#include "midi/midi_feedback.h"

#include "app/app_metronome.h"

void MidiTransport_ResetClockTracking(void)
{
    AppMetronome_ResetCycle();
    MidiTransportCycle_Reset();
    MidiClockEstimator_Reset();
    MidiTransport_ResetObservedState();
}

static void midi_transport_arm(MidiTransportEvent_t event)
{
    MidiFeedback_PulseTransportAnchor();
    MidiClock_ResetInternalPulseCount();
#if !MIDI_CLOCK_LOOPBACK_MONITOR_ONLY
    MidiClock_ResetOutputPhase();
#endif
    midi_transport_running = 1U;
    midi_transport_stop_latched = 0U;
    midi_transport_event = event;
    MidiTransport_ResetClockTracking();
    MidiTransportCycle_Arm();
}

void MidiTransport_OnStart(void)
{
    midi_transport_arm(MIDI_TRANSPORT_EVENT_START);
}

void MidiTransport_OnContinue(void)
{
    midi_transport_arm(MIDI_TRANSPORT_EVENT_CONTINUE);
}

void MidiTransport_OnStop(void)
{
    midi_transport_running = 0U;
    midi_transport_stop_latched = 1U;
    midi_transport_event = MIDI_TRANSPORT_EVENT_STOP;
    MidiTransport_ResetClockTracking();
}

void MidiTransport_ResetForInternalTempo(void)
{
    midi_transport_running = 0U;
    midi_transport_stop_latched = 0U;
    midi_transport_event = MIDI_TRANSPORT_EVENT_NONE;
    MidiClock_ResetInternalPulseCount();
    MidiTransport_ResetClockTracking();
}

MidiTransportEvent_t MidiTransport_TakeEvent(void)
{
    MidiTransportEvent_t event = midi_transport_event;
    midi_transport_event = MIDI_TRANSPORT_EVENT_NONE;
    return event;
}