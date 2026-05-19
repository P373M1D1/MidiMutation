#include "midi_functions.h"
#include "midi/midi_transport_internal.h"

/* External MIDI clock/transport dispatch surface.
 *
 * This module routes classified transport bytes into subsystem-owned state
 * handlers. Query/reporting APIs live in midi_transport_state.c.
 */

#define MIDI_REALTIME_CLOCK                0xF8U
#define MIDI_REALTIME_START                0xFAU
#define MIDI_REALTIME_CONTINUE             0xFBU
#define MIDI_REALTIME_STOP                 0xFCU

void MidiReceive(uint8_t byte)
{
    if (!MidiTransportParser_IsSyncByte(byte))
        return;

    if (byte == MIDI_REALTIME_START)
    {
        MidiTransport_OnStart();
        return;
    }

    if (byte == MIDI_REALTIME_CONTINUE)
    {
        MidiTransport_OnContinue();
        return;
    }

    if (byte == MIDI_REALTIME_STOP)
    {
        MidiTransport_OnStop();
        return;
    }

    if (byte != MIDI_REALTIME_CLOCK)
        return;

    MidiTransport_OnClockPulse(TIM2->CNT);
}

void MidiClockUseInternalTempo(void)
{
    MidiTransport_ResetForInternalTempo();
}

MidiTransportEvent_t MidiTransportConsumeEvent(void)
{
    return MidiTransport_TakeEvent();
} 