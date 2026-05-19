#include "midi/midi_transport_internal.h"

#include "midi/midi_clock_estimator.h"
#include "midi/midi_clock_internal.h"
#include "midi/midi_feedback.h"

#include "app/app_metronome.h"

#define MIDI_TIMER_WRAP_VALUE  UINT32_MAX

static void midi_transport_resync_clock(uint32_t now);
static void midi_transport_anchor_first_clock_pulse(void);
static uint32_t midi_transport_clock_interval_us(uint32_t now, uint32_t previous_pulse_us);
static void midi_transport_note_clock_interval(uint32_t interval_us);

void MidiTransport_OnClockPulse(uint32_t now)
{
    if (midi_clock_sync_lost)
    {
        midi_transport_resync_clock(now);
        return;
    }

    if (midi_clock_last_pulse_us == 0U)
    {
        midi_transport_anchor_first_clock_pulse();
    }
    else if (now != midi_clock_last_pulse_us)
    {
        midi_transport_note_clock_interval(
            midi_transport_clock_interval_us(now, midi_clock_last_pulse_us));
    }

    midi_clock_last_pulse_us = now;
    midi_clock_external_activity_timeout_us =
        MidiTransport_ComputeActivityTimeoutUs(midi_clock_pulse_interval_sum_us,
                                               midi_clock_pulse_interval_count);

    if (!MidiTransportCycle_OnClockPulse())
        return;

    MidiFeedback_PulseExternalClockBeat();
    AppMetronome_OnQuarterNote(APP_METRONOME_SOURCE_EXTERNAL);
}

static void midi_transport_resync_clock(uint32_t now)
{
    MidiTransport_ResetClockTracking();
    midi_transport_running = 1U;
    midi_clock_last_pulse_us = now;

#if !MIDI_CLOCK_LOOPBACK_MONITOR_ONLY
    MidiClock_ResetOutputPhase();
#endif
}

static void midi_transport_anchor_first_clock_pulse(void)
{
#if !MIDI_CLOCK_LOOPBACK_MONITOR_ONLY
    MidiClock_ResetOutputPhase();
#endif
}

static uint32_t midi_transport_clock_interval_us(uint32_t now, uint32_t previous_pulse_us)
{
    return (now >= previous_pulse_us)
        ? (now - previous_pulse_us)
        : (MIDI_TIMER_WRAP_VALUE - previous_pulse_us + now + 1U);
}

static void midi_transport_note_clock_interval(uint32_t interval_us)
{
    MidiClockEstimator_NotePulseInterval(interval_us);
    MidiTransport_NoteDiagnosticInterval(interval_us);

#if !MIDI_CLOCK_LOOPBACK_MONITOR_ONLY
    MidiClock_TrackExternalPulseInterval(interval_us);
#endif
}