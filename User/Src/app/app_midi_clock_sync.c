#include "app/app_midi_clock_sync.h"

#include "app/app_state.h"
#include "bpm_functions.h"
#include "midi_functions.h"

void AppMidiClock_TrackExternalPulseInterval(uint32_t interval_us)
{
    uint64_t denominator;
    uint32_t external_bpm;

    if (interval_us == 0U)
        return;

    denominator = (uint64_t)interval_us * (uint64_t)MIDI_CLOCK_PULSES_PER_QUARTER_NOTE;
    external_bpm = (uint32_t)((60000000ULL + (denominator / 2ULL)) / denominator);

    if (external_bpm < BPM_MIN || external_bpm > BPM_MAX)
        return;

    AppState_SetTempoBpm((uint16_t)external_bpm);
}