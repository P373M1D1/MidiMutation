#define MIDI_TRANSPORT_INTERNAL_ACCESS 1
#include "midi/midi_transport_internal.h"
#undef MIDI_TRANSPORT_INTERNAL_ACCESS

#include "app/app_state.h"

#include "runtime_config.h"

#define MIDI_BARBEAT_BEATS_PER_BAR  4U

__attribute__((section(".RamFunc")))
void MidiTransportCycle_Reset(void)
{
    midi_transport_origin_tick_count = midi_transport_global_tick_count;
}

__attribute__((section(".RamFunc")))
void MidiTransportCycle_Arm(void)
{
    midi_transport_origin_tick_count = midi_transport_global_tick_count;
    midi_barbeat_valid = 1U;
}

__attribute__((section(".RamFunc")))
uint8_t MidiTransportCycle_OnClockPulse(void)
{
    uint32_t relative_tick_count;

    midi_transport_global_tick_count++;

    if (!midi_barbeat_valid)
        return 0U;

    relative_tick_count = midi_transport_global_tick_count - midi_transport_origin_tick_count;
    if (relative_tick_count == 0U)
        return 0U;

    return ((relative_tick_count % MIDI_CLOCK_PULSES_PER_QUARTER_NOTE) == 0U) ? 1U : 0U;
}

uint8_t MidiTransportCycle_GetBarBeat(uint8_t *bar, uint8_t *beat)
{
    uint32_t primask = __get_PRIMASK();
    uint32_t total_tick_count;
    uint32_t origin_tick_count;
    uint32_t quarter_note_count;
    uint8_t valid;
    uint8_t bars_per_cycle;

    if (!bar || !beat)
        return 0U;

    __disable_irq();
    valid = midi_barbeat_valid;
    total_tick_count = midi_transport_global_tick_count;
    origin_tick_count = midi_transport_origin_tick_count;
    if (primask == 0U)
        __enable_irq();

    if (!valid)
        return 0U;

    quarter_note_count = (total_tick_count - origin_tick_count) / MIDI_CLOCK_PULSES_PER_QUARTER_NOTE;
    bars_per_cycle = RuntimeConfig_GetMidiClockBarCountFast(current_bank);

    *beat = (uint8_t)((quarter_note_count % MIDI_BARBEAT_BEATS_PER_BAR) + 1U);
    *bar = (uint8_t)(((quarter_note_count / MIDI_BARBEAT_BEATS_PER_BAR) % bars_per_cycle) + 1U);

    return 1U;
}