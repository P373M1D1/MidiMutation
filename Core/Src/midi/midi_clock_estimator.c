#include "midi/midi_clock_estimator.h"

#include "midi/midi_transport_internal.h"

#define MIDI_CLOCK_US_PER_MINUTE_X10  600000000ULL
#define MIDI_CLOCK_BPM_X10_MIN        200U
#define MIDI_CLOCK_BPM_X10_MAX        2400U

static uint32_t midi_clock_pulse_intervals_us[MIDI_CLOCK_BPM_WINDOW_PULSES];
static uint8_t midi_clock_pulse_interval_index = 0U;

__attribute__((section(".RamFunc")))
void MidiClockEstimator_Reset(void)
{
    midi_clock_pulse_interval_index = 0U;

    for (uint8_t index = 0U; index < MIDI_CLOCK_BPM_WINDOW_PULSES; index++)
        midi_clock_pulse_intervals_us[index] = 0U;
}

__attribute__((section(".RamFunc")))
void MidiClockEstimator_NotePulseInterval(uint32_t interval_us)
{
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
        uint64_t numerator = MIDI_CLOCK_US_PER_MINUTE_X10 * (uint64_t)midi_clock_pulse_interval_count;
        uint32_t denominator = MIDI_CLOCK_PULSES_PER_QUARTER_NOTE * midi_clock_pulse_interval_sum_us;
        uint32_t bpm_x10 = (uint32_t)((numerator + (uint64_t)(denominator / 2U)) / (uint64_t)denominator);

        if (bpm_x10 >= MIDI_CLOCK_BPM_X10_MIN && bpm_x10 <= MIDI_CLOCK_BPM_X10_MAX)
        {
            midi_clock_external_bpm_x10 = (uint16_t)bpm_x10;
            midi_clock_external_bpm_valid = 1U;
        }
        else
        {
            midi_clock_external_bpm_valid = 0U;
        }
    }
}