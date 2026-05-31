#include "midi_functions.h"
#define MIDI_TRANSPORT_INTERNAL_ACCESS 1
#include "midi/midi_transport_internal.h"
#undef MIDI_TRANSPORT_INTERNAL_ACCESS

/* External MIDI clock/transport dispatch surface.
 *
 * This module routes classified transport bytes into subsystem-owned state
 * handlers. Query/reporting APIs live in midi_transport_state.c.
 */

#define MIDI_REALTIME_CLOCK                0xF8U
#define MIDI_REALTIME_START                0xFAU
#define MIDI_REALTIME_CONTINUE             0xFBU
#define MIDI_REALTIME_STOP                 0xFCU

__attribute__((section(".RamFunc")))
uint8_t MidiTransport_HandleRealtimeByteFast(uint8_t byte, uint32_t now)
{
    switch (byte)
    {
    case MIDI_REALTIME_START:
        MidiTransport_OnStart(now);
        return 1U;

    case MIDI_REALTIME_CONTINUE:
        MidiTransport_OnContinue(now);
        return 1U;

    case MIDI_REALTIME_STOP:
        MidiTransport_OnStop(now);
        return 1U;

    case MIDI_REALTIME_CLOCK:
        if (midi_transport_rearm_required)
        {
            /* Returned clock during recovery wait is observed for diagnostics
             * and CLOCK IN indication only. Transport phase continuity cannot
             * be trusted without explicit START/CONTINUE re-anchor. */
            MidiTransport_NoteClockDuringRecoveryWait(now);
            return 1U;
        }
        MidiTransport_OnClockPulse(now);
        return 1U;

    default:
        return 0U;
    }
}

void MidiReceive(uint8_t byte)
{
    if (!MidiTransportParser_IsSyncByte(byte))
        return;

    if (byte == MIDI_REALTIME_START)
    {
        MidiTransport_OnStart(TIM2->CNT);
        return;
    }

    if (byte == MIDI_REALTIME_CONTINUE)
    {
        MidiTransport_OnContinue(TIM2->CNT);
        return;
    }

    if (byte == MIDI_REALTIME_STOP)
    {
        MidiTransport_OnStop(TIM2->CNT);
        return;
    }

    if (byte != MIDI_REALTIME_CLOCK)
        return;

    if (midi_transport_rearm_required)
    {
        /* Keep observing returned pulses but do not re-arm bar progression
         * until an explicit START/CONTINUE arrives. */
        MidiTransport_NoteClockDuringRecoveryWait(TIM2->CNT);
        return;
    }

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