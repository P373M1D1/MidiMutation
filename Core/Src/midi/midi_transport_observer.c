#include "midi/midi_transport_internal.h"

static void MidiTransport_GetTimingSnapshot(uint32_t *last_pulse_us,
                                            uint32_t *last_captured_pulse_us,
                                            uint32_t *pulse_interval_sum_us,
                                            uint8_t *pulse_interval_count);

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
    midi_clock_sync_lost = 0U;
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
        midi_transport_rearm_required = 1U;
        midi_transport_running = 0U;
        midi_clock_external_bpm_valid = 0U;
        midi_clock_external_bpm_window_pulses = 0U;
        midi_clock_last_pulse_us = 0U;
        midi_clock_last_captured_pulse_us = 0U;
        midi_clock_sync_lost = 0U;
    }
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