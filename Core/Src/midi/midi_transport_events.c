#include "midi/midi_transport_internal.h"

#include "midi/midi_clock_estimator.h"
#include "midi/midi_clock_internal.h"
#include "midi/midi_feedback.h"

#include "app/app_metronome.h"

__attribute__((section(".RamFunc")))
void MidiTransport_ResetClockTracking(void)
{
    AppMetronome_ResetCycle();
    MidiTransportCycle_Reset();
    MidiClockEstimator_Reset();
    MidiTransport_ResetObservedState();
}

__attribute__((section(".RamFunc")))
static void midi_transport_arm(MidiTransportEvent_t event, uint32_t now)
{
    if (!MidiTransport_IsFlashBusyFast())
        MidiFeedback_PulseTransportAnchor();
    MidiClock_ResetInternalPulseCount();
#if !MIDI_CLOCK_LOOPBACK_MONITOR_ONLY
    MidiClock_ResetOutputPhase();
#endif
    midi_transport_rearm_required = 0U;
    midi_transport_running = 1U;
    midi_transport_stop_latched = 0U;
    midi_transport_event = event;
    MidiTransport_ResetClockTracking();
    MidiTransportCycle_Arm();
    AppMetronome_OnQuarterNoteAt(APP_METRONOME_SOURCE_EXTERNAL, now);
}

__attribute__((section(".RamFunc")))
void MidiTransport_OnStart(uint32_t now)
{
    midi_transport_arm(MIDI_TRANSPORT_EVENT_START, now);
}

__attribute__((section(".RamFunc")))
void MidiTransport_OnContinue(uint32_t now)
{
    midi_transport_arm(MIDI_TRANSPORT_EVENT_CONTINUE, now);
}

__attribute__((section(".RamFunc")))
void MidiTransport_OnStop(void)
{
    midi_transport_rearm_required = 1U;
    midi_transport_running = 0U;
    midi_transport_stop_latched = 0U;
    midi_transport_event = MIDI_TRANSPORT_EVENT_NONE;
    midi_clock_last_pulse_us = 0U;
    midi_clock_last_captured_pulse_us = 0U;
    midi_clock_external_bpm_valid = 0U;
    midi_clock_sync_lost = 0U;
}

__attribute__((section(".RamFunc")))
void MidiTransport_ResetForInternalTempo(void)
{
    midi_transport_rearm_required = 0U;
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