#define MIDI_TRANSPORT_INTERNAL_ACCESS 1
#include "midi/midi_transport_internal.h"
#undef MIDI_TRANSPORT_INTERNAL_ACCESS
#include "midi/midi_clock_internal.h"

#define MIDI_CLOCK_RECOVERY_TEMPO_MATCH_BPS 100U
#define MIDI_CLOCK_RECOVERY_TEMPO_MATCH_MIN_US 100U
#define MIDI_CLOCK_RECOVERY_FILTER_DIVISOR 4U

static volatile uint32_t midi_transport_recovery_probe_last_pulse_us = 0U;
static volatile uint32_t midi_transport_recovery_probe_interval_us = 0U;

static void MidiTransport_GetTimingSnapshot(uint32_t *last_pulse_us,
                                            uint32_t *last_captured_pulse_us,
                                            uint32_t *pulse_interval_sum_us,
                                            uint8_t *pulse_interval_count);
static uint32_t MidiTransport_RecoveryIntervalUs(uint32_t now, uint32_t previous_pulse_us);
static uint32_t MidiTransport_RecoveryAbsDeltaU32(uint32_t a, uint32_t b);

__attribute__((section(".RamFunc")))
uint32_t MidiTransport_ComputeActivityTimeoutUs(uint32_t pulse_interval_sum_us,
                                                uint8_t pulse_interval_count)
{
    uint32_t timeout_us = (uint32_t)MIDI_CLOCK_LOST_TIMEOUT_MIN_MS * MIDI_CLOCK_US_PER_MS;

    if (pulse_interval_count > 0U)
    {
        uint32_t average_pulse_us = pulse_interval_sum_us / (uint32_t)pulse_interval_count;
        timeout_us = average_pulse_us * MIDI_CLOCK_LOST_TIMEOUT_PULSES;
        timeout_us += (uint32_t)MIDI_CLOCK_LOST_TIMEOUT_PAD_MS * MIDI_CLOCK_US_PER_MS;

        uint32_t min_timeout_us = (uint32_t)MIDI_CLOCK_LOST_TIMEOUT_MIN_MS * MIDI_CLOCK_US_PER_MS;
        if (timeout_us < min_timeout_us)
            timeout_us = min_timeout_us;
    }

    return timeout_us;
}

__attribute__((section(".RamFunc")))
void MidiTransport_ClearRecoveryHint(void)
{
    midi_clock_recovery_hint = (uint8_t)MIDI_CLOCK_RECOVERY_HINT_NONE;
    midi_transport_recovery_probe_last_pulse_us = 0U;
    midi_transport_recovery_probe_interval_us = 0U;
}

__attribute__((section(".RamFunc")))
void MidiTransport_ResetObservedState(void)
{
    midi_clock_last_pulse_us = 0U;
    midi_clock_pulse_interval_sum_us = 0U;
    midi_clock_pulse_interval_count = 0U;
    midi_clock_external_activity_timeout_us =
        (uint32_t)MIDI_CLOCK_LOST_TIMEOUT_MIN_MS * MIDI_CLOCK_US_PER_MS;
    midi_clock_external_bpm_x10 = 0U;
    midi_clock_external_bpm_valid = 0U;
    midi_clock_external_bpm_window_pulses = 0U;
    midi_barbeat_valid = 0U;
    midi_transport_origin_tick_count = midi_transport_global_tick_count;
    midi_transport_last_quarter_note_count = 0U;
    midi_transport_quarter_note_event_count = 0U;
    midi_transport_last_quarter_note_anchor_us = 0U;
    midi_clock_sync_lost = 0U;
    MidiTransport_ClearRecoveryHint();
}

