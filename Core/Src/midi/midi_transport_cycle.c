#include "midi/midi_transport_internal.h"

#include "app/app_midi_transport.h"

#define MIDI_BARBEAT_BEATS_PER_BAR  4U

static uint8_t midi_clock_pulse_count = 0U;

static void midi_transport_cycle_advance_quarter_note(void);

void MidiTransportCycle_Reset(void)
{
    midi_clock_pulse_count = 0U;
}

void MidiTransportCycle_Arm(void)
{
    midi_barbeat_valid = 1U;
}

uint8_t MidiTransportCycle_OnClockPulse(void)
{
    midi_clock_pulse_count++;
    if (midi_clock_pulse_count < MIDI_CLOCK_PULSES_PER_QUARTER_NOTE)
        return 0U;

    midi_clock_pulse_count = 0U;
    midi_transport_cycle_advance_quarter_note();
    return 1U;
}

static void midi_transport_cycle_advance_quarter_note(void)
{
    if (!midi_barbeat_valid)
        return;

    if (midi_barbeat_beat < MIDI_BARBEAT_BEATS_PER_BAR)
    {
        midi_barbeat_beat++;
        return;
    }

    {
        uint8_t bars_per_cycle = AppMidiTransport_GetBarsPerCycle();

        midi_barbeat_beat = 1U;
        midi_barbeat_bar = (midi_barbeat_bar < bars_per_cycle)
            ? (uint8_t)(midi_barbeat_bar + 1U)
            : 1U;
    }
}