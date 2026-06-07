#define MIDI_TRANSPORT_INTERNAL_ACCESS 1
#include "midi/midi_transport_internal.h"
#undef MIDI_TRANSPORT_INTERNAL_ACCESS

#include "midi/midi_clock_estimator.h"
#include "midi/midi_clock_internal.h"
#include "midi/midi_feedback.h"

#include "app/app_metronome.h"
#include "app/app_ui.h"

#define MIDI_TRANSPORT_DIAG_EVENT_QUEUE_DEPTH 8U

static MidiTransportDiagnosticEvent_t midi_transport_diag_event_queue[MIDI_TRANSPORT_DIAG_EVENT_QUEUE_DEPTH];
static volatile uint8_t midi_transport_diag_event_head = 0U;
static volatile uint8_t midi_transport_diag_event_tail = 0U;
static volatile uint8_t midi_transport_diag_event_count = 0U;
static volatile uint32_t midi_transport_diag_event_overflow_count = 0U;

__attribute__((section(".RamFunc")))
static void midi_transport_enqueue_diagnostic_event(MidiTransportEvent_t event,
                                                    uint32_t now,
                                                    uint8_t preserve_observed_clock)
{
    uint8_t tail;

    if (midi_transport_diag_event_count >= MIDI_TRANSPORT_DIAG_EVENT_QUEUE_DEPTH)
    {
        midi_transport_diag_event_head =
            (uint8_t)((midi_transport_diag_event_head + 1U) % MIDI_TRANSPORT_DIAG_EVENT_QUEUE_DEPTH);
        midi_transport_diag_event_count--;
        if (midi_transport_diag_event_overflow_count < UINT32_MAX)
            midi_transport_diag_event_overflow_count++;
    }

    tail = midi_transport_diag_event_tail;
    midi_transport_diag_event_queue[tail].event = event;
    midi_transport_diag_event_queue[tail].timestamp_us = now;
    midi_transport_diag_event_queue[tail].running = midi_transport_running;
    midi_transport_diag_event_queue[tail].rearm_required = midi_transport_rearm_required;
    midi_transport_diag_event_queue[tail].stop_latched = midi_transport_stop_latched;
    midi_transport_diag_event_queue[tail].preserve_observed_clock = preserve_observed_clock;
    midi_transport_diag_event_tail =
        (uint8_t)((midi_transport_diag_event_tail + 1U) % MIDI_TRANSPORT_DIAG_EVENT_QUEUE_DEPTH);
    midi_transport_diag_event_count++;
}

__attribute__((section(".RamFunc")))
void MidiTransport_ResetClockTracking(void)
{
    MidiTransport_ClearNextClockIntervalSkip();
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
    {
        midi_transport_reset_cycle_tracking_only();
        MidiTransport_SkipNextClockInterval();
    }
    else
    {
        MidiTransport_ResetClockTracking();
    }

    MidiTransportCycle_Arm();
    AppMetronome_OnQuarterNoteAtCount(APP_METRONOME_SOURCE_EXTERNAL, now, 0U);
    AppUi_RequestStatusStripRefresh();
    midi_transport_enqueue_diagnostic_event(event, now, preserve_observed_clock);
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
    midi_transport_stop_latched = 1U;
    midi_transport_event = MIDI_TRANSPORT_EVENT_STOP;
    midi_clock_last_pulse_us = 0U;
    midi_clock_last_captured_pulse_us = 0U;
    midi_clock_external_bpm_valid = 0U;
    midi_clock_external_bpm_window_pulses = 0U;
    midi_clock_sync_lost = 0U;
    midi_transport_enqueue_diagnostic_event(MIDI_TRANSPORT_EVENT_STOP, now, 0U);
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

uint8_t MidiTransport_TakeDiagnosticEvent(MidiTransportDiagnosticEvent_t *event)
{
    uint32_t primask;
    uint8_t head;

    if (!event)
        return 0U;

    primask = __get_PRIMASK();
    __disable_irq();
    if (midi_transport_diag_event_count == 0U)
    {
        if (primask == 0U)
            __enable_irq();
        return 0U;
    }

    head = midi_transport_diag_event_head;
    *event = midi_transport_diag_event_queue[head];
    midi_transport_diag_event_head =
        (uint8_t)((midi_transport_diag_event_head + 1U) % MIDI_TRANSPORT_DIAG_EVENT_QUEUE_DEPTH);
    midi_transport_diag_event_count--;
    if (primask == 0U)
        __enable_irq();

    return 1U;
}

uint32_t MidiTransport_GetDiagnosticEventOverflowCount(void)
{
    return midi_transport_diag_event_overflow_count;
}
