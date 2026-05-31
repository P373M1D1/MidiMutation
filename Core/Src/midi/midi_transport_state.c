#define MIDI_TRANSPORT_INTERNAL_ACCESS 1
#include "midi/midi_transport_internal.h"
#undef MIDI_TRANSPORT_INTERNAL_ACCESS

#include "midi/midi_clock_estimator.h"
#include "midi/midi_clock_internal.h"
#include "midi/midi_output.h"

#include <stdio.h>

#define MIDI_BPM_X10_ROUNDING_OFFSET 5U
#define MIDI_SYNC_STATE_INVALID ((MidiSyncState_t)0xFFU)
#define MIDI_SYNC_DWELL_ACQUIRE_TO_TRACKING_MS 120U
#define MIDI_SYNC_DWELL_TRACKING_TO_LOCKED_MS 180U
#define MIDI_SYNC_DWELL_LOCKED_TO_RELOCK_MS   120U
#define MIDI_SYNC_DWELL_RELOCK_TO_LOCKED_MS   150U
#define MIDI_SYNC_DWELL_LOCKED_TO_REARM_MS    180U
#define MIDI_SYNC_EVENT_QUEUE_DEPTH            8U
#define MIDI_SYNC_ADAPTIVE_CONTROL_ENABLED      1U
#define MIDI_SYNC_ADAPTIVE_WINDOW_MS        10000U
#define MIDI_SYNC_ADAPTIVE_EARLY_WINDOW_MS   2000U
#define MIDI_SYNC_ADAPTIVE_COOLDOWN_WINDOWS    1U
#define MIDI_SYNC_ADAPTIVE_DECAY_WINDOWS       3U
#define MIDI_SYNC_ADAPTIVE_RELOCK_HIGH_MS    900U
#define MIDI_SYNC_ADAPTIVE_RELOCK_RISE_MS      80U
#define MIDI_SYNC_ADAPTIVE_JITTER_HIGH_US    900U
#define MIDI_SYNC_ADAPTIVE_EARLY_JITTER_HIGH_US 1400U
#define MIDI_SYNC_ADAPTIVE_EARLY_JITTER_MIN_SAMPLES 48U
#define MIDI_SYNC_ADAPTIVE_JITTER_RISE_US     120U
#define MIDI_SYNC_ADAPTIVE_LOCK_LOSS_HIGH_PER_WINDOW 2U
#define MIDI_SYNC_ADAPTIVE_LEVEL_MAX           4U
#define MIDI_SYNC_ADAPTIVE_PROBATION_WINDOWS   2U
#define MIDI_SYNC_ADAPTIVE_PROBATION_JITTER_MARGIN_US 120U
#define MIDI_SYNC_ADAPTIVE_PROBATION_RELOCK_MARGIN_MS 80U

typedef enum
{
    MIDI_SYNC_TRANSITION_REASON_NONE = 0,
    MIDI_SYNC_TRANSITION_REASON_TRANSPORT_ARMED,
    MIDI_SYNC_TRANSITION_REASON_TRACKING_READY,
    MIDI_SYNC_TRANSITION_REASON_LOCK_CONFIRMED,
    MIDI_SYNC_TRANSITION_REASON_LOCK_DEGRADED,
    MIDI_SYNC_TRANSITION_REASON_SOURCE_TIMEOUT,
    MIDI_SYNC_TRANSITION_REASON_SOURCE_RETURNED,
    MIDI_SYNC_TRANSITION_REASON_ENTER_REARM,
    MIDI_SYNC_TRANSITION_REASON_SYNC_LOST,
    MIDI_SYNC_TRANSITION_REASON_TRANSPORT_IDLE,
} MidiSyncTransitionReason_t;

typedef struct
{
    uint8_t active;
    uint8_t running;
    uint8_t rearm_required;
    uint8_t sync_lost;
    MidiClockEstimatorStatus_t estimator_status;
    int32_t phase_error_us;
} MidiSyncLifecycleInputs_t;

typedef struct
{
    MidiSyncState_t from_state;
    MidiSyncState_t to_state;
    MidiSyncTransitionReason_t reason;
    uint32_t tick_ms;
    int32_t phase_error_us;
    uint8_t window_pulses;
    uint8_t observed_pulses;
} MidiSyncTransitionEvent_t;

typedef enum
{
    MIDI_SYNC_ADAPT_ACTION_NONE = 0,
    MIDI_SYNC_ADAPT_ACTION_ACQUIRE_AGGRESSIVE,
    MIDI_SYNC_ADAPT_ACTION_TRACK_NARROW,
    MIDI_SYNC_ADAPT_ACTION_HYSTERESIS_INCREASE,
    MIDI_SYNC_ADAPT_ACTION_ACQUIRE_DECAY,
    MIDI_SYNC_ADAPT_ACTION_TRACK_DECAY,
    MIDI_SYNC_ADAPT_ACTION_HYSTERESIS_DECAY,
} MidiSyncAdaptiveAction_t;

static uint32_t midi_transport_phase_elapsed_nonnegative_us(uint32_t now_us,
                                                            uint32_t pulse_us);
static uint16_t midi_transport_phase_fraction_q16(uint32_t numerator,
                                                  uint32_t denominator);
static uint16_t midi_transport_phase_fraction_milli(uint16_t fraction_q16);
static MidiSyncState_t midi_transport_compute_sync_state(const MidiSyncLifecycleInputs_t *inputs,
                                                         uint8_t has_lock_history);
static const char *midi_transport_sync_state_name(MidiSyncState_t state);
static const char *midi_transport_transition_reason_name(MidiSyncTransitionReason_t reason);
static const char *midi_transport_transition_event_name(MidiSyncState_t from,
                                                        MidiSyncState_t to);
static MidiSyncTransitionReason_t midi_transport_transition_reason(MidiSyncState_t from,
                                                                   MidiSyncState_t to,
                                                                   const MidiSyncLifecycleInputs_t *inputs);
static uint32_t midi_transport_transition_dwell_ms(MidiSyncState_t from,
                                                   MidiSyncState_t to);
static void midi_transport_enqueue_sync_event(const MidiSyncTransitionEvent_t *event);
static uint8_t midi_transport_dequeue_sync_event(MidiSyncTransitionEvent_t *event);
static const char *midi_transport_adaptive_action_name(MidiSyncAdaptiveAction_t action);
static MidiSyncAdaptiveAction_t midi_transport_adaptive_inverse_action(MidiSyncAdaptiveAction_t action);
static uint8_t midi_transport_adaptive_apply_action_step(MidiSyncAdaptiveAction_t action);
static void midi_transport_adaptive_window_reset(uint32_t now_ms);
static void midi_transport_adaptive_control_service(const MidiSyncLifecycleInputs_t *inputs,
                                                    uint32_t now_ms);