__attribute__((section(".RamFunc")))
void MidiTransport_NoteClockDuringRecoveryWait(uint32_t now)
{
    uint32_t returned_interval_us;
    uint32_t internal_interval_us;
    uint32_t tempo_delta_us;
    uint32_t tempo_tolerance_us;

    if (!midi_transport_rearm_required || !midi_clock_sync_lost)
        return;

    if (midi_transport_recovery_probe_last_pulse_us == 0U
     || now == midi_transport_recovery_probe_last_pulse_us)
    {
        midi_transport_recovery_probe_last_pulse_us = now;
        return;
    }

    returned_interval_us = MidiTransport_RecoveryIntervalUs(now,
                                                            midi_transport_recovery_probe_last_pulse_us);
    midi_transport_recovery_probe_last_pulse_us = now;
    if (returned_interval_us == 0U)
        return;

    if (midi_transport_recovery_probe_interval_us == 0U)
    {
        midi_transport_recovery_probe_interval_us = returned_interval_us;
    }
    else
    {
        int32_t step = (int32_t)(returned_interval_us - midi_transport_recovery_probe_interval_us)
            / (int32_t)MIDI_CLOCK_RECOVERY_FILTER_DIVISOR;

        if (step == 0)
            step = (returned_interval_us > midi_transport_recovery_probe_interval_us) ? 1 : -1;

        midi_transport_recovery_probe_interval_us =
            (uint32_t)((int32_t)midi_transport_recovery_probe_interval_us + step);
    }

    internal_interval_us = MidiClock_GetOutputPulseIntervalUs();
    tempo_delta_us = MidiTransport_RecoveryAbsDeltaU32(midi_transport_recovery_probe_interval_us,
                                                       internal_interval_us);
    tempo_tolerance_us = (uint32_t)((((uint64_t)internal_interval_us * MIDI_CLOCK_RECOVERY_TEMPO_MATCH_BPS)
        + 5000ULL) / 10000ULL);
    if (tempo_tolerance_us < MIDI_CLOCK_RECOVERY_TEMPO_MATCH_MIN_US)
        tempo_tolerance_us = MIDI_CLOCK_RECOVERY_TEMPO_MATCH_MIN_US;

    midi_clock_recovery_hint = (uint8_t)((tempo_delta_us > tempo_tolerance_us)
        ? MIDI_CLOCK_RECOVERY_HINT_BPM_DELTA
        : MIDI_CLOCK_RECOVERY_HINT_PHASE_DELTA);
}

uint8_t MidiTransport_IsRecoveryClockActive(void)
{
    uint32_t last_probe_us = midi_transport_recovery_probe_last_pulse_us;
    uint32_t timeout_us = midi_clock_external_activity_timeout_us;

    if (!midi_transport_rearm_required || !midi_clock_sync_lost || last_probe_us == 0U)
        return 0U;

    if (timeout_us == 0U)
        timeout_us = (uint32_t)MIDI_CLOCK_LOST_TIMEOUT_MIN_MS * MIDI_CLOCK_US_PER_MS;

    return (uint8_t)((TIM2->CNT - last_probe_us) <= timeout_us);
}

void MidiTransport_UpdateSyncState(void)
{
    uint32_t timeout_us;
    uint32_t last_pulse_us;
    uint32_t last_captured_pulse_us;
    uint32_t pulse_interval_sum_us;
    uint8_t pulse_interval_count;

    if (!midi_transport_running || midi_clock_sync_lost || midi_transport_rearm_required)
        return;

    MidiTransport_GetTimingSnapshot(&last_pulse_us,
                                    &last_captured_pulse_us,
                                    &pulse_interval_sum_us,
                                    &pulse_interval_count);
    if (last_pulse_us == 0U || pulse_interval_count == 0U)
        return;

    timeout_us = MidiTransport_ComputeActivityTimeoutUs(pulse_interval_sum_us, pulse_interval_count);

    if ((TIM2->CNT - last_captured_pulse_us) > timeout_us)
    {
        MidiClock_HandoffExternalPhaseToInternal(TIM2->CNT);
        MidiTransport_ClearRecoveryHint();
        midi_transport_rearm_required = 1U;
        midi_transport_running = 0U;
        midi_clock_external_bpm_valid = 0U;
        midi_clock_external_bpm_window_pulses = 0U;
        midi_clock_last_pulse_us = 0U;
        midi_clock_last_captured_pulse_us = 0U;
        midi_clock_sync_lost = 1U;
    }
}

static uint32_t MidiTransport_RecoveryIntervalUs(uint32_t now, uint32_t previous_pulse_us)
{
    return (now >= previous_pulse_us)
        ? (now - previous_pulse_us)
        : (UINT32_MAX - previous_pulse_us + now + 1U);
}

static uint32_t MidiTransport_RecoveryAbsDeltaU32(uint32_t a, uint32_t b)
{
    return (a >= b) ? (a - b) : (b - a);
}

static void MidiTransport_GetTimingSnapshot(uint32_t *last_pulse_us,
                                            uint32_t *last_captured_pulse_us,
                                            uint32_t *pulse_interval_sum_us,
                                            uint8_t *pulse_interval_count)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    *last_pulse_us = midi_clock_last_pulse_us;
    *last_captured_pulse_us = midi_clock_last_captured_pulse_us;
    *pulse_interval_sum_us = midi_clock_pulse_interval_sum_us;
    *pulse_interval_count = midi_clock_pulse_interval_count;
    if (primask == 0U)
        __enable_irq();
}