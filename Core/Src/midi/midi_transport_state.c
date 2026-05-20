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
static volatile uint32_t midi_quarter_service_latency_sum_us = 0U;
static volatile uint32_t midi_quarter_service_latency_max_us = 0U;
static volatile uint16_t midi_quarter_service_latency_count = 0U;

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

void MidiTransport_NoteQuarterServiceLatency(uint32_t latency_us)
{
#if MIDI_CLOCK_DIAGNOSTICS_ENABLED
    midi_quarter_service_latency_sum_us += latency_us;
    if (latency_us > midi_quarter_service_latency_max_us)
        midi_quarter_service_latency_max_us = latency_us;
    if (midi_quarter_service_latency_count < UINT16_MAX)
        midi_quarter_service_latency_count++;
#else
    (void)latency_us;
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
    MidiInputRealtimeRxDiagnostics_t realtime_rx_diag;
    uint32_t now = HAL_GetTick();
    uint32_t sum_us;
    uint32_t min_us;
    uint32_t max_us;
    uint32_t quarter_service_sum_us;
    uint32_t quarter_service_max_us;
    uint16_t count;
    uint16_t quarter_service_count;
    uint16_t bpm_x10 = 0U;
    uint8_t active;
    uint8_t running;
    uint8_t rearm_required;
    uint8_t barbeat_valid;
    uint8_t have_clock_interval_stats;
    uint8_t have_quarter_service_stats;

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
        quarter_service_sum_us = midi_quarter_service_latency_sum_us;
        quarter_service_max_us = midi_quarter_service_latency_max_us;
        quarter_service_count = midi_quarter_service_latency_count;
        running = midi_transport_running;
        rearm_required = midi_transport_rearm_required;
        barbeat_valid = midi_barbeat_valid;
        midi_clock_diag_interval_sum_us = 0U;
        midi_clock_diag_interval_min_us = UINT32_MAX;
        midi_clock_diag_interval_max_us = 0U;
        midi_clock_diag_interval_count = 0U;
        midi_quarter_service_latency_sum_us = 0U;
        midi_quarter_service_latency_max_us = 0U;
        midi_quarter_service_latency_count = 0U;
        if (primask == 0U)
            __enable_irq();
    }

    MidiInput_TakeRealtimeRxDiagnostics(&realtime_rx_diag);
    have_clock_interval_stats = (count != 0U && min_us != UINT32_MAX) ? 1U : 0U;
    have_quarter_service_stats = (quarter_service_count != 0U) ? 1U : 0U;

    if (!have_clock_interval_stats
     && !have_quarter_service_stats
     && !active
     && realtime_rx_diag.current_depth == 0U
     && realtime_rx_diag.interval_peak_depth == 0U
     && realtime_rx_diag.interval_dropped_count == 0U)
        return;

    (void)MidiClockGetExternalBpmX10(&bpm_x10);
        printf("CLKDIAG active=%u run=%u rearm=%u bb=%u samples=%u avg=%luus min=%lu max=%lu pkpk=%lu bpm=%u.%u q=%u qpk=%u qmax=%u drop=%lu/%lu lat=%lu/%luus ls=%u bsvc=%lu/%luus bs=%u\r\n",
           (unsigned)active,
            (unsigned)running,
            (unsigned)rearm_required,
            (unsigned)barbeat_valid,
           (unsigned)(have_clock_interval_stats ? count : 0U),
           (unsigned long)(have_clock_interval_stats ? (sum_us / (uint32_t)count) : 0U),
           (unsigned long)(have_clock_interval_stats ? min_us : 0U),
           (unsigned long)(have_clock_interval_stats ? max_us : 0U),
           (unsigned long)(have_clock_interval_stats ? (max_us - min_us) : 0U),
           (unsigned)(bpm_x10 / 10U),
           (unsigned)(bpm_x10 % 10U),
           (unsigned)realtime_rx_diag.current_depth,
           (unsigned)realtime_rx_diag.interval_peak_depth,
           (unsigned)realtime_rx_diag.lifetime_peak_depth,
           (unsigned long)realtime_rx_diag.interval_dropped_count,
           (unsigned long)realtime_rx_diag.total_dropped_count,
           (unsigned long)realtime_rx_diag.interval_latency_average_us,
           (unsigned long)realtime_rx_diag.interval_latency_max_us,
           (unsigned)realtime_rx_diag.interval_latency_sample_count,
           (unsigned long)(have_quarter_service_stats ? (quarter_service_sum_us / (uint32_t)quarter_service_count) : 0U),
           (unsigned long)(have_quarter_service_stats ? quarter_service_max_us : 0U),
           (unsigned)quarter_service_count);
#endif
}