static void midi_transport_update_sync_lifecycle(void);

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
volatile uint32_t midi_transport_last_quarter_note_count = 0U;
volatile uint32_t midi_transport_quarter_note_event_count = 0U;
volatile uint32_t midi_transport_last_quarter_note_anchor_us = 0U;
volatile uint8_t midi_clock_sync_lost = 0U;
volatile uint8_t midi_clock_recovery_hint = (uint8_t)MIDI_CLOCK_RECOVERY_HINT_NONE;
volatile uint8_t midi_transport_running = 0U;
volatile uint8_t midi_transport_stop_latched = 0U;
volatile uint8_t midi_transport_rearm_required = 0U;
volatile MidiTransportEvent_t midi_transport_event = MIDI_TRANSPORT_EVENT_NONE;
static volatile uint32_t midi_quarter_service_latency_sum_us = 0U;
static volatile uint32_t midi_quarter_service_latency_max_us = 0U;
static volatile uint16_t midi_quarter_service_latency_count = 0U;
static volatile MidiSyncState_t midi_sync_state = MIDI_SYNC_STATE_IDLE;
static volatile uint32_t midi_sync_state_enter_tick_ms = 0U;
static volatile uint32_t midi_sync_last_lock_tick_ms = 0U;
static volatile uint32_t midi_sync_holdover_enter_tick_ms = 0U;
static volatile uint8_t midi_sync_has_lock_history = 0U;
static volatile MidiSyncState_t midi_sync_pending_state = MIDI_SYNC_STATE_INVALID;
static volatile uint32_t midi_sync_pending_enter_tick_ms = 0U;
static volatile MidiSyncTransitionReason_t midi_sync_pending_reason = MIDI_SYNC_TRANSITION_REASON_NONE;
static volatile MidiSyncState_t midi_sync_last_transition_from = MIDI_SYNC_STATE_IDLE;
static volatile MidiSyncState_t midi_sync_last_transition_to = MIDI_SYNC_STATE_IDLE;
static volatile MidiSyncTransitionReason_t midi_sync_last_transition_reason = MIDI_SYNC_TRANSITION_REASON_NONE;
static volatile uint32_t midi_sync_last_transition_tick_ms = 0U;
static volatile int32_t midi_sync_last_transition_phase_error_us = 0;
static volatile uint8_t midi_sync_last_transition_window_pulses = 0U;
static volatile uint8_t midi_sync_last_transition_observed_pulses = 0U;
static MidiSyncTransitionEvent_t midi_sync_event_queue[MIDI_SYNC_EVENT_QUEUE_DEPTH];
static volatile uint8_t midi_sync_event_head = 0U;
static volatile uint8_t midi_sync_event_tail = 0U;
static volatile uint8_t midi_sync_event_count = 0U;
static volatile uint32_t midi_sync_event_overflow_count = 0U;
static volatile uint32_t midi_sync_metric_lock_acquired_count = 0U;
static volatile uint32_t midi_sync_metric_lock_lost_count = 0U;
static volatile uint32_t midi_sync_metric_holdover_entry_count = 0U;
static volatile uint32_t midi_sync_metric_acquire_success_count = 0U;
static volatile uint32_t midi_sync_metric_acquire_latency_last_ms = 0U;
static volatile uint32_t midi_sync_metric_acquire_latency_max_ms = 0U;
static volatile uint32_t midi_sync_metric_acquire_latency_sum_ms = 0U;
static volatile uint32_t midi_sync_metric_relock_success_count = 0U;
static volatile uint32_t midi_sync_metric_relock_latency_last_ms = 0U;
static volatile uint32_t midi_sync_metric_relock_latency_max_ms = 0U;
static volatile uint32_t midi_sync_metric_relock_latency_sum_ms = 0U;
static volatile uint32_t midi_sync_acquire_start_tick_ms = 0U;
static volatile uint32_t midi_sync_relock_start_tick_ms = 0U;
static uint32_t midi_sync_adapt_window_start_tick_ms = 0U;
static uint32_t midi_sync_adapt_snapshot_relock_sum_ms = 0U;
static uint32_t midi_sync_adapt_snapshot_relock_success_count = 0U;
static uint32_t midi_sync_adapt_snapshot_lock_lost_count = 0U;
static uint32_t midi_sync_adapt_snapshot_holdover_entry_count = 0U;
static uint64_t midi_sync_adapt_window_jitter_sum_us = 0ULL;
static uint32_t midi_sync_adapt_window_jitter_samples = 0U;
static uint32_t midi_sync_adapt_last_sampled_pulse_us = 0U;
static uint32_t midi_sync_adapt_prev_window_relock_avg_ms = 0U;
static uint32_t midi_sync_adapt_prev_window_jitter_avg_us = 0U;
static uint32_t midi_sync_adapt_prev_window_lock_lost = 0U;
static uint32_t midi_sync_adapt_last_window_relock_avg_ms = 0U;
static uint32_t midi_sync_adapt_last_window_jitter_avg_us = 0U;
static uint32_t midi_sync_adapt_last_window_lock_lost = 0U;
static uint32_t midi_sync_adapt_last_window_holdover_entries = 0U;
static uint8_t midi_sync_adapt_holdover_latch_valid = 0U;
static uint32_t midi_sync_adapt_holdover_latch_lock_lost = 0U;
static uint32_t midi_sync_adapt_holdover_latch_entries = 0U;
static uint8_t midi_sync_adapt_cooldown_windows = 0U;
static uint8_t midi_sync_adapt_stable_windows = 0U;
static MidiSyncAdaptiveAction_t midi_sync_adapt_last_action = MIDI_SYNC_ADAPT_ACTION_NONE;
static uint32_t midi_sync_adapt_last_action_tick_ms = 0U;
static uint8_t midi_sync_adapt_last_action_rollback = 0U;
static uint8_t midi_sync_adapt_probation_active = 0U;
static MidiSyncAdaptiveAction_t midi_sync_adapt_probation_action = MIDI_SYNC_ADAPT_ACTION_NONE;
static uint8_t midi_sync_adapt_probation_windows_remaining = 0U;
static uint32_t midi_sync_adapt_probation_baseline_relock_avg_ms = 0U;
static uint32_t midi_sync_adapt_probation_baseline_jitter_avg_us = 0U;
static uint32_t midi_sync_adapt_probation_baseline_lock_lost = 0U;
static uint32_t midi_sync_adapt_probation_baseline_holdover_entries = 0U;

static MidiSyncState_t midi_transport_compute_sync_state(const MidiSyncLifecycleInputs_t *inputs,
                                                         uint8_t has_lock_history)
{
    const MidiClockEstimatorStatus_t *status = &inputs->estimator_status;

    if (inputs->rearm_required)
        return inputs->sync_lost ? MIDI_SYNC_STATE_HOLDOVER : MIDI_SYNC_STATE_REARM;

    if (inputs->sync_lost)
        return MIDI_SYNC_STATE_LOST;

    if (!inputs->running)
        return MIDI_SYNC_STATE_IDLE;

    if (!inputs->active)
        return has_lock_history ? MIDI_SYNC_STATE_RELOCK : MIDI_SYNC_STATE_ACQUIRE;

    if (status->live_lock == MIDI_CLOCK_LOCK_QUALITY_LOCKED
     && status->history_confidence >= MIDI_CLOCK_TRANSPORT_CONFIDENCE_STABLE
     && status->publication_ready)
    {
        return MIDI_SYNC_STATE_LOCKED;
    }

    if (status->history_confidence >= MIDI_CLOCK_TRANSPORT_CONFIDENCE_TRACKING)
        return has_lock_history ? MIDI_SYNC_STATE_RELOCK : MIDI_SYNC_STATE_TRACKING;

    return MIDI_SYNC_STATE_ACQUIRE;
}

static const char *midi_transport_transition_reason_name(MidiSyncTransitionReason_t reason)
{
    switch (reason)
    {
    case MIDI_SYNC_TRANSITION_REASON_TRANSPORT_ARMED:
        return "TRANSPORT_ARMED";
    case MIDI_SYNC_TRANSITION_REASON_TRACKING_READY:
        return "TRACKING_READY";
    case MIDI_SYNC_TRANSITION_REASON_LOCK_CONFIRMED:
        return "LOCK_CONFIRMED";
    case MIDI_SYNC_TRANSITION_REASON_LOCK_DEGRADED:
        return "LOCK_DEGRADED";
    case MIDI_SYNC_TRANSITION_REASON_SOURCE_TIMEOUT:
        return "SOURCE_TIMEOUT";
    case MIDI_SYNC_TRANSITION_REASON_SOURCE_RETURNED:
        return "SOURCE_RETURNED";
    case MIDI_SYNC_TRANSITION_REASON_ENTER_REARM:
        return "ENTER_REARM";
    case MIDI_SYNC_TRANSITION_REASON_SYNC_LOST:
        return "SYNC_LOST";
    case MIDI_SYNC_TRANSITION_REASON_TRANSPORT_IDLE:
        return "TRANSPORT_IDLE";
    default:
        return "NONE";
    }
}

static const char *midi_transport_transition_event_name(MidiSyncState_t from,
                                                        MidiSyncState_t to)
{
    if (to == MIDI_SYNC_STATE_LOCKED)
    {
        if (from == MIDI_SYNC_STATE_RELOCK)
            return "SYNC_EVENT_RELOCK_COMPLETE";

        return "SYNC_EVENT_LOCK_ACQUIRED";
    }

    if (to == MIDI_SYNC_STATE_HOLDOVER)
        return "SYNC_EVENT_ENTER_HOLDOVER";

    if (from == MIDI_SYNC_STATE_LOCKED && to == MIDI_SYNC_STATE_REARM)
        return "SYNC_EVENT_TRANSPORT_REARM";

    if (from == MIDI_SYNC_STATE_LOCKED
     && (to == MIDI_SYNC_STATE_RELOCK
      || to == MIDI_SYNC_STATE_TRACKING
      || to == MIDI_SYNC_STATE_ACQUIRE
      || to == MIDI_SYNC_STATE_LOST))
    {
        return "SYNC_EVENT_LOCK_LOST";
    }

    return "SYNC_EVENT_STATE_TRANSITION";
}

static const char *midi_transport_adaptive_action_name(MidiSyncAdaptiveAction_t action)
{
    switch (action)
    {
    case MIDI_SYNC_ADAPT_ACTION_ACQUIRE_AGGRESSIVE:
        return "ACQUIRE_AGGRESSIVE";
    case MIDI_SYNC_ADAPT_ACTION_TRACK_NARROW:
        return "TRACK_NARROW";
    case MIDI_SYNC_ADAPT_ACTION_HYSTERESIS_INCREASE:
        return "HYSTERESIS_INCREASE";
    case MIDI_SYNC_ADAPT_ACTION_ACQUIRE_DECAY:
        return "ACQUIRE_DECAY";
    case MIDI_SYNC_ADAPT_ACTION_TRACK_DECAY:
        return "TRACK_DECAY";
    case MIDI_SYNC_ADAPT_ACTION_HYSTERESIS_DECAY:
        return "HYSTERESIS_DECAY";
    default:
        return "NONE";
    }
}

static MidiSyncAdaptiveAction_t midi_transport_adaptive_inverse_action(MidiSyncAdaptiveAction_t action)
{
    switch (action)
    {
    case MIDI_SYNC_ADAPT_ACTION_ACQUIRE_AGGRESSIVE:
        return MIDI_SYNC_ADAPT_ACTION_ACQUIRE_DECAY;
    case MIDI_SYNC_ADAPT_ACTION_TRACK_NARROW:
        return MIDI_SYNC_ADAPT_ACTION_TRACK_DECAY;
    case MIDI_SYNC_ADAPT_ACTION_HYSTERESIS_INCREASE:
        return MIDI_SYNC_ADAPT_ACTION_HYSTERESIS_DECAY;
    case MIDI_SYNC_ADAPT_ACTION_ACQUIRE_DECAY:
        return MIDI_SYNC_ADAPT_ACTION_ACQUIRE_AGGRESSIVE;
    case MIDI_SYNC_ADAPT_ACTION_TRACK_DECAY:
        return MIDI_SYNC_ADAPT_ACTION_TRACK_NARROW;
    case MIDI_SYNC_ADAPT_ACTION_HYSTERESIS_DECAY:
        return MIDI_SYNC_ADAPT_ACTION_HYSTERESIS_INCREASE;
    default:
        return MIDI_SYNC_ADAPT_ACTION_NONE;
    }
}

