#define MIDI_LEGACY_CLOCK_READ_API_ALLOWED 1
#include "midi/clock_engine.h"

#include "app/app_ui.h"
#include "midi/midi_clock_estimator.h"
#include "midi/midi_clock_internal.h"
#include "midi_functions.h"
#include "runtime_config.h"
#undef MIDI_LEGACY_CLOCK_READ_API_ALLOWED
#define MIDI_TRANSPORT_INTERNAL_ACCESS 1
#include "midi/midi_transport_internal.h"
#undef MIDI_TRANSPORT_INTERNAL_ACCESS

#define CLOCK_ENGINE_SERVICE_INTERVAL_MS              10U
#define CLOCK_ENGINE_MAX_FREQ_CORRECTION_PPM_PER_SEC  250000U
#define CLOCK_ENGINE_MAX_ACCEPTED_ESTIMATOR_STEP_US   50000U
#define CLOCK_ENGINE_MIN_FREQ_STEP_US                 1U
#define CLOCK_ENGINE_EXTERNAL_BPM_REFRESH_DELTA_X10   2U

static volatile ClockEngineSnapshot_t clock_engine_cached_snapshot;
static volatile uint8_t clock_engine_cached_snapshot_valid = 0U;
static volatile uint8_t clock_engine_last_status_sync_lost = 0U;
static volatile uint8_t clock_engine_last_status_signal_present = 0U;
static volatile uint8_t clock_engine_last_status_running = 0U;
static volatile MidiSyncState_t clock_engine_last_status_sync_state = MIDI_SYNC_STATE_IDLE;
static volatile uint8_t clock_engine_last_status_valid = 0U;
static uint16_t clock_engine_last_external_bpm_x10 = 0U;
static uint8_t clock_engine_last_external_bpm_valid = 0U;
static volatile ClockState_t clock_engine_last_state = CLOCK_STATE_OFF;
static volatile uint8_t clock_engine_last_state_valid = 0U;
static volatile ClockEngineStateTrace_t clock_engine_last_state_trace = {
    CLOCK_STATE_OFF,
    CLOCK_STATE_OFF,
    CLOCK_ENGINE_TRANSITION_REASON_NONE,
    0U,
    0U,
};

static ClockEngineTransitionReason_t ClockEngine_ClassifyTransitionReason(const ClockEngineSnapshot_t *previous,
                                                                          uint8_t previous_valid,
                                                                          const ClockEngineSnapshot_t *current,
                                                                          ClockState_t from_state,
                                                                          ClockState_t to_state);

static void ClockEngine_CaptureSnapshot(void);
static void ClockEngine_ServiceExternalRateControl(void);
static uint8_t ClockEngine_ServiceExternalBpmDisplayRefresh(void);
static uint32_t ClockEngine_AbsDeltaU32(uint32_t a, uint32_t b);
static uint32_t ClockEngine_MaxFrequencyStepUs(uint32_t current_interval_us);

/**
 * Atomically captures all transport and sync primitives into a single coherent
 * snapshot. All source reads are performed under one critical section so that
 * state, beat, and phase cannot diverge across an ISR boundary mid-capture.
 *
 * MidiTransportGetContinuousPhase() and MidiClockGetQuarterNoteRenderStampWithAnchor()
 * use the PRIMASK pattern internally; nesting is safe because the inner calls
 * see IRQs already disabled and do not re-enable them.
 *
 * ClockState_t and beat_valid are derived at capture time so all consumers
 * share the same classification without recomputation divergence.
 */
