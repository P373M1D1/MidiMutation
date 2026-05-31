#include "midi/clock_engine.h"

#include "app/app_ui.h"
#include "midi_functions.h"
#define MIDI_TRANSPORT_INTERNAL_ACCESS 1
#include "midi/midi_transport_internal.h"
#undef MIDI_TRANSPORT_INTERNAL_ACCESS

static volatile ClockEngineSnapshot_t clock_engine_cached_snapshot;
static volatile uint8_t clock_engine_cached_snapshot_valid = 0U;
static volatile uint8_t clock_engine_last_status_sync_lost = 0U;
static volatile uint8_t clock_engine_last_status_signal_present = 0U;
static volatile uint8_t clock_engine_last_status_running = 0U;
static volatile MidiSyncState_t clock_engine_last_status_sync_state = MIDI_SYNC_STATE_IDLE;
static volatile uint8_t clock_engine_last_status_valid = 0U;

static void ClockEngine_CaptureSnapshot(void);

static void ClockEngine_CaptureSnapshot(void)
{
    ClockEngineSnapshot_t snapshot = {0U};
    MidiTransportPhaseSnapshot_t phase_snapshot;
    uint32_t primask;

    snapshot.running = MidiTransportIsRunning() ? 1U : 0U;
    snapshot.external_signal_present = MidiClockIsExternalSignalPresent() ? 1U : 0U;
    snapshot.sync_lost = MidiClockIsSyncLost() ? 1U : 0U;
    snapshot.estimator_valid = MidiClockIsEstimatorValid() ? 1U : 0U;
    snapshot.sync_state = MidiClockGetSyncState();
    snapshot.publication_ready = (snapshot.sync_state == MIDI_SYNC_STATE_LOCKED) ? 1U : 0U;
    snapshot.estimator_window_pulses = MidiClockGetExternalBpmWindowPulses();
    snapshot.transport_confidence = MidiClockGetTransportConfidence();
    snapshot.lock_quality = MidiClockGetLockQuality();

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

    primask = __get_PRIMASK();
    __disable_irq();
    clock_engine_cached_snapshot = snapshot;
    clock_engine_cached_snapshot_valid = 1U;
    if (primask == 0U)
        __enable_irq();
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

    previous_sync_lost = clock_engine_last_status_sync_lost;
    previous_signal_present = clock_engine_last_status_signal_present;
    previous_running = clock_engine_last_status_running;
    previous_sync_state = clock_engine_last_status_sync_state;
    previous_valid = clock_engine_last_status_valid;

    MidiTransport_ServiceSyncLifecycle();
    ClockEngine_CaptureSnapshot();

    clock_engine_last_status_sync_lost = clock_engine_cached_snapshot.sync_lost;
    clock_engine_last_status_signal_present = clock_engine_cached_snapshot.external_signal_present;
    clock_engine_last_status_running = clock_engine_cached_snapshot.running;
    clock_engine_last_status_sync_state = clock_engine_cached_snapshot.sync_state;
    clock_engine_last_status_valid = 1U;

    status_changed = (uint8_t)(!previous_valid
        || previous_sync_lost != clock_engine_cached_snapshot.sync_lost
        || previous_signal_present != clock_engine_cached_snapshot.external_signal_present
        || previous_running != clock_engine_cached_snapshot.running
        || previous_sync_state != clock_engine_cached_snapshot.sync_state);

    if (status_changed)
        AppUi_RequestStatusStripRefresh();
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