static uint8_t midi_transport_adaptive_apply_action_step(MidiSyncAdaptiveAction_t action)
{
    switch (action)
    {
    case MIDI_SYNC_ADAPT_ACTION_ACQUIRE_AGGRESSIVE:
        MidiClockEstimator_AdjustAcquireAggressiveness(+1);
        return 1U;
    case MIDI_SYNC_ADAPT_ACTION_TRACK_NARROW:
        MidiClockEstimator_AdjustTrackingBandwidth(+1);
        return 1U;
    case MIDI_SYNC_ADAPT_ACTION_HYSTERESIS_INCREASE:
        MidiClockEstimator_AdjustLockHysteresis(+1);
        return 1U;
    case MIDI_SYNC_ADAPT_ACTION_ACQUIRE_DECAY:
        MidiClockEstimator_AdjustAcquireAggressiveness(-1);
        return 1U;
    case MIDI_SYNC_ADAPT_ACTION_TRACK_DECAY:
        MidiClockEstimator_AdjustTrackingBandwidth(-1);
        return 1U;
    case MIDI_SYNC_ADAPT_ACTION_HYSTERESIS_DECAY:
        MidiClockEstimator_AdjustLockHysteresis(-1);
        return 1U;
    default:
        return 0U;
    }
}

static void midi_transport_adaptive_window_reset(uint32_t now_ms)
{
    uint32_t recovered_pulse_us = 0U;
    uint8_t have_recovered_pulse;

    have_recovered_pulse = MidiClockEstimator_GetRecoveredPulseTimestampUs(&recovered_pulse_us);
    midi_sync_adapt_window_start_tick_ms = now_ms;
    midi_sync_adapt_snapshot_relock_sum_ms = midi_sync_metric_relock_latency_sum_ms;
    midi_sync_adapt_snapshot_relock_success_count = midi_sync_metric_relock_success_count;
    midi_sync_adapt_snapshot_lock_lost_count = midi_sync_metric_lock_lost_count;
    midi_sync_adapt_snapshot_holdover_entry_count = midi_sync_metric_holdover_entry_count;
    midi_sync_adapt_window_jitter_sum_us = 0ULL;
    midi_sync_adapt_window_jitter_samples = 0U;
    midi_sync_adapt_last_sampled_pulse_us = have_recovered_pulse ? recovered_pulse_us : 0U;
}

static void midi_transport_adaptive_control_service(const MidiSyncLifecycleInputs_t *inputs,
                                                    uint32_t now_ms)
{
#if !MIDI_SYNC_ADAPTIVE_CONTROL_ENABLED
    (void)inputs;
    (void)now_ms;
#else
    MidiClockEstimatorAdaptiveTuning_t tuning;
    MidiSyncAdaptiveAction_t action = MIDI_SYNC_ADAPT_ACTION_NONE;
    uint32_t abs_phase_error_avg_us = 0U;
    uint32_t recovered_pulse_us = 0U;
    uint32_t window_elapsed_ms;
    uint32_t relock_sum_ms;
    uint32_t relock_success_count;
    uint32_t lock_lost_count;
    uint32_t holdover_entry_count;
    uint8_t early_window_stress = 0U;
    uint8_t early_window_jitter = 0U;
    uint32_t relock_sum_delta;
    uint32_t relock_success_delta;
    uint32_t lock_lost_delta;
    uint32_t holdover_entry_delta;
    uint32_t relock_avg_ms = 0U;
    uint32_t jitter_avg_us = 0U;
    uint8_t should_increase_acquire = 0U;
    uint8_t should_narrow_tracking = 0U;
    uint8_t should_increase_hysteresis = 0U;
    uint8_t have_recovered_pulse = 0U;
    uint8_t policy_allowed = 1U;
    uint8_t action_is_rollback = 0U;

    if (!inputs)
        return;

    MidiClockEstimator_GetRecoveredAbsPhaseErrorAvgUs(&abs_phase_error_avg_us);
    have_recovered_pulse = MidiClockEstimator_GetRecoveredPulseTimestampUs(&recovered_pulse_us);
    if (inputs->running && inputs->active && inputs->estimator_status.estimator_valid)
    {
        if (have_recovered_pulse && recovered_pulse_us != midi_sync_adapt_last_sampled_pulse_us)
        {
            midi_sync_adapt_last_sampled_pulse_us = recovered_pulse_us;

            if (UINT64_MAX - midi_sync_adapt_window_jitter_sum_us >= abs_phase_error_avg_us)
                midi_sync_adapt_window_jitter_sum_us += abs_phase_error_avg_us;
            else
                midi_sync_adapt_window_jitter_sum_us = UINT64_MAX;

            if (midi_sync_adapt_window_jitter_samples < UINT32_MAX)
                midi_sync_adapt_window_jitter_samples++;
        }
    }

    if (midi_sync_adapt_window_start_tick_ms == 0U)
    {
        midi_transport_adaptive_window_reset(now_ms);
        return;
    }

    if (!inputs->running)
    {
        midi_sync_adapt_stable_windows = 0U;
        return;
    }

    window_elapsed_ms = now_ms - midi_sync_adapt_window_start_tick_ms;
    relock_sum_ms = midi_sync_metric_relock_latency_sum_ms;
    relock_success_count = midi_sync_metric_relock_success_count;
    lock_lost_count = midi_sync_metric_lock_lost_count;
    holdover_entry_count = midi_sync_metric_holdover_entry_count;

    if (midi_sync_adapt_window_jitter_samples > 0U)
    {
        jitter_avg_us = (uint32_t)(midi_sync_adapt_window_jitter_sum_us
            / (uint64_t)midi_sync_adapt_window_jitter_samples);
    }

    if (window_elapsed_ms < MIDI_SYNC_ADAPTIVE_WINDOW_MS)
    {
        early_window_stress = (uint8_t)((lock_lost_count > midi_sync_adapt_snapshot_lock_lost_count)
            || (holdover_entry_count > midi_sync_adapt_snapshot_holdover_entry_count));
        early_window_jitter = (uint8_t)(midi_sync_adapt_window_jitter_samples
            >= MIDI_SYNC_ADAPTIVE_EARLY_JITTER_MIN_SAMPLES
            && jitter_avg_us >= MIDI_SYNC_ADAPTIVE_EARLY_JITTER_HIGH_US);
        if (window_elapsed_ms < MIDI_SYNC_ADAPTIVE_EARLY_WINDOW_MS
         || (!early_window_stress && !early_window_jitter))
            return;
    }

    relock_sum_delta = (relock_sum_ms >= midi_sync_adapt_snapshot_relock_sum_ms)
        ? (relock_sum_ms - midi_sync_adapt_snapshot_relock_sum_ms)
        : relock_sum_ms;
    relock_success_delta = (relock_success_count >= midi_sync_adapt_snapshot_relock_success_count)
        ? (relock_success_count - midi_sync_adapt_snapshot_relock_success_count)
        : relock_success_count;
    lock_lost_delta = (lock_lost_count >= midi_sync_adapt_snapshot_lock_lost_count)
        ? (lock_lost_count - midi_sync_adapt_snapshot_lock_lost_count)
        : lock_lost_count;
    holdover_entry_delta = (holdover_entry_count >= midi_sync_adapt_snapshot_holdover_entry_count)
        ? (holdover_entry_count - midi_sync_adapt_snapshot_holdover_entry_count)
        : holdover_entry_count;

    if (relock_success_delta > 0U)
        relock_avg_ms = relock_sum_delta / relock_success_delta;

    midi_sync_adapt_last_window_relock_avg_ms = relock_avg_ms;
    midi_sync_adapt_last_window_jitter_avg_us = jitter_avg_us;
    midi_sync_adapt_last_window_lock_lost = lock_lost_delta;
    midi_sync_adapt_last_window_holdover_entries = holdover_entry_delta;

    if (midi_sync_adapt_probation_active)
    {
        uint8_t probation_degraded = 0U;
        MidiSyncAdaptiveAction_t rollback_action = MIDI_SYNC_ADAPT_ACTION_NONE;

        if (lock_lost_delta > midi_sync_adapt_probation_baseline_lock_lost
         || holdover_entry_delta > midi_sync_adapt_probation_baseline_holdover_entries)
        {
            probation_degraded = 1U;
        }

        if (!probation_degraded
         && jitter_avg_us > 0U
         && midi_sync_adapt_probation_baseline_jitter_avg_us > 0U
         && jitter_avg_us >= (midi_sync_adapt_probation_baseline_jitter_avg_us
             + MIDI_SYNC_ADAPTIVE_PROBATION_JITTER_MARGIN_US))
        {
            probation_degraded = 1U;
        }

        if (!probation_degraded
         && relock_success_delta > 0U
         && midi_sync_adapt_probation_baseline_relock_avg_ms > 0U
         && relock_avg_ms >= (midi_sync_adapt_probation_baseline_relock_avg_ms
             + MIDI_SYNC_ADAPTIVE_PROBATION_RELOCK_MARGIN_MS))
        {
            probation_degraded = 1U;
        }

        if (probation_degraded)
        {
            rollback_action = midi_transport_adaptive_inverse_action(midi_sync_adapt_probation_action);
            midi_sync_adapt_probation_active = 0U;
            midi_sync_adapt_probation_action = MIDI_SYNC_ADAPT_ACTION_NONE;
            midi_sync_adapt_probation_windows_remaining = 0U;
            if (midi_transport_adaptive_apply_action_step(rollback_action))
            {
                action = rollback_action;
                action_is_rollback = 1U;
                policy_allowed = 0U;
            }
        }
        else
        {
            if (midi_sync_adapt_probation_windows_remaining > 0U)
                midi_sync_adapt_probation_windows_remaining--;

            if (midi_sync_adapt_probation_windows_remaining == 0U)
            {
                midi_sync_adapt_probation_active = 0U;
                midi_sync_adapt_probation_action = MIDI_SYNC_ADAPT_ACTION_NONE;
            }

            midi_sync_adapt_prev_window_relock_avg_ms = relock_avg_ms;
            midi_sync_adapt_prev_window_jitter_avg_us = jitter_avg_us;
            midi_sync_adapt_prev_window_lock_lost = lock_lost_delta;
            midi_transport_adaptive_window_reset(now_ms);
            return;
        }
    }

    if (policy_allowed)
    {
        if (relock_success_delta > 0U)
        {
            if (relock_avg_ms >= MIDI_SYNC_ADAPTIVE_RELOCK_HIGH_MS)
                should_increase_acquire = 1U;
            else if (midi_sync_adapt_prev_window_relock_avg_ms > 0U
                  && relock_avg_ms >= (midi_sync_adapt_prev_window_relock_avg_ms
                      + MIDI_SYNC_ADAPTIVE_RELOCK_RISE_MS))
            {
                should_increase_acquire = 1U;
            }
        }

        if (jitter_avg_us > 0U)
        {
            if (jitter_avg_us >= MIDI_SYNC_ADAPTIVE_JITTER_HIGH_US)
                should_narrow_tracking = 1U;
            else if (midi_sync_adapt_prev_window_jitter_avg_us > 0U
                  && jitter_avg_us >= (midi_sync_adapt_prev_window_jitter_avg_us
                      + MIDI_SYNC_ADAPTIVE_JITTER_RISE_US))
            {
                should_narrow_tracking = 1U;
            }
        }

        if (lock_lost_delta >= MIDI_SYNC_ADAPTIVE_LOCK_LOSS_HIGH_PER_WINDOW
         || (midi_sync_adapt_prev_window_lock_lost > 0U
          && lock_lost_delta > midi_sync_adapt_prev_window_lock_lost)
         || (lock_lost_delta > 0U && holdover_entry_delta > 0U))
        {
            should_increase_hysteresis = 1U;
        }

        if (midi_sync_adapt_cooldown_windows > 0U)
        {
            midi_sync_adapt_cooldown_windows--;
        }
        else
        {
            MidiClockEstimator_GetAdaptiveTuning(&tuning);

            if (should_increase_hysteresis
             && tuning.hysteresis_level < MIDI_SYNC_ADAPTIVE_LEVEL_MAX)
            {
                action = MIDI_SYNC_ADAPT_ACTION_HYSTERESIS_INCREASE;
                (void)midi_transport_adaptive_apply_action_step(action);
            }
            else if (should_increase_acquire
                  && tuning.acquire_aggression_level < MIDI_SYNC_ADAPTIVE_LEVEL_MAX)
            {
                action = MIDI_SYNC_ADAPT_ACTION_ACQUIRE_AGGRESSIVE;
                (void)midi_transport_adaptive_apply_action_step(action);
            }
            else if (should_narrow_tracking
                  && tuning.tracking_bandwidth_level < MIDI_SYNC_ADAPTIVE_LEVEL_MAX)
            {
                action = MIDI_SYNC_ADAPT_ACTION_TRACK_NARROW;
                (void)midi_transport_adaptive_apply_action_step(action);
            }
            else
            {
                uint8_t healthy_window = (uint8_t)(lock_lost_delta == 0U
                    && holdover_entry_delta == 0U
                    && !should_increase_acquire
                    && !should_narrow_tracking
                    && !should_increase_hysteresis);

                if (healthy_window)
                {
                    if (midi_sync_adapt_stable_windows < UINT8_MAX)
                        midi_sync_adapt_stable_windows++;

                    if (midi_sync_adapt_stable_windows >= MIDI_SYNC_ADAPTIVE_DECAY_WINDOWS)
                    {
                        if (tuning.acquire_aggression_level > 0U)
                        {
                            action = MIDI_SYNC_ADAPT_ACTION_ACQUIRE_DECAY;
                            (void)midi_transport_adaptive_apply_action_step(action);
                        }
                        else if (tuning.tracking_bandwidth_level > 0U)
                        {
                            action = MIDI_SYNC_ADAPT_ACTION_TRACK_DECAY;
                            (void)midi_transport_adaptive_apply_action_step(action);
                        }
                        else if (tuning.hysteresis_level > 0U)
                        {
                            action = MIDI_SYNC_ADAPT_ACTION_HYSTERESIS_DECAY;
                            (void)midi_transport_adaptive_apply_action_step(action);
                        }
                    }
                }
                else
                {
                    midi_sync_adapt_stable_windows = 0U;
                }
            }
        }
    }

    if (action != MIDI_SYNC_ADAPT_ACTION_NONE)
    {
        midi_sync_adapt_last_action = action;
        midi_sync_adapt_last_action_tick_ms = now_ms;
        midi_sync_adapt_last_action_rollback = action_is_rollback;
        midi_sync_adapt_stable_windows = 0U;
        midi_sync_adapt_cooldown_windows = MIDI_SYNC_ADAPTIVE_COOLDOWN_WINDOWS;

        if (!action_is_rollback)
        {
            midi_sync_adapt_probation_active = 1U;
            midi_sync_adapt_probation_action = action;
            midi_sync_adapt_probation_windows_remaining = MIDI_SYNC_ADAPTIVE_PROBATION_WINDOWS;
            midi_sync_adapt_probation_baseline_relock_avg_ms = relock_avg_ms;
            midi_sync_adapt_probation_baseline_jitter_avg_us = jitter_avg_us;
            midi_sync_adapt_probation_baseline_lock_lost = lock_lost_delta;
            midi_sync_adapt_probation_baseline_holdover_entries = holdover_entry_delta;
        }

        MidiClockEstimator_GetAdaptiveTuning(&tuning);
        printf("SYNCADAPT action=%s rollback=%u probation_action=%s probation_windows=%u relock_avg_ms=%lu jitter_avg_us=%lu lock_lost=%lu holdover_entries=%lu levels=acq%u,track%u,hyst%u gains=ap%u,af%u,tp%u,tf%u lock_div=enter%u,exit%u lock_stable=%u\\r\\n",
               midi_transport_adaptive_action_name(action),
               (unsigned)action_is_rollback,
               midi_transport_adaptive_action_name(midi_sync_adapt_probation_action),
               (unsigned)midi_sync_adapt_probation_windows_remaining,
               (unsigned long)relock_avg_ms,
               (unsigned long)jitter_avg_us,
               (unsigned long)lock_lost_delta,
               (unsigned long)holdover_entry_delta,
               (unsigned)tuning.acquire_aggression_level,
               (unsigned)tuning.tracking_bandwidth_level,
               (unsigned)tuning.hysteresis_level,
               (unsigned)tuning.acquire_phase_gain_divisor,
               (unsigned)tuning.acquire_frequency_gain_divisor,
               (unsigned)tuning.track_phase_gain_divisor,
               (unsigned)tuning.track_frequency_gain_divisor,
               (unsigned)tuning.lock_enter_threshold_divisor,
               (unsigned)tuning.lock_exit_threshold_divisor,
               (unsigned)tuning.lock_stable_pulses);
    }

    midi_sync_adapt_prev_window_relock_avg_ms = relock_avg_ms;
    midi_sync_adapt_prev_window_jitter_avg_us = jitter_avg_us;
    midi_sync_adapt_prev_window_lock_lost = lock_lost_delta;
    midi_transport_adaptive_window_reset(now_ms);
#endif
}

