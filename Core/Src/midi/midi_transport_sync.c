#define MIDI_TRANSPORT_INTERNAL_ACCESS 1
#include "midi/midi_transport_internal.h"
#undef MIDI_TRANSPORT_INTERNAL_ACCESS

#include "midi/midi_clock_estimator.h"
#include "midi/midi_clock_internal.h"
#include "midi/midi_feedback.h"

#include "app/app_metronome.h"
#include "app/app_ui.h"

#define MIDI_TIMER_WRAP_VALUE  UINT32_MAX

static volatile uint8_t midi_transport_skip_next_clock_interval = 0U;

__attribute__((section(".RamFunc")))
static void midi_transport_resync_clock(uint32_t now);
__attribute__((section(".RamFunc")))
static void midi_transport_anchor_first_clock_pulse(uint32_t now);
__attribute__((section(".RamFunc")))
static uint32_t midi_transport_clock_interval_us(uint32_t now, uint32_t previous_pulse_us);
__attribute__((section(".RamFunc")))
static void midi_transport_note_clock_interval(uint32_t now, uint32_t interval_us);

__attribute__((section(".RamFunc")))
void MidiTransport_SkipNextClockInterval(void)
{
    midi_transport_skip_next_clock_interval = 1U;
}

__attribute__((section(".RamFunc")))
void MidiTransport_ClearNextClockIntervalSkip(void)
{
    midi_transport_skip_next_clock_interval = 0U;
}

__attribute__((section(".RamFunc")))
void MidiTransport_OnClockPulse(uint32_t now)
{
    uint32_t quarter_note_anchor_us = now;
    uint32_t quarter_note_count;
    MidiClockEstimatorStatus_t estimator_status;

    if (midi_clock_sync_lost)
    {
        midi_transport_resync_clock(now);
        return;
    }

    if (midi_clock_last_pulse_us == 0U)
    {
        midi_transport_anchor_first_clock_pulse(now);
    }
    else if (midi_transport_skip_next_clock_interval)
    {
        midi_transport_skip_next_clock_interval = 0U;
        midi_transport_anchor_first_clock_pulse(now);
    }
    else if (now != midi_clock_last_pulse_us)
    {
        midi_transport_note_clock_interval(
            now,
            midi_transport_clock_interval_us(now, midi_clock_last_pulse_us));
    }

    midi_clock_last_pulse_us = now;
    midi_clock_external_activity_timeout_us =
        MidiTransport_ComputeActivityTimeoutUs(midi_clock_pulse_interval_sum_us,
                                               midi_clock_pulse_interval_count);

    if (!MidiTransportCycle_OnClockPulse())
        return;

    MidiTransport_NoteQuarterServiceLatency(TIM2->CNT - now);

    quarter_note_count = (midi_transport_global_tick_count - midi_transport_origin_tick_count)
        / MIDI_CLOCK_PULSES_PER_QUARTER_NOTE;
    midi_transport_last_quarter_note_count = quarter_note_count;
    midi_transport_last_quarter_note_anchor_us = quarter_note_anchor_us;
    if (midi_transport_quarter_note_event_count < UINT32_MAX)
        midi_transport_quarter_note_event_count++;
    /* During ACQUIRE/TRACKING the recovered pulse timestamp can move as the
     * estimator settles, which makes beat feedback appear double-triggered or
     * jittery. Use recovered anchor only once lock/publication are ready. */
    MidiClockEstimator_GetStatus(&estimator_status);
    if (estimator_status.live_lock == MIDI_CLOCK_LOCK_QUALITY_LOCKED
     && estimator_status.publication_ready)
    {
        (void)MidiClockEstimator_GetRecoveredPulseTimestampUs(&quarter_note_anchor_us);
    }

    if (!MidiTransport_IsFlashBusyFast())
        MidiFeedback_PulseExternalClockBeatAt(quarter_note_anchor_us);
    AppMetronome_OnQuarterNoteAtCount(APP_METRONOME_SOURCE_EXTERNAL,
                                      quarter_note_anchor_us,
                                      quarter_note_count);
    AppUi_RequestBeatSynchronousStatusStripRefresh();
}

__attribute__((section(".RamFunc")))
static void midi_transport_resync_clock(uint32_t now)
{
    MidiTransport_ResetClockTracking();
    midi_transport_running = 1U;
    midi_clock_last_pulse_us = now;
    MidiClockEstimator_AnchorPulse(now);
}

__attribute__((section(".RamFunc")))
static void midi_transport_anchor_first_clock_pulse(uint32_t now)
{
    MidiClockEstimator_AnchorPulse(now);
}

__attribute__((section(".RamFunc")))
static uint32_t midi_transport_clock_interval_us(uint32_t now, uint32_t previous_pulse_us)
{
    return (now >= previous_pulse_us)
        ? (now - previous_pulse_us)
        : (MIDI_TIMER_WRAP_VALUE - previous_pulse_us + now + 1U);
}

__attribute__((section(".RamFunc")))
static void midi_transport_note_clock_interval(uint32_t now, uint32_t interval_us)
{
    MidiClockEstimator_NotePulseInterval(now, interval_us);
    if (!MidiTransport_IsFlashBusyFast())
        MidiTransport_NoteDiagnosticInterval(interval_us);
}
