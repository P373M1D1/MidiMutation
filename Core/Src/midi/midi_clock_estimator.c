#include "midi/midi_clock_estimator.h"

#include "midi/midi_transport_internal.h"

#define MIDI_CLOCK_US_PER_MINUTE_X10  600000000ULL
#define MIDI_CLOCK_BPM_X10_MIN        200U
#define MIDI_CLOCK_BPM_X10_MAX        2400U
#define MIDI_CLOCK_BPM_WINDOW_MIN_PULSES      24U
#define MIDI_CLOCK_BPM_WINDOW_MOTION_PULSES   24U
#define MIDI_CLOCK_BPM_WINDOW_SHRINK_BPS      100U
#define MIDI_CLOCK_BPM_WINDOW_EXPAND_BPS      25U
#define MIDI_CLOCK_BPM_WINDOW_HOLD_PULSES     24U

static uint32_t midi_clock_pulse_intervals_us[MIDI_CLOCK_BPM_WINDOW_PULSES];
static uint8_t midi_clock_pulse_interval_index = 0U;
static uint8_t midi_clock_estimator_window_pulses = 0U;
static uint8_t midi_clock_estimator_window_hold_pulses = 0U;

__attribute__((section(".RamFunc")))
static uint32_t midi_clock_estimator_sum_recent_intervals(uint8_t interval_count)
{
    uint32_t sum_us = 0U;
    uint8_t buffer_index = midi_clock_pulse_interval_index;

    while (interval_count > 0U)
    {
        buffer_index = (buffer_index == 0U)
            ? (uint8_t)(MIDI_CLOCK_BPM_WINDOW_PULSES - 1U)
            : (uint8_t)(buffer_index - 1U);
        sum_us += midi_clock_pulse_intervals_us[buffer_index];
        interval_count--;
    }

    return sum_us;
}

__attribute__((section(".RamFunc")))
static uint8_t midi_clock_estimator_select_window_pulses(void)
{
    uint8_t available_count = midi_clock_pulse_interval_count;

    if (available_count == 0U)
    {
        midi_clock_estimator_window_pulses = 0U;
        midi_clock_estimator_window_hold_pulses = 0U;
        return 0U;
    }

    if (midi_clock_estimator_window_pulses == 0U
     || midi_clock_estimator_window_pulses > available_count)
    {
        midi_clock_estimator_window_pulses = available_count;
    }

    if (available_count >= MIDI_CLOCK_BPM_WINDOW_MOTION_PULSES)
    {
        uint32_t full_average_us;
        uint32_t recent_average_us;
        uint32_t recent_sum_us;
        uint32_t delta_us;
        uint32_t shrink_threshold_us;
        uint32_t expand_threshold_us;

        full_average_us = midi_clock_pulse_interval_sum_us / (uint32_t)available_count;
        recent_sum_us = midi_clock_estimator_sum_recent_intervals(MIDI_CLOCK_BPM_WINDOW_MOTION_PULSES);
        recent_average_us = recent_sum_us / (uint32_t)MIDI_CLOCK_BPM_WINDOW_MOTION_PULSES;
        delta_us = (recent_average_us >= full_average_us)
            ? (recent_average_us - full_average_us)
            : (full_average_us - recent_average_us);
        shrink_threshold_us = (((uint32_t)full_average_us * MIDI_CLOCK_BPM_WINDOW_SHRINK_BPS) + 5000U) / 10000U;
        expand_threshold_us = (((uint32_t)full_average_us * MIDI_CLOCK_BPM_WINDOW_EXPAND_BPS) + 5000U) / 10000U;

        if (delta_us >= shrink_threshold_us)
        {
            midi_clock_estimator_window_pulses = (available_count > MIDI_CLOCK_BPM_WINDOW_MIN_PULSES)
                ? MIDI_CLOCK_BPM_WINDOW_MIN_PULSES
                : available_count;
            midi_clock_estimator_window_hold_pulses = MIDI_CLOCK_BPM_WINDOW_HOLD_PULSES;
        }
        else if (midi_clock_estimator_window_hold_pulses > 0U)
        {
            midi_clock_estimator_window_hold_pulses--;
        }
        else if (delta_us <= expand_threshold_us
              && midi_clock_estimator_window_pulses < available_count)
        {
            midi_clock_estimator_window_pulses++;
        }
    }

    return midi_clock_estimator_window_pulses;
}

__attribute__((section(".RamFunc")))
void MidiClockEstimator_Reset(void)
{
    midi_clock_pulse_interval_index = 0U;
    midi_clock_estimator_window_pulses = 0U;
    midi_clock_estimator_window_hold_pulses = 0U;
    midi_clock_external_bpm_window_pulses = 0U;

    for (uint8_t index = 0U; index < MIDI_CLOCK_BPM_WINDOW_PULSES; index++)
        midi_clock_pulse_intervals_us[index] = 0U;
}

__attribute__((section(".RamFunc")))
void MidiClockEstimator_NotePulseInterval(uint32_t interval_us)
{
    uint8_t estimator_window_pulses;
    uint32_t estimator_window_sum_us;

    if (interval_us == 0U)
        return;

    if (midi_clock_pulse_interval_count == MIDI_CLOCK_BPM_WINDOW_PULSES)
    {
        midi_clock_pulse_interval_sum_us -=
            midi_clock_pulse_intervals_us[midi_clock_pulse_interval_index];
    }
    else
    {
        midi_clock_pulse_interval_count++;
    }

    midi_clock_pulse_intervals_us[midi_clock_pulse_interval_index] = interval_us;
    midi_clock_pulse_interval_sum_us += interval_us;
    midi_clock_pulse_interval_index =
        (uint8_t)((midi_clock_pulse_interval_index + 1U) % MIDI_CLOCK_BPM_WINDOW_PULSES);

    if (midi_clock_pulse_interval_sum_us > 0U)
    {
        estimator_window_pulses = midi_clock_estimator_select_window_pulses();
        if (estimator_window_pulses == 0U)
        {
            midi_clock_external_bpm_valid = 0U;
            midi_clock_external_bpm_window_pulses = 0U;
            return;
        }

        estimator_window_sum_us = (estimator_window_pulses == midi_clock_pulse_interval_count)
            ? midi_clock_pulse_interval_sum_us
            : midi_clock_estimator_sum_recent_intervals(estimator_window_pulses);

        if (estimator_window_sum_us == 0U)
        {
            midi_clock_external_bpm_valid = 0U;
            midi_clock_external_bpm_window_pulses = 0U;
            return;
        }

        uint64_t numerator = MIDI_CLOCK_US_PER_MINUTE_X10 * (uint64_t)estimator_window_pulses;
        uint32_t denominator = MIDI_CLOCK_PULSES_PER_QUARTER_NOTE * estimator_window_sum_us;
        uint32_t bpm_x10 = (uint32_t)((numerator + (uint64_t)(denominator / 2U)) / (uint64_t)denominator);

        if (bpm_x10 >= MIDI_CLOCK_BPM_X10_MIN && bpm_x10 <= MIDI_CLOCK_BPM_X10_MAX)
        {
            midi_clock_external_bpm_x10 = (uint16_t)bpm_x10;
            midi_clock_external_bpm_valid = 1U;
            midi_clock_external_bpm_window_pulses = estimator_window_pulses;
        }
        else
        {
            midi_clock_external_bpm_valid = 0U;
            midi_clock_external_bpm_window_pulses = 0U;
        }
    }
}