static void midi_transport_enqueue_sync_event(const MidiSyncTransitionEvent_t *event)
{
    uint32_t primask;

    if (!event)
        return;

    primask = __get_PRIMASK();
    __disable_irq();

    if (midi_sync_event_count >= MIDI_SYNC_EVENT_QUEUE_DEPTH)
    {
        midi_sync_event_head = (uint8_t)((midi_sync_event_head + 1U) % MIDI_SYNC_EVENT_QUEUE_DEPTH);
        if (midi_sync_event_overflow_count < UINT32_MAX)
            midi_sync_event_overflow_count++;
    }
    else
    {
        midi_sync_event_count++;
    }

    midi_sync_event_queue[midi_sync_event_tail] = *event;
    midi_sync_event_tail = (uint8_t)((midi_sync_event_tail + 1U) % MIDI_SYNC_EVENT_QUEUE_DEPTH);

    if (primask == 0U)
        __enable_irq();
}

static uint8_t midi_transport_dequeue_sync_event(MidiSyncTransitionEvent_t *event)
{
    uint32_t primask;

    if (!event)
        return 0U;

    primask = __get_PRIMASK();
    __disable_irq();

    if (midi_sync_event_count == 0U)
    {
        if (primask == 0U)
            __enable_irq();
        return 0U;
    }

    *event = midi_sync_event_queue[midi_sync_event_head];
    midi_sync_event_head = (uint8_t)((midi_sync_event_head + 1U) % MIDI_SYNC_EVENT_QUEUE_DEPTH);
    midi_sync_event_count--;

    if (primask == 0U)
        __enable_irq();

    return 1U;
}

static MidiSyncTransitionReason_t midi_transport_transition_reason(MidiSyncState_t from,
                                                                   MidiSyncState_t to,
                                                                   const MidiSyncLifecycleInputs_t *inputs)
{
    (void)inputs;

    if (from == to)
        return MIDI_SYNC_TRANSITION_REASON_NONE;

    if (to == MIDI_SYNC_STATE_HOLDOVER)
        return MIDI_SYNC_TRANSITION_REASON_SOURCE_TIMEOUT;

    if (to == MIDI_SYNC_STATE_REARM)
        return MIDI_SYNC_TRANSITION_REASON_ENTER_REARM;

    if (to == MIDI_SYNC_STATE_LOST)
        return MIDI_SYNC_TRANSITION_REASON_SYNC_LOST;

    if (to == MIDI_SYNC_STATE_LOCKED)
        return MIDI_SYNC_TRANSITION_REASON_LOCK_CONFIRMED;

    if (to == MIDI_SYNC_STATE_RELOCK)
    {
        if (from == MIDI_SYNC_STATE_LOCKED)
            return MIDI_SYNC_TRANSITION_REASON_LOCK_DEGRADED;

        return MIDI_SYNC_TRANSITION_REASON_SOURCE_RETURNED;
    }

    if (to == MIDI_SYNC_STATE_TRACKING)
        return MIDI_SYNC_TRANSITION_REASON_TRACKING_READY;

    if (to == MIDI_SYNC_STATE_IDLE)
        return MIDI_SYNC_TRANSITION_REASON_TRANSPORT_IDLE;

    if (to == MIDI_SYNC_STATE_ACQUIRE)
    {
        if (from == MIDI_SYNC_STATE_LOCKED)
            return MIDI_SYNC_TRANSITION_REASON_LOCK_DEGRADED;

        return MIDI_SYNC_TRANSITION_REASON_TRANSPORT_ARMED;
    }

    return MIDI_SYNC_TRANSITION_REASON_NONE;
}