static void ClockEngine_CaptureSnapshot(void)
{
    ClockEngineSnapshot_t snapshot = {0U};
    MidiTransportPhaseSnapshot_t phase_snapshot;
    uint32_t beat_event_count = 0U;
    uint32_t beat_quarter_note_count = 0U;
    uint32_t beat_anchor_us = 0U;
    uint8_t beat_valid = 0U;
    uint32_t primask;

    /**
     * Single critical section covers all source reads.
     * This ensures state, phase, and beat data are captured from the same
     * transport moment and cannot be split by an ISR firing between reads.
     */
    primask = __get_PRIMASK();
    __disable_irq();

    snapshot.running = MidiTransportIsRunning() ? 1U : 0U;
    snapshot.transport_rearm_required = midi_transport_rearm_required;
    snapshot.external_signal_present = MidiClockIsExternalSignalPresent() ? 1U : 0U;
    snapshot.sync_lost = MidiClockIsSyncLost() ? 1U : 0U;
    snapshot.estimator_valid = MidiClockIsEstimatorValid() ? 1U : 0U;
    snapshot.sync_state = MidiClockGetSyncState();
    snapshot.publication_ready = (snapshot.sync_state == MIDI_SYNC_STATE_LOCKED) ? 1U : 0U;
    snapshot.estimator_window_pulses = MidiClockGetExternalBpmWindowPulses();
    snapshot.transport_confidence = MidiClockGetTransportConfidence();
    snapshot.lock_quality = MidiClockGetLockQuality();

    /**
     * Beat stamp: sample under the same lock as all other fields so beat
     * state is coherent with running/sync_state at exactly one moment.
     * MidiClockGetQuarterNoteRenderStampWithAnchor uses its own PRIMASK
     * guard internally; the nested disable is safe on Cortex-M.
     */
    beat_valid = (uint8_t)MidiClockGetQuarterNoteRenderStampWithAnchor(
        &beat_event_count, &beat_quarter_note_count, &beat_anchor_us);

    /**
     * Phase snapshot: MidiTransportGetContinuousPhase also uses PRIMASK
     * internally; nested call is safe.
     */
    if (MidiTransportGetContinuousPhase(&phase_snapshot))
    {
        snapshot.phase = phase_snapshot.tick_fraction_q16;
        snapshot.tick = phase_snapshot.tick_count;

        if (phase_snapshot.source == MIDI_TRANSPORT_PHASE_SOURCE_INTERNAL)
            snapshot.source = CLOCK_ENGINE_SOURCE_INTERNAL;
        else if (phase_snapshot.source == MIDI_TRANSPORT_PHASE_SOURCE_EXTERNAL)
            snapshot.source = CLOCK_ENGINE_SOURCE_EXTERNAL;
        else
            snapshot.source = CLOCK_ENGINE_SOURCE_NONE;
    }
    else if (snapshot.running || snapshot.external_signal_present)
    {
        snapshot.source = CLOCK_ENGINE_SOURCE_EXTERNAL;
    }
    else
    {
        snapshot.source = CLOCK_ENGINE_SOURCE_INTERNAL;
    }

    if (primask == 0U)
        __enable_irq();

    /**
     * State classification and beat data derivation happen after the critical
     * section. These are pure functions of the already-captured snapshot fields;
     * no further transport reads are needed.
     */
    snapshot.state = ClockTruth_ClassifyState(snapshot.external_signal_present,
                                              snapshot.running,
                                              snapshot.sync_lost,
                                              snapshot.sync_state);
    snapshot.beat_valid = beat_valid;
    snapshot.beat_event_count = beat_event_count;
    snapshot.beat_quarter_note_count = beat_quarter_note_count;
    snapshot.beat_anchor_us = beat_anchor_us;

    /**
     * Write the completed snapshot under lock so any concurrent reader
     * always sees a fully-populated struct, never a partial write.
     */
    primask = __get_PRIMASK();
    __disable_irq();
    clock_engine_cached_snapshot = snapshot;
    clock_engine_cached_snapshot_valid = 1U;
    if (primask == 0U)
        __enable_irq();
}

static ClockEngineTransitionReason_t ClockEngine_ClassifyTransitionReason(const ClockEngineSnapshot_t *previous,
                                                                          uint8_t previous_valid,
                                                                          const ClockEngineSnapshot_t *current,
                                                                          ClockState_t from_state,
                                                                          ClockState_t to_state)
{
    if (!current || from_state == to_state)
        return CLOCK_ENGINE_TRANSITION_REASON_NONE;

    if (to_state == CLOCK_STATE_LOCKED)
        return CLOCK_ENGINE_TRANSITION_REASON_LOCK_ACQUIRED;

    if (to_state == CLOCK_STATE_HOLDOVER)
        return CLOCK_ENGINE_TRANSITION_REASON_ENTER_HOLDOVER;

    if (to_state == CLOCK_STATE_LOST)
        return CLOCK_ENGINE_TRANSITION_REASON_SYNC_LOST;

    if (to_state == CLOCK_STATE_DETECTING)
    {
        if (!previous_valid || (previous && !previous->external_signal_present))
            return CLOCK_ENGINE_TRANSITION_REASON_SIGNAL_DETECTED;

        return CLOCK_ENGINE_TRANSITION_REASON_RECLASSIFIED;
    }

    if (to_state == CLOCK_STATE_SYNCING)
    {
        if (from_state == CLOCK_STATE_LOCKED)
            return CLOCK_ENGINE_TRANSITION_REASON_LOCK_DEGRADED;

        return CLOCK_ENGINE_TRANSITION_REASON_TRANSPORT_ACTIVE;
    }

    if (to_state == CLOCK_STATE_OFF)
    {
        if (previous_valid && previous && previous->external_signal_present && !current->external_signal_present)
            return CLOCK_ENGINE_TRANSITION_REASON_SIGNAL_REMOVED;

        if (previous_valid && previous && previous->running && !current->running)
            return CLOCK_ENGINE_TRANSITION_REASON_TRANSPORT_STOPPED;

        return CLOCK_ENGINE_TRANSITION_REASON_RECLASSIFIED;
    }

    return CLOCK_ENGINE_TRANSITION_REASON_RECLASSIFIED;
}

