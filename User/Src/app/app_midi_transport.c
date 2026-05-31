#include "app/app_midi_transport.h"

#include "app/app_state.h"
#include "runtime_config.h"

/* Returns the current bar-count setting for MIDI clock cycles. */
uint8_t AppMidiTransport_GetBarsPerCycle(void)
{
    const RuntimeConfigBank_t *bank = RuntimeConfig_GetBank(AppState_GetCurrentBank());
    uint8_t bars_per_cycle = bank->midi_clock_bar_count;

    if (bars_per_cycle < RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MIN
     || bars_per_cycle > RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MAX)
        return RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_DEFAULT;

    return bars_per_cycle;
}