static uint32_t midi_transport_transition_dwell_ms(MidiSyncState_t from,
                                                   MidiSyncState_t to)
{
    if (from == MIDI_SYNC_STATE_ACQUIRE && to == MIDI_SYNC_STATE_TRACKING)
        return MIDI_SYNC_DWELL_ACQUIRE_TO_TRACKING_MS;

    if (from == MIDI_SYNC_STATE_TRACKING && to == MIDI_SYNC_STATE_LOCKED)
        return MIDI_SYNC_DWELL_TRACKING_TO_LOCKED_MS;

    if (from == MIDI_SYNC_STATE_LOCKED && to == MIDI_SYNC_STATE_RELOCK)
        return MIDI_SYNC_DWELL_LOCKED_TO_RELOCK_MS;

    if (from == MIDI_SYNC_STATE_RELOCK && to == MIDI_SYNC_STATE_LOCKED)
        return MIDI_SYNC_DWELL_RELOCK_TO_LOCKED_MS;

    if (from == MIDI_SYNC_STATE_LOCKED && to == MIDI_SYNC_STATE_REARM)
        return MIDI_SYNC_DWELL_LOCKED_TO_REARM_MS;

    return 0U;
}

static const char *midi_transport_sync_state_name(MidiSyncState_t state)
{
    switch (state)
    {
    case MIDI_SYNC_STATE_IDLE:
        return "IDLE";
    case MIDI_SYNC_STATE_ACQUIRE:
        return "ACQUIRE";
    case MIDI_SYNC_STATE_TRACKING:
        return "TRACKING";
    case MIDI_SYNC_STATE_LOCKED:
        return "LOCKED";
    case MIDI_SYNC_STATE_HOLDOVER:
        return "HOLDOVER";
    case MIDI_SYNC_STATE_RELOCK:
        return "RELOCK";
    case MIDI_SYNC_STATE_LOST:
        return "LOST";
    case MIDI_SYNC_STATE_REARM:
        return "REARM";
    default:
        return "UNKNOWN";
    }
}

static void midi_transport_update_sync_lifecycle(void)
{
    MidiSyncLifecycleInputs_t inputs;
    MidiSyncTransitionEvent_t transition_event;
    MidiSyncState_t desired_state;
    uint32_t metric_latency_ms;
    uint32_t transition_dwell_ms;
    uint32_t now_ms = HAL_GetTick();

    inputs.active = MidiTransport_IsExternalClockActive();
    inputs.running = midi_transport_running;
    inputs.rearm_required = midi_transport_rearm_required;
    inputs.sync_lost = midi_clock_sync_lost;
    MidiClockEstimator_GetStatus(&inputs.estimator_status);
    MidiClockEstimator_GetRecoveredPllDiagnostics(&inputs.phase_error_us,
                                                  NULL,
                                                  NULL,
                                                  NULL);
    midi_transport_adaptive_control_service(&inputs, now_ms);

    desired_state = midi_transport_compute_sync_state(&inputs,
                                                      midi_sync_has_lock_history);

    if (desired_state == midi_sync_state)
    {
        midi_sync_pending_state = MIDI_SYNC_STATE_INVALID;
        midi_sync_pending_enter_tick_ms = 0U;
        midi_sync_pending_reason = MIDI_SYNC_TRANSITION_REASON_NONE;
        return;
    }

    if (midi_sync_pending_state != desired_state)
    {
        midi_sync_pending_state = desired_state;
        midi_sync_pending_enter_tick_ms = now_ms;
        midi_sync_pending_reason = midi_transport_transition_reason(midi_sync_state,
                                                                    desired_state,
                                                                    &inputs);
        return;
    }

    transition_dwell_ms = midi_transport_transition_dwell_ms(midi_sync_state,
                                                             desired_state);
    if ((now_ms - midi_sync_pending_enter_tick_ms) < transition_dwell_ms)
        return;

    midi_sync_last_transition_from = midi_sync_state;
    midi_sync_last_transition_to = desired_state;
    midi_sync_last_transition_reason = midi_sync_pending_reason;
    midi_sync_last_transition_tick_ms = now_ms;
    midi_sync_last_transition_phase_error_us = inputs.phase_error_us;
    midi_sync_last_transition_window_pulses = inputs.estimator_status.window_pulses;
    midi_sync_last_transition_observed_pulses = inputs.estimator_status.observed_pulses;

    if (midi_sync_last_transition_from == MIDI_SYNC_STATE_LOCKED
     && midi_sync_last_transition_to != MIDI_SYNC_STATE_LOCKED
     && midi_sync_last_transition_reason != MIDI_SYNC_TRANSITION_REASON_ENTER_REARM
     && midi_sync_last_transition_reason != MIDI_SYNC_TRANSITION_REASON_TRANSPORT_IDLE
     && midi_sync_metric_lock_lost_count < UINT32_MAX)
    {
        midi_sync_metric_lock_lost_count++;
    }

    if (midi_sync_last_transition_to == MIDI_SYNC_STATE_HOLDOVER
     && midi_sync_last_transition_from != MIDI_SYNC_STATE_HOLDOVER
     && midi_sync_metric_holdover_entry_count < UINT32_MAX)
    {
        midi_sync_metric_holdover_entry_count++;

        midi_sync_adapt_holdover_latch_lock_lost =
            (midi_sync_metric_lock_lost_count >= midi_sync_adapt_snapshot_lock_lost_count)
            ? (midi_sync_metric_lock_lost_count - midi_sync_adapt_snapshot_lock_lost_count)
            : midi_sync_metric_lock_lost_count;
        midi_sync_adapt_holdover_latch_entries =
            (midi_sync_metric_holdover_entry_count >= midi_sync_adapt_snapshot_holdover_entry_count)
            ? (midi_sync_metric_holdover_entry_count - midi_sync_adapt_snapshot_holdover_entry_count)
            : midi_sync_metric_holdover_entry_count;
        midi_sync_adapt_holdover_latch_valid = 1U;
    }

    transition_event.from_state = midi_sync_last_transition_from;
    transition_event.to_state = midi_sync_last_transition_to;
    transition_event.reason = midi_sync_last_transition_reason;
    transition_event.tick_ms = midi_sync_last_transition_tick_ms;
    transition_event.phase_error_us = midi_sync_last_transition_phase_error_us;
    transition_event.window_pulses = midi_sync_last_transition_window_pulses;
    transition_event.observed_pulses = midi_sync_last_transition_observed_pulses;
    midi_transport_enqueue_sync_event(&transition_event);

    midi_sync_state = desired_state;
    midi_sync_state_enter_tick_ms = now_ms;
    midi_sync_pending_state = MIDI_SYNC_STATE_INVALID;
    midi_sync_pending_enter_tick_ms = 0U;
    midi_sync_pending_reason = MIDI_SYNC_TRANSITION_REASON_NONE;

    if (midi_sync_state == MIDI_SYNC_STATE_ACQUIRE)
    {
        MidiClockEstimatorAdaptiveTuning_t adaptive_tuning;

        midi_sync_acquire_start_tick_ms = now_ms;
        midi_sync_relock_start_tick_ms = 0U;
        midi_transport_adaptive_window_reset(now_ms);
        midi_sync_adapt_prev_window_relock_avg_ms = 0U;
        midi_sync_adapt_prev_window_jitter_avg_us = 0U;
        midi_sync_adapt_prev_window_lock_lost = 0U;
        midi_sync_adapt_last_window_relock_avg_ms = 0U;
        midi_sync_adapt_last_window_jitter_avg_us = 0U;
        midi_sync_adapt_last_window_lock_lost = 0U;
        midi_sync_adapt_last_window_holdover_entries = 0U;
        midi_sync_adapt_holdover_latch_valid = 0U;
        midi_sync_adapt_holdover_latch_lock_lost = 0U;
        midi_sync_adapt_holdover_latch_entries = 0U;
        midi_sync_adapt_probation_active = 0U;
        midi_sync_adapt_probation_action = MIDI_SYNC_ADAPT_ACTION_NONE;
        midi_sync_adapt_probation_windows_remaining = 0U;
        midi_sync_adapt_probation_baseline_relock_avg_ms = 0U;
        midi_sync_adapt_probation_baseline_jitter_avg_us = 0U;
        midi_sync_adapt_probation_baseline_lock_lost = 0U;
        midi_sync_adapt_probation_baseline_holdover_entries = 0U;
        midi_sync_adapt_cooldown_windows = 0U;
        midi_sync_adapt_stable_windows = 0U;
        midi_sync_adapt_last_action_rollback = 0U;

        MidiClockEstimator_GetAdaptiveTuning(&adaptive_tuning);
        if (adaptive_tuning.acquire_aggression_level == 0U
         && adaptive_tuning.tracking_bandwidth_level == 0U
         && adaptive_tuning.hysteresis_level == 0U)
        {
            midi_sync_adapt_last_action = MIDI_SYNC_ADAPT_ACTION_NONE;
            midi_sync_adapt_last_action_tick_ms = 0U;
        }
    }
    else if (midi_sync_state == MIDI_SYNC_STATE_TRACKING)
    {
        if (midi_sync_acquire_start_tick_ms == 0U && !midi_sync_has_lock_history)
            midi_sync_acquire_start_tick_ms = now_ms;
    }
    else if (midi_sync_state == MIDI_SYNC_STATE_RELOCK)
    {
        midi_sync_relock_start_tick_ms = now_ms;
        midi_sync_acquire_start_tick_ms = 0U;
    }

    if (midi_sync_state == MIDI_SYNC_STATE_LOCKED)
    {
        if (midi_sync_metric_lock_acquired_count < UINT32_MAX)
            midi_sync_metric_lock_acquired_count++;

        if (midi_sync_relock_start_tick_ms != 0U)
        {
            metric_latency_ms = now_ms - midi_sync_relock_start_tick_ms;
            midi_sync_metric_relock_latency_last_ms = metric_latency_ms;
            if (metric_latency_ms > midi_sync_metric_relock_latency_max_ms)
                midi_sync_metric_relock_latency_max_ms = metric_latency_ms;
            if (UINT32_MAX - midi_sync_metric_relock_latency_sum_ms >= metric_latency_ms)
                midi_sync_metric_relock_latency_sum_ms += metric_latency_ms;
            else
                midi_sync_metric_relock_latency_sum_ms = UINT32_MAX;
            if (midi_sync_metric_relock_success_count < UINT32_MAX)
                midi_sync_metric_relock_success_count++;
            midi_sync_relock_start_tick_ms = 0U;
        }
        else if (midi_sync_acquire_start_tick_ms != 0U)
        {
            metric_latency_ms = now_ms - midi_sync_acquire_start_tick_ms;
            midi_sync_metric_acquire_latency_last_ms = metric_latency_ms;
            if (metric_latency_ms > midi_sync_metric_acquire_latency_max_ms)
                midi_sync_metric_acquire_latency_max_ms = metric_latency_ms;
            if (UINT32_MAX - midi_sync_metric_acquire_latency_sum_ms >= metric_latency_ms)
                midi_sync_metric_acquire_latency_sum_ms += metric_latency_ms;
            else
                midi_sync_metric_acquire_latency_sum_ms = UINT32_MAX;
            if (midi_sync_metric_acquire_success_count < UINT32_MAX)
                midi_sync_metric_acquire_success_count++;
            midi_sync_acquire_start_tick_ms = 0U;
        }
    }

    if (midi_sync_state == MIDI_SYNC_STATE_IDLE
     || midi_sync_state == MIDI_SYNC_STATE_REARM
     || midi_sync_state == MIDI_SYNC_STATE_HOLDOVER
     || midi_sync_state == MIDI_SYNC_STATE_LOST)
    {
        midi_sync_acquire_start_tick_ms = 0U;
        midi_sync_relock_start_tick_ms = 0U;
    }

    if (midi_sync_state == MIDI_SYNC_STATE_LOCKED)
    {
        midi_sync_has_lock_history = 1U;
        midi_sync_last_lock_tick_ms = now_ms;
    }

    if (midi_sync_state == MIDI_SYNC_STATE_HOLDOVER)
    {
        if (midi_sync_last_transition_from != MIDI_SYNC_STATE_HOLDOVER)
            midi_sync_holdover_enter_tick_ms = now_ms;
    }
    else
    {
        midi_sync_holdover_enter_tick_ms = 0U;
    }

    /* Keep lock history across REARM so brief source interruptions can
     * relock via the RELOCK path instead of restarting in ACQUIRE. This
     * preserves estimator continuity under jittery but returning clocks. */

    if (midi_sync_state == MIDI_SYNC_STATE_IDLE && !inputs.active)
    {
        midi_sync_has_lock_history = 0U;
        midi_sync_last_lock_tick_ms = 0U;
    }
}

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

