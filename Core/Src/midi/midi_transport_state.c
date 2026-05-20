#include "midi/midi_transport_internal.h"

#include <stdio.h>

#define MIDI_BPM_X10_ROUNDING_OFFSET 5U

volatile uint32_t midi_clock_last_pulse_us = 0U;
volatile uint32_t midi_clock_diag_interval_sum_us = 0U;
volatile uint32_t midi_clock_diag_interval_min_us = UINT32_MAX;
volatile uint32_t midi_clock_diag_interval_max_us = 0U;
volatile uint16_t midi_clock_diag_interval_count = 0U;
volatile uint32_t midi_clock_pulse_interval_sum_us = 0U;
volatile uint8_t midi_clock_pulse_interval_count = 0U;
volatile uint32_t midi_clock_external_activity_timeout_us = 0U;
volatile uint32_t midi_clock_last_captured_pulse_us = 0U;
volatile uint16_t midi_clock_external_bpm_x10 = 0U;
volatile uint8_t midi_clock_external_bpm_valid = 0U;
volatile uint8_t midi_barbeat_valid = 0U;
volatile uint32_t midi_transport_global_tick_count = 0U;
volatile uint32_t midi_transport_origin_tick_count = 0U;
volatile uint8_t midi_clock_sync_lost = 0U;
volatile uint8_t midi_transport_running = 0U;
volatile uint8_t midi_transport_stop_latched = 0U;
volatile uint8_t midi_transport_rearm_required = 0U;
volatile MidiTransportEvent_t midi_transport_event = MIDI_TRANSPORT_EVENT_NONE;

void MidiTransport_NoteDiagnosticInterval(uint32_t interval_us)
{
#if MIDI_CLOCK_DIAGNOSTICS_ENABLED
    if (interval_us == 0U)
        return;

    if (midi_clock_diag_interval_count == 0U || interval_us < midi_clock_diag_interval_min_us)
        midi_clock_diag_interval_min_us = interval_us;

    if (interval_us > midi_clock_diag_interval_max_us)
        midi_clock_diag_interval_max_us = interval_us;

    midi_clock_diag_interval_sum_us += interval_us;
    if (midi_clock_diag_interval_count < UINT16_MAX)
        midi_clock_diag_interval_count++;
#else
    (void)interval_us;
#endif
}

uint8_t MidiTransport_IsExternalClockActive(void)
{
    uint32_t last_pulse_us = midi_clock_last_pulse_us;

    if (midi_transport_running)
        return 1U;

    if (last_pulse_us == 0U)
        return 0U;

    return (uint8_t)((TIM2->CNT - last_pulse_us) <= midi_clock_external_activity_timeout_us);
}

uint8_t MidiTransportIsRunning(void)
{
    MidiTransport_UpdateSyncState();
    return midi_transport_running;
}

uint8_t MidiClockIsSyncLost(void)
{
    MidiTransport_UpdateSyncState();
    return midi_clock_sync_lost;
}

uint8_t MidiTransportStopLatched(void)
{
    return midi_transport_stop_latched;
}

uint8_t MidiClockGetExternalBpm(uint16_t *bpm)
{
    uint16_t bpm_x10;

    if (!bpm || !MidiClockGetExternalBpmX10(&bpm_x10))
        return 0U;

    *bpm = (uint16_t)((bpm_x10 + MIDI_BPM_X10_ROUNDING_OFFSET) / 10U);
    return 1U;
}

uint8_t MidiClockGetExternalBpmX10(uint16_t *bpm_x10)
{
    MidiTransport_UpdateSyncState();

    if (!bpm_x10 || !midi_clock_external_bpm_valid || !MidiTransport_IsExternalClockActive())
        return 0U;

    *bpm_x10 = midi_clock_external_bpm_x10;
    return 1U;
}

uint8_t MidiClockIsExternalSignalPresent(void)
{
    MidiTransport_UpdateSyncState();
    return MidiTransport_IsExternalClockActive();
}

uint8_t MidiClockGetBarBeat(uint8_t *bar, uint8_t *beat)
{
    return MidiTransportCycle_GetBarBeat(bar, beat);
}

void MidiClockDiagnosticService(void)
{
#if MIDI_CLOCK_DIAGNOSTICS_ENABLED
    static uint32_t last_report_tick = 0U;
    uint32_t now = HAL_GetTick();
    uint32_t sum_us;
    uint32_t min_us;
    uint32_t max_us;
    uint16_t count;
    uint16_t bpm_x10 = 0U;
    uint8_t active;

    if ((now - last_report_tick) < MIDI_CLOCK_DIAGNOSTIC_REPORT_MS)
        return;

    last_report_tick = now;
    active = MidiClockIsExternalSignalPresent();

    {
        uint32_t primask = __get_PRIMASK();

        __disable_irq();
        sum_us = midi_clock_diag_interval_sum_us;
        min_us = midi_clock_diag_interval_min_us;
        max_us = midi_clock_diag_interval_max_us;
        count = midi_clock_diag_interval_count;
        midi_clock_diag_interval_sum_us = 0U;
        midi_clock_diag_interval_min_us = UINT32_MAX;
        midi_clock_diag_interval_max_us = 0U;
        midi_clock_diag_interval_count = 0U;
        if (primask == 0U)
            __enable_irq();
    }

    if (count == 0U || min_us == UINT32_MAX)
        return;

    (void)MidiClockGetExternalBpmX10(&bpm_x10);
    printf("CLKDIAG active=%u samples=%u avg=%luus min=%lu max=%lu pkpk=%lu bpm=%u.%u\r\n",
           (unsigned)active,
           (unsigned)count,
           (unsigned long)(sum_us / (uint32_t)count),
           (unsigned long)min_us,
           (unsigned long)max_us,
           (unsigned long)(max_us - min_us),
           (unsigned)(bpm_x10 / 10U),
           (unsigned)(bpm_x10 % 10U));
#endif
}