void ClockEngine_Init(uint16_t startup_bpm)
{
    MidiClockOutputInit(startup_bpm);
    ClockEngine_CaptureSnapshot();
}

__attribute__((section(".RamFunc")))
void ClockEngine_ISR_OnInternalPulse(uint32_t now_us)
{
    (void)now_us;
    MidiClockOutputIrqHandler();
}

__attribute__((section(".RamFunc")))
uint8_t ClockEngine_ISR_OnExternalRealtime(uint8_t byte, uint32_t now_us)
{
    return MidiTransport_HandleRealtimeByteFast(byte, now_us);
}

void ClockEngine_Service10ms(void)
{
    uint8_t previous_sync_lost;
    uint8_t previous_signal_present;
    uint8_t previous_running;
    MidiSyncState_t previous_sync_state;
    uint8_t previous_valid;
    uint8_t status_changed;
    ClockEngineSnapshot_t previous_snapshot;
    ClockState_t previous_state;
    ClockState_t current_state;
    ClockEngineTransitionReason_t reason;

    previous_sync_lost = clock_engine_last_status_sync_lost;
    previous_signal_present = clock_engine_last_status_signal_present;
    previous_running = clock_engine_last_status_running;
    previous_sync_state = clock_engine_last_status_sync_state;
    previous_valid = clock_engine_last_status_valid;
    previous_snapshot = clock_engine_cached_snapshot;
    previous_state = clock_engine_last_state;

    MidiTransport_ServiceSyncLifecycle();
    ClockEngine_ServiceExternalRateControl();
    ClockEngine_CaptureSnapshot();

    clock_engine_last_status_sync_lost = clock_engine_cached_snapshot.sync_lost;
    clock_engine_last_status_signal_present = clock_engine_cached_snapshot.external_signal_present;
    clock_engine_last_status_running = clock_engine_cached_snapshot.running;
    clock_engine_last_status_sync_state = clock_engine_cached_snapshot.sync_state;
    clock_engine_last_status_valid = 1U;

    /**
     * Use the state already computed during CaptureSnapshot() — avoids any
     * divergence from a second call to ClockTruth_ClassifyState() here.
     */
    current_state = clock_engine_cached_snapshot.state;
    if (!clock_engine_last_state_valid)
    {
        clock_engine_last_state = current_state;
        clock_engine_last_state_valid = 1U;
    }
    else if (previous_state != current_state)
    {
        reason = ClockEngine_ClassifyTransitionReason(&previous_snapshot,
                                                      previous_valid,
                                                      (const ClockEngineSnapshot_t *)&clock_engine_cached_snapshot,
                                                      previous_state,
                                                      current_state);
        clock_engine_last_state_trace.from_state = previous_state;
        clock_engine_last_state_trace.to_state = current_state;
        clock_engine_last_state_trace.reason = reason;
        clock_engine_last_state_trace.tick_ms = HAL_GetTick();
        clock_engine_last_state_trace.sequence++;
        clock_engine_last_state = current_state;
    }

    status_changed = (uint8_t)(!previous_valid
        || previous_sync_lost != clock_engine_cached_snapshot.sync_lost
        || previous_signal_present != clock_engine_cached_snapshot.external_signal_present
        || previous_running != clock_engine_cached_snapshot.running
        || previous_sync_state != clock_engine_cached_snapshot.sync_state);

    if (ClockEngine_ServiceExternalBpmDisplayRefresh())
        status_changed = 1U;

    if (status_changed)
        AppUi_RequestStatusStripRefresh();
}