void MidiTransport_ServiceSyncLifecycle(void)
{
    MidiTransport_UpdateSyncState();
    midi_transport_update_sync_lifecycle();
}

uint8_t MidiTransportIsRunning(void)
{
    return midi_transport_running;
}

uint8_t MidiClockIsSyncLost(void)
{
    return midi_clock_sync_lost;
}

uint8_t MidiClockIsEstimatorValid(void)
{
    MidiClockEstimatorStatus_t status;

    MidiClockEstimator_GetStatus(&status);
    return status.estimator_valid;
}

MidiClockTransportConfidence_t MidiClockGetTransportConfidence(void)
{
    MidiClockEstimatorStatus_t status;

    MidiClockEstimator_GetStatus(&status);
    return status.history_confidence;
}

MidiClockLockQuality_t MidiClockGetLockQuality(void)
{
    MidiClockEstimatorStatus_t status;

    MidiClockEstimator_GetStatus(&status);
    return status.live_lock;
}

MidiSyncState_t MidiClockGetSyncState(void)
{
    return midi_sync_state;
}

uint32_t MidiClockGetSyncStateAgeMs(void)
{
    uint32_t now_ms;

    now_ms = HAL_GetTick();
    return now_ms - midi_sync_state_enter_tick_ms;
}

uint32_t MidiClockGetHoldoverAgeMs(void)
{
    uint32_t now_ms;

    if (midi_sync_state != MIDI_SYNC_STATE_HOLDOVER || midi_sync_holdover_enter_tick_ms == 0U)
        return 0U;

    now_ms = HAL_GetTick();
    return now_ms - midi_sync_holdover_enter_tick_ms;
}

uint32_t MidiClockGetLastLockAgeMs(void)
{
    uint32_t now_ms;

    if (midi_sync_last_lock_tick_ms == 0U)
        return 0U;

    now_ms = HAL_GetTick();
    return now_ms - midi_sync_last_lock_tick_ms;
}

uint8_t MidiClockIsPublicationReady(void)
{
    return (uint8_t)(MidiClockGetSyncState() == MIDI_SYNC_STATE_LOCKED);
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
    if (!bpm_x10 || !MidiTransport_IsExternalClockActive())
        return 0U;

    /**
     * Prefer the window-averaged measured BPM for display.
     * It reacts immediately to tempo changes because the estimator
     * automatically shrinks the averaging window on detected motion.
     * The PLL-recovered BPM has frequency tracking gain 1/64, so it
     * lags a 1 BPM change by ~70 pulses (~1.5s at 110 BPM) — too slow
     * for a responsive display.  Fall back to PLL then raw if the
     * window average is not yet valid.
     */
    if (MidiClockEstimator_GetMeasuredBpmX10(bpm_x10))
        return 1U;

    if (MidiClockEstimator_GetRecoveredBpmX10(bpm_x10))
        return 1U;

    if (MidiClockEstimator_GetRawBpmX10(bpm_x10))
        return 1U;

    return 0U;
}

uint8_t MidiClockGetExternalBpmWindowPulses(void)
{
    MidiClockEstimatorStatus_t status;

    MidiClockEstimator_GetStatus(&status);
    return status.window_pulses;
}

uint8_t MidiClockGetRawExternalBpmX10(uint16_t *bpm_x10)
{
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
    return MidiTransport_IsExternalClockActive();
}

uint8_t MidiClockGetBarBeat(uint8_t *bar, uint8_t *beat)
{
    return MidiTransportCycle_GetBarBeat(bar, beat);
}

uint8_t MidiClockGetQuarterNoteCount(uint32_t *quarter_note_count)
{
    uint32_t primask;
    uint32_t total_tick_count;
    uint32_t origin_tick_count;
    uint8_t valid;

    if (!quarter_note_count)
        return 0U;

    primask = __get_PRIMASK();
    __disable_irq();
    valid = midi_barbeat_valid;
    total_tick_count = midi_transport_global_tick_count;
    origin_tick_count = midi_transport_origin_tick_count;
    if (primask == 0U)
        __enable_irq();

    if (!valid)
        return 0U;

    *quarter_note_count = (total_tick_count - origin_tick_count)
        / MIDI_CLOCK_PULSES_PER_QUARTER_NOTE;
    return 1U;
}

uint8_t MidiClockGetQuarterNoteRenderStamp(uint32_t *event_count,
                                           uint32_t *quarter_note_count)
{
    return MidiClockGetQuarterNoteRenderStampWithAnchor(event_count,
                                                        quarter_note_count,
                                                        0);
}

