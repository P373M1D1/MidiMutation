#include "midi/midi_transport_internal.h"

#include "midi/midi_clock_estimator.h"
#include "midi/midi_clock_internal.h"

#include <stdio.h>

#define MIDI_BPM_X10_ROUNDING_OFFSET 5U

static uint32_t midi_transport_phase_elapsed_nonnegative_us(uint32_t now_us,
                                                            uint32_t pulse_us);
static uint16_t midi_transport_phase_fraction_q16(uint32_t numerator,
                                                  uint32_t denominator);
static uint16_t midi_transport_phase_fraction_milli(uint16_t fraction_q16);

static uint32_t midi_transport_phase_elapsed_nonnegative_us(uint32_t now_us,
                                                            uint32_t pulse_us)
{
    int32_t elapsed_us = (int32_t)(now_us - pulse_us);

    return (elapsed_us > 0) ? (uint32_t)elapsed_us : 0U;
}

static uint16_t midi_transport_phase_fraction_q16(uint32_t numerator,
                                                  uint32_t denominator)
{
    uint64_t fraction_q16;

    if (numerator == 0U || denominator == 0U)
        return 0U;

    fraction_q16 = (((uint64_t)numerator << 16) + ((uint64_t)denominator / 2ULL))
        / (uint64_t)denominator;
    if (fraction_q16 > 0xFFFFULL)
        fraction_q16 = 0xFFFFULL;

    return (uint16_t)fraction_q16;
}

static uint16_t midi_transport_phase_fraction_milli(uint16_t fraction_q16)
{
    uint32_t milli = ((((uint32_t)fraction_q16 * 1000U) + 32768U) >> 16);

    return (milli >= 1000U) ? 999U : (uint16_t)milli;
}

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
volatile uint8_t midi_clock_external_bpm_window_pulses = 0U;
volatile uint8_t midi_barbeat_valid = 0U;
volatile uint32_t midi_transport_global_tick_count = 0U;
volatile uint32_t midi_transport_origin_tick_count = 0U;
volatile uint8_t midi_clock_sync_lost = 0U;
volatile uint8_t midi_clock_recovery_hint = (uint8_t)MIDI_CLOCK_RECOVERY_HINT_NONE;
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

    if (MidiTransport_IsRecoveryClockActive())
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

uint8_t MidiClockIsEstimatorValid(void)
{
    MidiClockEstimatorStatus_t status;

    MidiTransport_UpdateSyncState();
    MidiClockEstimator_GetStatus(&status);
    return status.estimator_valid;
}

MidiClockLockQuality_t MidiClockGetLockQuality(void)
{
    MidiClockEstimatorStatus_t status;

    MidiTransport_UpdateSyncState();
    MidiClockEstimator_GetStatus(&status);
    return status.lock_quality;
}

uint8_t MidiClockIsPublicationReady(void)
{
    MidiClockEstimatorStatus_t status;

    MidiTransport_UpdateSyncState();
    MidiClockEstimator_GetStatus(&status);
    return (uint8_t)(status.publication_ready && MidiTransport_IsExternalClockActive());
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

    if (!bpm_x10 || !MidiTransport_IsExternalClockActive() || !MidiClockIsPublicationReady())
        return 0U;

    if (MidiClockEstimator_GetRecoveredBpmX10(bpm_x10))
        return 1U;

    if (!MidiClockEstimator_GetRawBpmX10(bpm_x10))
        return 0U;

    return 1U;
}

uint8_t MidiClockGetMeasuredExternalBpmX10(uint16_t *bpm_x10)
{
    MidiTransport_UpdateSyncState();

    if (!bpm_x10 || !MidiTransport_IsExternalClockActive() || !MidiClockIsPublicationReady())
        return 0U;

    if (MidiClockEstimator_GetRecoveredBpmX10(bpm_x10))
        return 1U;

    if (!MidiClockEstimator_GetMeasuredBpmX10(bpm_x10))
        return 0U;

    return 1U;
}

uint8_t MidiClockGetExternalBpmWindowPulses(void)
{
    MidiClockEstimatorStatus_t status;

    MidiTransport_UpdateSyncState();
    MidiClockEstimator_GetStatus(&status);
    return status.window_pulses;
}

uint8_t MidiClockGetRawExternalBpmX10(uint16_t *bpm_x10)
{
    MidiTransport_UpdateSyncState();

    if (!bpm_x10 || !MidiClockEstimator_GetRawBpmX10(bpm_x10) || !MidiTransport_IsExternalClockActive())
        return 0U;

    return 1U;
}

