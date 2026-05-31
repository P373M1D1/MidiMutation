#define MIDI_TRANSPORT_INTERNAL_ACCESS 1
#include "midi/midi_transport_internal.h"
#undef MIDI_TRANSPORT_INTERNAL_ACCESS

#include "midi/midi_clock_estimator.h"
#include "midi/midi_clock_internal.h"
#include "midi/midi_feedback.h"

#include "app/app_metronome.h"
#include "app/app_ui.h"

__attribute__((section(".RamFunc")))
void MidiTransport_ResetClockTracking(void)
{
    AppMetronome_ResetCycle();
    MidiTransportCycle_Reset();
    MidiClockEstimator_Reset();
    MidiTransport_ResetObservedState();
}

__attribute__((section(".RamFunc")))
static void midi_transport_reset_cycle_tracking_only(void)
{
    AppMetronome_ResetCycle();
    MidiTransportCycle_Reset();
    midi_clock_sync_lost = 0U;
    MidiTransport_ClearRecoveryHint();
}

__attribute__((section(".RamFunc")))
static void midi_transport_arm(MidiTransportEvent_t event, uint32_t now)
{
    uint8_t preserve_observed_clock = 0U;

    if (!MidiTransport_IsFlashBusyFast())
        MidiFeedback_PulseTransportAnchorAt(now);
    MidiClock_ResetInternalPulseCount();
#if !MIDI_CLOCK_LOOPBACK_MONITOR_ONLY
    MidiClock_ResetOutputPhase();
#endif

    if (midi_clock_pulse_interval_count != 0U
     && midi_clock_last_pulse_us != 0U
     && midi_clock_external_activity_timeout_us != 0U
     && (now - midi_clock_last_pulse_us) <= midi_clock_external_activity_timeout_us)
    {
        preserve_observed_clock = 1U;
    }

    midi_transport_rearm_required = 0U;
    midi_transport_running = 1U;
    midi_transport_stop_latched = 0U;
    midi_transport_event = event;

    /* Preserve pre-roll external clock timing so Start/Continue does not
     * throw away a stable estimator window right before transport arms. */
    if (preserve_observed_clock)
        midi_transport_reset_cycle_tracking_only();
    else
        MidiTransport_ResetClockTracking();

    MidiTransportCycle_Arm();
    AppMetronome_OnQuarterNoteAtCount(APP_METRONOME_SOURCE_EXTERNAL, now, 0U);
    AppUi_RequestStatusStripRefresh();
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
void MidiTransport_OnStop(uint32_t now)
{
    MidiClock_HandoffExternalPhaseToInternal(now);
    MidiTransport_ClearRecoveryHint();
    midi_transport_rearm_required = 1U;
    midi_transport_running = 0U;
    midi_transport_stop_latched = 0U;
    midi_transport_event = MIDI_TRANSPORT_EVENT_NONE;
    midi_clock_last_pulse_us = 0U;
    midi_clock_last_captured_pulse_us = 0U;
    midi_clock_external_bpm_valid = 0U;
    midi_clock_external_bpm_window_pulses = 0U;
    midi_clock_sync_lost = 0U;
}

__attribute__((section(".RamFunc")))
void MidiTransport_ResetForInternalTempo(void)
{
    MidiClock_HandoffExternalPhaseToInternal(TIM2->CNT);
    MidiTransport_ClearRecoveryHint();
    midi_transport_rearm_required = 0U;
    midi_transport_running = 0U;
    midi_transport_stop_latched = 0U;
    midi_transport_event = MIDI_TRANSPORT_EVENT_NONE;
    MidiTransport_ResetClockTracking();
}

MidiTransportEvent_t MidiTransport_TakeEvent(void)
{
    MidiTransportEvent_t event = midi_transport_event;
    midi_transport_event = MIDI_TRANSPORT_EVENT_NONE;
    return event;
}