uint8_t MidiClockGetQuarterNoteRenderStampWithAnchor(uint32_t *event_count,
                                                     uint32_t *quarter_note_count,
                                                     uint32_t *anchor_us)
{
    uint32_t primask;
    uint8_t valid;

    if (!event_count || !quarter_note_count)
        return 0U;

    primask = __get_PRIMASK();
    __disable_irq();
    valid = midi_barbeat_valid;
    *event_count = midi_transport_quarter_note_event_count;
    *quarter_note_count = midi_transport_last_quarter_note_count;
    if (anchor_us)
        *anchor_us = midi_transport_last_quarter_note_anchor_us;
    if (primask == 0U)
        __enable_irq();

    return valid;
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
    MidiSyncTransitionEvent_t sync_event;
    MidiInputRealtimeRxDiagnostics_t realtime_rx_diag;
    MidiOutputTimebendDiagnostics_t timebend_diag;
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
    uint8_t sync_lost;
    MidiSyncState_t sync_state;
    const char *sync_state_text = "UNKNOWN";
    uint32_t sync_state_age_ms = 0U;
    uint32_t holdover_age_ms = 0U;
    uint32_t last_lock_age_ms = 0U;
    const char *last_transition_from_text = "UNKNOWN";
    const char *last_transition_to_text = "UNKNOWN";
    const char *transition_reason_text = "NONE";
    uint32_t transition_age_ms = 0U;
    int32_t transition_phase_error_us = 0;
    uint8_t transition_window_pulses = 0U;
    uint8_t transition_observed_pulses = 0U;
    uint32_t acquire_latency_last_ms = 0U;
    uint32_t acquire_latency_avg_ms = 0U;
    uint32_t acquire_latency_max_ms = 0U;
    uint32_t relock_latency_last_ms = 0U;
    uint32_t relock_latency_avg_ms = 0U;
    uint32_t relock_latency_max_ms = 0U;
    uint32_t lock_acquired_count = 0U;
    uint32_t lock_lost_count = 0U;
    uint32_t holdover_entry_count = 0U;
    uint32_t relock_success_count = 0U;
    uint32_t acquire_success_count = 0U;
    MidiClockEstimatorAdaptiveTuning_t adaptive_tuning;
    const char *adaptive_last_action_text = "NONE";
    uint32_t adaptive_last_action_age_ms = 0U;
    uint8_t adaptive_last_action_rollback = 0U;
    uint8_t adaptive_probation_active = 0U;
    const char *adaptive_probation_action_text = "NONE";
    uint8_t adaptive_probation_windows = 0U;
    uint32_t adaptive_window_relock_sum_ms = 0U;
    uint32_t adaptive_window_relock_success = 0U;
    uint32_t adaptive_window_relock_avg_ms = 0U;
    uint32_t adaptive_window_jitter_avg_us = 0U;
    uint32_t adaptive_window_lock_lost = 0U;
    uint32_t adaptive_window_holdover_entries = 0U;
    uint32_t adaptive_window_age_ms = 0U;
    uint32_t adaptive_window_jitter_samples = 0U;
    uint8_t adaptive_cooldown_windows = 0U;
    uint8_t adaptive_stable_windows = 0U;
    uint32_t phase_tick_count = 0U;
    uint16_t phase_tick_milli = 0U;
    char phase_source = '-';
    char transport_confidence = '-';
    uint8_t estimator_valid;
    uint8_t estimator_observed_pulses;
    uint8_t publication_ready;
    int32_t timebend_phase_pct_x10 = 0;
    int32_t timebend_effective_bpm_pct_x10 = 0;
    int32_t pll_phase_error_us = 0;
    int32_t pll_phase_correction_us = 0;
    int32_t pll_frequency_correction_us = 0;
    char pll_mode = '-';

    while (midi_transport_dequeue_sync_event(&sync_event))
    {
        printf("SYNCEVT event=%s from=%s to=%s reason=%s age_ms=%lu phase_err_us=%ld window=%u observed=%u overflow_total=%lu\r\n",
               midi_transport_transition_event_name(sync_event.from_state,
                                                    sync_event.to_state),
               midi_transport_sync_state_name(sync_event.from_state),
               midi_transport_sync_state_name(sync_event.to_state),
               midi_transport_transition_reason_name(sync_event.reason),
               (unsigned long)(now - sync_event.tick_ms),
               (long)sync_event.phase_error_us,
               (unsigned)sync_event.window_pulses,
               (unsigned)sync_event.observed_pulses,
               (unsigned long)midi_sync_event_overflow_count);
    }

    if ((now - last_report_tick) < MIDI_CLOCK_DIAGNOSTIC_REPORT_MS)
        return;

    last_report_tick = now;

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
        sync_lost = midi_clock_sync_lost;
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
    MidiOutput_TakeTimebendDiagnostics(&timebend_diag);
    MidiClockEstimator_GetStatus(&estimator_status);
    midi_transport_update_sync_lifecycle();
    active = MidiClockIsExternalSignalPresent();

    {
        uint32_t primask = __get_PRIMASK();

        __disable_irq();
        running = midi_transport_running;
        rearm_required = midi_transport_rearm_required;
        sync_lost = midi_clock_sync_lost;
        barbeat_valid = midi_barbeat_valid;
        if (primask == 0U)
            __enable_irq();
    }

    sync_state = midi_sync_state;
    sync_state_text = midi_transport_sync_state_name(sync_state);
    sync_state_age_ms = (now >= midi_sync_state_enter_tick_ms)
        ? (now - midi_sync_state_enter_tick_ms)
        : 0U;
    holdover_age_ms = (midi_sync_holdover_enter_tick_ms == 0U)
        ? 0U
        : ((now >= midi_sync_holdover_enter_tick_ms)
            ? (now - midi_sync_holdover_enter_tick_ms)
            : 0U);
    last_lock_age_ms = (midi_sync_last_lock_tick_ms == 0U)
        ? 0U
        : ((now >= midi_sync_last_lock_tick_ms)
            ? (now - midi_sync_last_lock_tick_ms)
            : 0U);
    last_transition_from_text = midi_transport_sync_state_name(midi_sync_last_transition_from);
    last_transition_to_text = midi_transport_sync_state_name(midi_sync_last_transition_to);
    transition_reason_text = midi_transport_transition_reason_name(midi_sync_last_transition_reason);
    transition_age_ms = (midi_sync_last_transition_tick_ms == 0U)
        ? 0U
        : ((now >= midi_sync_last_transition_tick_ms)
            ? (now - midi_sync_last_transition_tick_ms)
            : 0U);
    transition_phase_error_us = midi_sync_last_transition_phase_error_us;
    transition_window_pulses = midi_sync_last_transition_window_pulses;
    transition_observed_pulses = midi_sync_last_transition_observed_pulses;
    acquire_latency_last_ms = midi_sync_metric_acquire_latency_last_ms;
    acquire_latency_max_ms = midi_sync_metric_acquire_latency_max_ms;
    acquire_success_count = midi_sync_metric_acquire_success_count;
    acquire_latency_avg_ms = (acquire_success_count == 0U)
        ? 0U
        : (midi_sync_metric_acquire_latency_sum_ms / acquire_success_count);
    relock_latency_last_ms = midi_sync_metric_relock_latency_last_ms;
    relock_latency_max_ms = midi_sync_metric_relock_latency_max_ms;
    relock_success_count = midi_sync_metric_relock_success_count;
    relock_latency_avg_ms = (relock_success_count == 0U)
        ? 0U
        : (midi_sync_metric_relock_latency_sum_ms / relock_success_count);
    lock_acquired_count = midi_sync_metric_lock_acquired_count;
    lock_lost_count = midi_sync_metric_lock_lost_count;
    holdover_entry_count = midi_sync_metric_holdover_entry_count;
    MidiClockEstimator_GetAdaptiveTuning(&adaptive_tuning);
    adaptive_last_action_text = midi_transport_adaptive_action_name(midi_sync_adapt_last_action);
    adaptive_last_action_age_ms = (midi_sync_adapt_last_action_tick_ms == 0U)
        ? 0U
        : (now - midi_sync_adapt_last_action_tick_ms);
    adaptive_window_relock_sum_ms = (midi_sync_metric_relock_latency_sum_ms >= midi_sync_adapt_snapshot_relock_sum_ms)
        ? (midi_sync_metric_relock_latency_sum_ms - midi_sync_adapt_snapshot_relock_sum_ms)
        : midi_sync_metric_relock_latency_sum_ms;
    adaptive_window_relock_success = (midi_sync_metric_relock_success_count
        >= midi_sync_adapt_snapshot_relock_success_count)
        ? (midi_sync_metric_relock_success_count - midi_sync_adapt_snapshot_relock_success_count)
        : midi_sync_metric_relock_success_count;
    adaptive_window_relock_avg_ms = (adaptive_window_relock_success == 0U)
        ? 0U
        : (adaptive_window_relock_sum_ms / adaptive_window_relock_success);
    adaptive_window_age_ms = (midi_sync_adapt_window_start_tick_ms == 0U)
        ? 0U
        : (now - midi_sync_adapt_window_start_tick_ms);
    adaptive_window_jitter_samples = midi_sync_adapt_window_jitter_samples;
    adaptive_window_jitter_avg_us = (adaptive_window_jitter_samples == 0U)
        ? 0U
        : (uint32_t)(midi_sync_adapt_window_jitter_sum_us / (uint64_t)adaptive_window_jitter_samples);
    adaptive_window_lock_lost = (midi_sync_metric_lock_lost_count >= midi_sync_adapt_snapshot_lock_lost_count)
        ? (midi_sync_metric_lock_lost_count - midi_sync_adapt_snapshot_lock_lost_count)
        : midi_sync_metric_lock_lost_count;
    adaptive_window_holdover_entries =
        (midi_sync_metric_holdover_entry_count >= midi_sync_adapt_snapshot_holdover_entry_count)
        ? (midi_sync_metric_holdover_entry_count - midi_sync_adapt_snapshot_holdover_entry_count)
        : midi_sync_metric_holdover_entry_count;
    if ((sync_state == MIDI_SYNC_STATE_HOLDOVER || sync_state == MIDI_SYNC_STATE_REARM)
     && midi_sync_adapt_holdover_latch_valid)
    {
        if (adaptive_window_lock_lost < midi_sync_adapt_holdover_latch_lock_lost)
            adaptive_window_lock_lost = midi_sync_adapt_holdover_latch_lock_lost;
        if (adaptive_window_holdover_entries < midi_sync_adapt_holdover_latch_entries)
            adaptive_window_holdover_entries = midi_sync_adapt_holdover_latch_entries;
    }
    adaptive_cooldown_windows = midi_sync_adapt_cooldown_windows;
    adaptive_stable_windows = midi_sync_adapt_stable_windows;
    adaptive_last_action_rollback = midi_sync_adapt_last_action_rollback;
    adaptive_probation_active = midi_sync_adapt_probation_active;
    adaptive_probation_action_text = midi_transport_adaptive_action_name(midi_sync_adapt_probation_action);
    adaptive_probation_windows = midi_sync_adapt_probation_windows_remaining;
    publication_ready = (uint8_t)(sync_state == MIDI_SYNC_STATE_LOCKED);
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
    estimator_observed_pulses = estimator_status.observed_pulses;
    estimator_valid = estimator_status.estimator_valid;
    switch (estimator_status.history_confidence)
    {
    case MIDI_CLOCK_TRANSPORT_CONFIDENCE_STABLE:
        transport_confidence = 'S';
        break;
    case MIDI_CLOCK_TRANSPORT_CONFIDENCE_TRACKING:
        transport_confidence = 'T';
        break;
    case MIDI_CLOCK_TRANSPORT_CONFIDENCE_OPERATIONAL:
        transport_confidence = 'O';
        break;
    default:
        transport_confidence = '-';
        break;
    }
    pll_mode = (estimator_status.live_lock == MIDI_CLOCK_LOCK_QUALITY_LOCKED)
        ? 'L'
        : ((estimator_status.live_lock == MIDI_CLOCK_LOCK_QUALITY_ACQUIRING) ? 'A' : '-');
    have_clock_interval_stats = (count != 0U && min_us != UINT32_MAX) ? 1U : 0U;
    have_quarter_service_stats = (quarter_service_count != 0U) ? 1U : 0U;

    if (have_clock_interval_stats && sum_us != 0U)
    {
        uint32_t interval_avg_us = sum_us / (uint32_t)count;

        if (interval_avg_us != 0U)
        {
            int64_t scaled = ((int64_t)timebend_diag.phase_offset_us * 1000LL);

            if (scaled >= 0)
                timebend_phase_pct_x10 = (int32_t)((scaled + (int64_t)(interval_avg_us / 2U)) / (int64_t)interval_avg_us);
            else
                timebend_phase_pct_x10 = (int32_t)((scaled - (int64_t)(interval_avg_us / 2U)) / (int64_t)interval_avg_us);
        }
    }

    if (have_clock_interval_stats && sum_us != 0U && timebend_diag.emit_interval_avg_us != 0U)
    {
        uint32_t truth_interval_avg_us = sum_us / (uint32_t)count;
        int64_t scaled_ratio_x10 = ((int64_t)truth_interval_avg_us * 1000LL);

        if (scaled_ratio_x10 >= 0)
            scaled_ratio_x10 = (scaled_ratio_x10 + (int64_t)(timebend_diag.emit_interval_avg_us / 2U))
                / (int64_t)timebend_diag.emit_interval_avg_us;
        else
            scaled_ratio_x10 = (scaled_ratio_x10 - (int64_t)(timebend_diag.emit_interval_avg_us / 2U))
                / (int64_t)timebend_diag.emit_interval_avg_us;

        timebend_effective_bpm_pct_x10 = (int32_t)(scaled_ratio_x10 - 1000LL);
    }

    if (!have_clock_interval_stats
     && !have_quarter_service_stats
     && !active
     && realtime_rx_diag.current_depth == 0U
     && realtime_rx_diag.interval_peak_depth == 0U
     && realtime_rx_diag.interval_dropped_count == 0U)
    {
        return;
    }

    (void)MidiClockGetRawExternalBpmX10(&bpm_x10);
        printf("CLKDIAG ext_active=%u transport_run=%u rearm_pending=%u sync_lost=%u barbeat_valid=%u interval_samples=%u interval_avg_us=%lu interval_min_us=%lu interval_max_us=%lu interval_pkpk_us=%lu raw_bpm=%u.%u est_window=%u est_observed=%u history_confidence=%c est_valid=%u publication_ready=%u sync_state=%s sync_state_age_ms=%lu holdover_ms=%lu last_lock_age_ms=%lu last_transition=%s->%s transition_reason=%s transition_age_ms=%lu transition_phase_err_us=%ld transition_window=%u transition_observed=%u acq_ms_last=%lu acq_ms_avg=%lu acq_ms_max=%lu relock_ms_last=%lu relock_ms_avg=%lu relock_ms_max=%lu lock_acquired=%lu acquire_success=%lu relock_success=%lu lock_lost=%lu holdover_entries=%lu adapt_action=%s adapt_action_age_ms=%lu adapt_last_rollback=%u adapt_prob_active=%u adapt_prob_action=%s adapt_prob_windows=%u adapt_win_age_ms=%lu adapt_win_relock_ms=%lu adapt_win_jitter_us=%lu adapt_win_jitter_samples=%lu adapt_win_lock_lost=%lu adapt_win_holdover=%lu adapt_cd_windows=%u adapt_stable_windows=%u adapt_levels=%u/%u/%u adapt_gains=%u/%u/%u/%u adapt_lock_div=%u/%u adapt_lock_stable=%u phase_tick=%lu.%03u phase_src=%c live_lock=%c pll_err_us=%ld pll_phase_corr_us=%ld pll_freq_corr_us=%ld rx_q_now=%u rx_q_peak=%u rx_q_lifetime_peak=%u rx_drop_interval=%lu rx_drop_total=%lu rx_latency_avg_us=%lu rx_latency_max_us=%lu rx_latency_samples=%u beat_service_avg_us=%lu beat_service_max_us=%lu beat_service_samples=%u tb_active=%u tb_phase_us=%ld tb_phase_pct_x10=%ld tb_eff_bpm_pct_x10=%ld tb_vel_usps=%ld tb_q_depth=%u tb_q_peak=%u tb_enq=%lu tb_emit=%lu tb_emit_iavg_us=%lu tb_emit_imin_us=%lu tb_emit_imax_us=%lu tb_emit_isamples=%lu tb_drop=%lu tb_clamp_min=%lu tb_clamp_max=%lu tb_late_avg_us=%lu tb_late_max_us=%lu tb_late_samples=%lu tb_miss=%lu tb_cross_backlog_now=%lu tb_cross_backlog_peak=%lu tb_uart_q_now=%u tb_uart_q_peak=%u tb_phase_nonmono=%lu\r\n",
           (unsigned)active,
            (unsigned)running,
            (unsigned)rearm_required,
            (unsigned)sync_lost,
            (unsigned)barbeat_valid,
           (unsigned)(have_clock_interval_stats ? count : 0U),
           (unsigned long)(have_clock_interval_stats ? (sum_us / (uint32_t)count) : 0U),
           (unsigned long)(have_clock_interval_stats ? min_us : 0U),
           (unsigned long)(have_clock_interval_stats ? max_us : 0U),
           (unsigned long)(have_clock_interval_stats ? (max_us - min_us) : 0U),
           (unsigned)(bpm_x10 / 10U),
           (unsigned)(bpm_x10 % 10U),
           (unsigned)estimator_window_pulses,
           (unsigned)estimator_observed_pulses,
           transport_confidence,
           (unsigned)estimator_valid,
           (unsigned)publication_ready,
           sync_state_text,
           (unsigned long)sync_state_age_ms,
           (unsigned long)holdover_age_ms,
           (unsigned long)last_lock_age_ms,
           last_transition_from_text,
           last_transition_to_text,
           transition_reason_text,
           (unsigned long)transition_age_ms,
           (long)transition_phase_error_us,
           (unsigned)transition_window_pulses,
           (unsigned)transition_observed_pulses,
           (unsigned long)acquire_latency_last_ms,
           (unsigned long)acquire_latency_avg_ms,
           (unsigned long)acquire_latency_max_ms,
           (unsigned long)relock_latency_last_ms,
           (unsigned long)relock_latency_avg_ms,
           (unsigned long)relock_latency_max_ms,
           (unsigned long)lock_acquired_count,
           (unsigned long)acquire_success_count,
           (unsigned long)relock_success_count,
           (unsigned long)lock_lost_count,
           (unsigned long)holdover_entry_count,
           adaptive_last_action_text,
           (unsigned long)adaptive_last_action_age_ms,
           (unsigned)adaptive_last_action_rollback,
           (unsigned)adaptive_probation_active,
           adaptive_probation_action_text,
           (unsigned)adaptive_probation_windows,
           (unsigned long)adaptive_window_age_ms,
           (unsigned long)adaptive_window_relock_avg_ms,
           (unsigned long)adaptive_window_jitter_avg_us,
           (unsigned long)adaptive_window_jitter_samples,
           (unsigned long)adaptive_window_lock_lost,
           (unsigned long)adaptive_window_holdover_entries,
           (unsigned)adaptive_cooldown_windows,
           (unsigned)adaptive_stable_windows,
           (unsigned)adaptive_tuning.acquire_aggression_level,
           (unsigned)adaptive_tuning.tracking_bandwidth_level,
           (unsigned)adaptive_tuning.hysteresis_level,
           (unsigned)adaptive_tuning.acquire_phase_gain_divisor,
           (unsigned)adaptive_tuning.acquire_frequency_gain_divisor,
           (unsigned)adaptive_tuning.track_phase_gain_divisor,
           (unsigned)adaptive_tuning.track_frequency_gain_divisor,
           (unsigned)adaptive_tuning.lock_enter_threshold_divisor,
           (unsigned)adaptive_tuning.lock_exit_threshold_divisor,
           (unsigned)adaptive_tuning.lock_stable_pulses,
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
           (unsigned)quarter_service_count,
           (unsigned)timebend_diag.active,
           (long)timebend_diag.phase_offset_us,
           (long)timebend_phase_pct_x10,
           (long)timebend_effective_bpm_pct_x10,
           (long)timebend_diag.velocity_us_per_s,
           (unsigned)timebend_diag.due_depth,
           (unsigned)timebend_diag.due_peak_depth,
           (unsigned long)timebend_diag.enqueued_count,
           (unsigned long)timebend_diag.emitted_count,
           (unsigned long)timebend_diag.emit_interval_avg_us,
           (unsigned long)timebend_diag.emit_interval_min_us,
           (unsigned long)timebend_diag.emit_interval_max_us,
           (unsigned long)timebend_diag.emit_interval_sample_count,
           (unsigned long)timebend_diag.dropped_count,
           (unsigned long)timebend_diag.clamp_min_count,
           (unsigned long)timebend_diag.clamp_max_count,
           (unsigned long)timebend_diag.late_avg_us,
           (unsigned long)timebend_diag.late_max_us,
           (unsigned long)timebend_diag.late_sample_count,
           (unsigned long)timebend_diag.missed_emit_count,
           (unsigned long)timebend_diag.crossing_backlog_now,
           (unsigned long)timebend_diag.crossing_backlog_peak,
           (unsigned)timebend_diag.uart_clock_depth,
           (unsigned)timebend_diag.uart_clock_peak_depth,
           (unsigned long)timebend_diag.phase_nonmono_count);
#endif
}