static void ClockEngine_ServiceExternalRateControl(void)
{
    const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();
    MidiClockEstimatorStatus_t estimator_status;
    uint32_t current_interval_us;
    uint32_t target_interval_us;
    uint32_t step_us;
    uint32_t delta_us;
    uint32_t next_interval_us;

    if (!global || global->sync_style != RUNTIME_CONFIG_SYNC_STYLE_MIDI_CLOCK)
        return;

    if (global->clock_mode != RUNTIME_CONFIG_CLOCK_MODE_SLAVE)
        return;

    MidiClockEstimator_GetStatus(&estimator_status);
    if (!estimator_status.estimator_valid || !estimator_status.publication_ready)
        return;

    if (!MidiClockEstimator_GetRecoveredPulseIntervalUs(&target_interval_us) || target_interval_us == 0U)
        return;

    current_interval_us = MidiClock_GetOutputPulseIntervalUs();
    if (current_interval_us == 0U)
        return;

    delta_us = ClockEngine_AbsDeltaU32(target_interval_us, current_interval_us);
    if (delta_us == 0U)
        return;

    if (delta_us > CLOCK_ENGINE_MAX_ACCEPTED_ESTIMATOR_STEP_US)
        return;

    step_us = ClockEngine_MaxFrequencyStepUs(current_interval_us);
    if (step_us > delta_us)
        step_us = delta_us;

    next_interval_us = (target_interval_us > current_interval_us)
        ? (current_interval_us + step_us)
        : (current_interval_us - step_us);

    (void)MidiClock_ClockEngineApplyOutputIntervalUs(next_interval_us);
}

static uint8_t ClockEngine_ServiceExternalBpmDisplayRefresh(void)
{
    uint16_t bpm_x10;
    uint32_t delta_x10;

    if (!clock_engine_cached_snapshot.external_signal_present
     || clock_engine_cached_snapshot.sync_lost
     || (!MidiClockGetMeasuredExternalBpmX10(&bpm_x10)
      && !MidiClockGetExternalBpmX10(&bpm_x10)))
    {
        clock_engine_last_external_bpm_valid = 0U;
        clock_engine_last_external_bpm_x10 = 0U;
        return 0U;
    }

    if (!clock_engine_last_external_bpm_valid)
    {
        clock_engine_last_external_bpm_valid = 1U;
        clock_engine_last_external_bpm_x10 = bpm_x10;
        return 1U;
    }

    delta_x10 = ClockEngine_AbsDeltaU32((uint32_t)bpm_x10,
                                        (uint32_t)clock_engine_last_external_bpm_x10);
    if (delta_x10 < CLOCK_ENGINE_EXTERNAL_BPM_REFRESH_DELTA_X10)
        return 0U;

    clock_engine_last_external_bpm_x10 = bpm_x10;
    return 1U;
}

static uint32_t ClockEngine_AbsDeltaU32(uint32_t a, uint32_t b)
{
    return (a >= b) ? (a - b) : (b - a);
}

static uint32_t ClockEngine_MaxFrequencyStepUs(uint32_t current_interval_us)
{
    uint64_t step_us = (uint64_t)current_interval_us
        * (uint64_t)CLOCK_ENGINE_MAX_FREQ_CORRECTION_PPM_PER_SEC
        * (uint64_t)CLOCK_ENGINE_SERVICE_INTERVAL_MS;

    step_us /= 1000000000ULL;
    if (step_us < CLOCK_ENGINE_MIN_FREQ_STEP_US)
        step_us = CLOCK_ENGINE_MIN_FREQ_STEP_US;

    if (step_us > UINT32_MAX)
        return UINT32_MAX;

    return (uint32_t)step_us;
}

uint8_t ClockEngine_GetSnapshot(ClockEngineSnapshot_t *snapshot)
{
    uint32_t primask;

    if (!snapshot)
        return 0U;

    if (!clock_engine_cached_snapshot_valid)
        ClockEngine_CaptureSnapshot();

    primask = __get_PRIMASK();
    __disable_irq();
    *snapshot = clock_engine_cached_snapshot;
    if (primask == 0U)
        __enable_irq();

    return 1U;
}

uint8_t ClockEngine_IsRunning(void)
{
    ClockEngineSnapshot_t snapshot;

    return ClockEngine_GetSnapshot(&snapshot) ? snapshot.running : 0U;
}

uint8_t ClockEngine_IsSyncLost(void)
{
    ClockEngineSnapshot_t snapshot;

    return ClockEngine_GetSnapshot(&snapshot) ? snapshot.sync_lost : 0U;
}

uint8_t ClockEngine_IsExternalSignalPresent(void)
{
    ClockEngineSnapshot_t snapshot;

    return ClockEngine_GetSnapshot(&snapshot) ? snapshot.external_signal_present : 0U;
}

uint8_t ClockEngine_IsPublicationReady(void)
{
    ClockEngineSnapshot_t snapshot;

    return ClockEngine_GetSnapshot(&snapshot) ? snapshot.publication_ready : 0U;
}