MidiClockRecoveryHint_t MidiClockGetRecoveryHint(void)
{
    if (!MidiClockIsSyncLost())
        return MIDI_CLOCK_RECOVERY_HINT_NONE;

    return (MidiClockRecoveryHint_t)midi_clock_recovery_hint;
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

uint8_t MidiTransportGetContinuousPhase(MidiTransportPhaseSnapshot_t *phase)
{
    uint32_t primask;
    uint8_t valid;
    uint8_t running;
    uint32_t relative_tick_count;
    uint32_t now_us;
    uint32_t recovered_pulse_us;
    uint32_t recovered_interval_us;
    uint32_t internal_pulse_count;
    uint32_t internal_phase_counts;
    uint32_t internal_pulse_counts;

    if (!phase)
        return 0U;

    MidiTransport_UpdateSyncState();

    primask = __get_PRIMASK();
    __disable_irq();
    valid = midi_barbeat_valid;
    running = midi_transport_running;
    relative_tick_count = midi_transport_global_tick_count - midi_transport_origin_tick_count;
    now_us = TIM2->CNT;
    MidiClockEstimator_GetRecoveredTimingSnapshot(&recovered_pulse_us,
                                                  &recovered_interval_us);
    MidiClock_GetInternalPhaseSnapshot(&internal_pulse_count,
                                       &internal_phase_counts,
                                       &internal_pulse_counts);
    if (primask == 0U)
        __enable_irq();

    if (!valid)
        return 0U;

    phase->tick_count = relative_tick_count;
    phase->tick_fraction_q16 = 0U;
    phase->source = running
        ? MIDI_TRANSPORT_PHASE_SOURCE_EXTERNAL
        : MIDI_TRANSPORT_PHASE_SOURCE_INTERNAL;

    if (running)
    {
        if (recovered_interval_us != 0U)
        {
            uint32_t elapsed_us =
                midi_transport_phase_elapsed_nonnegative_us(now_us, recovered_pulse_us);

            phase->tick_count += elapsed_us / recovered_interval_us;
            phase->tick_fraction_q16 = midi_transport_phase_fraction_q16(
                elapsed_us % recovered_interval_us,
                recovered_interval_us);
        }

        return 1U;
    }

    phase->tick_count = internal_pulse_count;
    if (internal_phase_counts >= internal_pulse_counts)
        internal_phase_counts = internal_pulse_counts - 1U;
    phase->tick_fraction_q16 = midi_transport_phase_fraction_q16(internal_phase_counts,
                                                                 internal_pulse_counts);

    return 1U;
}

void MidiClockDiagnosticService(void)
{
#if MIDI_CLOCK_DIAGNOSTICS_ENABLED
    static uint32_t last_report_tick = 0U;
    MidiTransportPhaseSnapshot_t phase_snapshot;
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
    MidiClockEstimatorStatus_t estimator_status;
    uint8_t estimator_window_pulses;
    uint8_t active;
    uint8_t running;
    uint8_t rearm_required;
    uint8_t barbeat_valid;
    uint8_t have_clock_interval_stats;
    uint8_t have_quarter_service_stats;
    uint32_t phase_tick_count = 0U;
    uint16_t phase_tick_milli = 0U;
    char phase_source = '-';
    uint8_t estimator_valid;
    uint8_t publication_ready;
    int32_t pll_phase_error_us = 0;
    int32_t pll_phase_correction_us = 0;
    int32_t pll_frequency_correction_us = 0;
    char pll_mode = '-';

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
        estimator_window_pulses = midi_clock_external_bpm_window_pulses;
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
    MidiClockEstimator_GetStatus(&estimator_status);
    publication_ready = (uint8_t)(estimator_status.publication_ready && active);
    if (MidiTransportGetContinuousPhase(&phase_snapshot))
    {
        phase_tick_count = phase_snapshot.tick_count;
        phase_tick_milli = midi_transport_phase_fraction_milli(phase_snapshot.tick_fraction_q16);
        phase_source = (phase_snapshot.source == MIDI_TRANSPORT_PHASE_SOURCE_EXTERNAL) ? 'e' : 'i';
    }
    MidiClockEstimator_GetRecoveredPllDiagnostics(&pll_phase_error_us,
                                                  &pll_phase_correction_us,
                                                  &pll_frequency_correction_us,
                                                  NULL);
    estimator_window_pulses = estimator_status.window_pulses;
    estimator_valid = estimator_status.estimator_valid;
    pll_mode = (estimator_status.lock_quality == MIDI_CLOCK_LOCK_QUALITY_LOCKED)
        ? 'L'
        : ((estimator_status.lock_quality == MIDI_CLOCK_LOCK_QUALITY_ACQUIRING) ? 'A' : '-');
    have_clock_interval_stats = (count != 0U && min_us != UINT32_MAX) ? 1U : 0U;
    have_quarter_service_stats = (quarter_service_count != 0U) ? 1U : 0U;

    if (!have_clock_interval_stats
     && !have_quarter_service_stats
     && !active
     && realtime_rx_diag.current_depth == 0U
     && realtime_rx_diag.interval_peak_depth == 0U
     && realtime_rx_diag.interval_dropped_count == 0U)
        return;

    (void)MidiClockGetRawExternalBpmX10(&bpm_x10);
          printf("CLKDIAG active=%u run=%u rearm=%u bb=%u samples=%u avg=%luus min=%lu max=%lu pkpk=%lu bpm=%u.%u ew=%u ev=%u pr=%u ph=%lu.%03u ps=%c pll=%c pe=%ld pc=%ld fc=%ld q=%u qpk=%u qmax=%u drop=%lu/%lu lat=%lu/%luus ls=%u bsvc=%lu/%luus bs=%u\r\n",
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
           (unsigned)estimator_window_pulses,
           (unsigned)estimator_valid,
           (unsigned)publication_ready,
           (unsigned long)phase_tick_count,
           (unsigned)phase_tick_milli,
           phase_source,
               pll_mode,
               (long)pll_phase_error_us,
               (long)pll_phase_correction_us,
               (long)pll_frequency_correction_us,
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