#define MIDI_TRANSPORT_INTERNAL_ACCESS 1
#include "midi/midi_transport_internal.h"
#undef MIDI_TRANSPORT_INTERNAL_ACCESS

#define MIDI_STATUS_BIT              0x80U
#define MIDI_TIMECODE_QUARTER_FRAME  0xF1U
#define MIDI_REALTIME_CLOCK          0xF8U
#define MIDI_REALTIME_START          0xFAU
#define MIDI_REALTIME_CONTINUE       0xFBU
#define MIDI_REALTIME_STOP           0xFCU
#define MIDI_REALTIME_STATUS_FIRST   0xF8U

static uint8_t midi_input_expect_timecode_data = 0U;

uint8_t MidiTransportParser_IsSyncByte(uint8_t byte)
{
    if (byte == MIDI_REALTIME_CLOCK
     || byte == MIDI_REALTIME_START
     || byte == MIDI_REALTIME_CONTINUE
     || byte == MIDI_REALTIME_STOP)
    {
        return 1U;
    }

    if (byte == MIDI_TIMECODE_QUARTER_FRAME)
    {
        midi_input_expect_timecode_data = 1U;
        return 1U;
    }

    if (midi_input_expect_timecode_data)
    {
        if (byte >= MIDI_REALTIME_STATUS_FIRST)
            return 0U;

        midi_input_expect_timecode_data = 0U;
        return (uint8_t)((byte & MIDI_STATUS_BIT) == 0U);
    }

    return 0U;
}