uint8_t ClockEngine_IsStopLatched(void)
{
    return MidiTransportStopLatched();
}

uint8_t ClockEngine_IsEstimatorValid(void)
{
    ClockEngineSnapshot_t snapshot;

    return ClockEngine_GetSnapshot(&snapshot) ? snapshot.estimator_valid : 0U;
}

uint8_t ClockEngine_GetEstimatorWindowPulses(void)
{
    ClockEngineSnapshot_t snapshot;

    return ClockEngine_GetSnapshot(&snapshot) ? snapshot.estimator_window_pulses : 0U;
}

MidiClockTransportConfidence_t ClockEngine_GetTransportConfidence(void)
{
    ClockEngineSnapshot_t snapshot;

    return ClockEngine_GetSnapshot(&snapshot)
        ? snapshot.transport_confidence
        : MIDI_CLOCK_TRANSPORT_CONFIDENCE_NONE;
}

MidiClockLockQuality_t ClockEngine_GetLockQuality(void)
{
    ClockEngineSnapshot_t snapshot;

    return ClockEngine_GetSnapshot(&snapshot)
        ? snapshot.lock_quality
        : MIDI_CLOCK_LOCK_QUALITY_NONE;
}

MidiSyncState_t ClockEngine_GetSyncState(void)
{
    ClockEngineSnapshot_t snapshot;

    return ClockEngine_GetSnapshot(&snapshot) ? snapshot.sync_state : MIDI_SYNC_STATE_IDLE;
}

ClockState_t ClockEngine_GetState(void)
{
    ClockEngineSnapshot_t snapshot;

    /**
     * State is pre-computed during CaptureSnapshot() and stored in the snapshot.
     * Using the cached field avoids recomputation divergence between calls.
     */
    if (!ClockEngine_GetSnapshot(&snapshot))
        return CLOCK_STATE_OFF;

    return snapshot.state;
}

ClockBehaviorProfile_t ClockEngine_GetBehaviorProfile(void)
{
    return ClockTruth_BehaviorForState(ClockEngine_GetState());
}

/**
 * Returns a unified tick struct that bundles clock state, behavior profile,
 * and beat data from one coherent snapshot.
 *
 * Consumers should call this once per service cycle and reason only from the
 * result. This prevents state/profile/beat divergence from multiple reads.
 */
ClockEngineTick_t ClockEngine_GetTick(void)
{
    ClockEngineSnapshot_t snapshot;
    ClockEngineTick_t tick = {0};

    if (!ClockEngine_GetSnapshot(&snapshot))
    {
        tick.state = CLOCK_STATE_OFF;
        tick.profile = ClockTruth_BehaviorForState(CLOCK_STATE_OFF);
        return tick;
    }

    tick.state = snapshot.state;
    tick.profile = ClockTruth_BehaviorForState(snapshot.state);
    tick.source = snapshot.source;
    tick.beat_valid = snapshot.beat_valid;
    tick.beat_event_count = snapshot.beat_event_count;
    tick.beat_quarter_note_count = snapshot.beat_quarter_note_count;
    tick.beat_anchor_us = snapshot.beat_anchor_us;
    tick.beat_is_downbeat = (snapshot.beat_valid
        && (snapshot.beat_quarter_note_count % 4U) == 0U) ? 1U : 0U;

    return tick;
}

uint8_t ClockEngine_GetStateTrace(ClockEngineStateTrace_t *trace)
{
    uint32_t primask;

    if (!trace)
        return 0U;

    primask = __get_PRIMASK();
    __disable_irq();
    *trace = clock_engine_last_state_trace;
    if (primask == 0U)
        __enable_irq();

    return 1U;
}

uint8_t ClockEngine_GetBeatEvent(ClockEngineBeatEvent_t *event)
{
    ClockEngineSnapshot_t snapshot;

    if (!event)
        return 0U;

    /**
     * Beat data is now captured atomically during CaptureSnapshot().
     * Reading from the snapshot eliminates the hybrid-authority race
     * that previously existed between ClockEngine and MidiClock ISR.
     */
    if (!ClockEngine_GetSnapshot(&snapshot))
        return 0U;

    if (!snapshot.beat_valid)
        return 0U;

    event->event_count = snapshot.beat_event_count;
    event->quarter_note_count = snapshot.beat_quarter_note_count;
    event->anchor_us = snapshot.beat_anchor_us;
    event->source = snapshot.source;
    event->downbeat = (uint8_t)((snapshot.beat_quarter_note_count % 4U) == 0U ? 1U : 0U);

    return 1U;
}
