#ifndef CLOCK_ENGINE_H
#define CLOCK_ENGINE_H

#include <stdint.h>
#include "midi/clock_truth.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    CLOCK_ENGINE_SOURCE_NONE = 0,
    CLOCK_ENGINE_SOURCE_INTERNAL,
    CLOCK_ENGINE_SOURCE_EXTERNAL,
} ClockEngineSource_t;

typedef enum
{
    CLOCK_ENGINE_TRANSITION_REASON_NONE = 0,
    CLOCK_ENGINE_TRANSITION_REASON_SIGNAL_DETECTED,
    CLOCK_ENGINE_TRANSITION_REASON_TRANSPORT_ACTIVE,
    CLOCK_ENGINE_TRANSITION_REASON_LOCK_ACQUIRED,
    CLOCK_ENGINE_TRANSITION_REASON_LOCK_DEGRADED,
    CLOCK_ENGINE_TRANSITION_REASON_ENTER_HOLDOVER,
    CLOCK_ENGINE_TRANSITION_REASON_SYNC_LOST,
    CLOCK_ENGINE_TRANSITION_REASON_SIGNAL_REMOVED,
    CLOCK_ENGINE_TRANSITION_REASON_TRANSPORT_STOPPED,
    CLOCK_ENGINE_TRANSITION_REASON_RECLASSIFIED,
} ClockEngineTransitionReason_t;

typedef struct
{
    uint32_t phase;
    uint32_t tick;
    uint8_t downbeat;
    uint8_t running;
    uint8_t transport_rearm_required;
    uint8_t external_signal_present;
    uint8_t sync_lost;
    uint8_t estimator_valid;
    uint8_t publication_ready;
    uint8_t estimator_window_pulses;
    MidiClockTransportConfidence_t transport_confidence;
    MidiClockLockQuality_t lock_quality;
    MidiSyncState_t sync_state;
    ClockEngineSource_t source;
    /**
     * Derived state and beat data baked in during snapshot capture.
     * All consumers should read these fields instead of recomputing.
     * Beat fields are sampled atomically with transport state.
     */
    ClockState_t state;
    uint32_t beat_event_count;
    uint32_t beat_quarter_note_count;
    uint32_t beat_anchor_us;
    uint8_t beat_valid;
} ClockEngineSnapshot_t;

typedef struct
{
    ClockState_t from_state;
    ClockState_t to_state;
    ClockEngineTransitionReason_t reason;
    uint32_t tick_ms;
    uint32_t sequence;
} ClockEngineStateTrace_t;

/**
 * Unified clock tick contract.
 *
 * All subsystems that need to make a policy or output decision should call
 * ClockEngine_GetTick() once per service cycle and consume only this struct.
 * This eliminates multiple-call divergence between state, profile, and beat.
 *
 * beat_valid: beat_anchor_us and beat_quarter_note_count are meaningful.
 * beat_is_downbeat: quarter note falls on bar boundary (beat 0 of a 4-beat bar).
 */
typedef struct
{
    ClockState_t state;
    ClockBehaviorProfile_t profile;
    uint32_t beat_event_count;
    uint32_t beat_quarter_note_count;
    uint32_t beat_anchor_us;
    uint8_t beat_valid;
    uint8_t beat_is_downbeat;
    ClockEngineSource_t source;
} ClockEngineTick_t;

typedef struct
{
    uint32_t event_count;
    uint32_t quarter_note_count;
    uint32_t anchor_us;
    ClockEngineSource_t source;
    uint8_t downbeat;
} ClockEngineBeatEvent_t;

/* Stage-1 seam: wrapper API with behavior delegated to current MIDI modules. */
void ClockEngine_Init(uint16_t startup_bpm);
void ClockEngine_ISR_OnInternalPulse(uint32_t now_us);
uint8_t ClockEngine_ISR_OnExternalRealtime(uint8_t byte, uint32_t now_us);
void ClockEngine_Service10ms(void);
uint8_t ClockEngine_GetSnapshot(ClockEngineSnapshot_t *snapshot);
uint8_t ClockEngine_IsRunning(void);
uint8_t ClockEngine_IsSyncLost(void);
uint8_t ClockEngine_IsExternalSignalPresent(void);
uint8_t ClockEngine_IsPublicationReady(void);
uint8_t ClockEngine_IsStopLatched(void);
uint8_t ClockEngine_IsEstimatorValid(void);
uint8_t ClockEngine_GetEstimatorWindowPulses(void);
MidiClockTransportConfidence_t ClockEngine_GetTransportConfidence(void);
MidiClockLockQuality_t ClockEngine_GetLockQuality(void);
MidiSyncState_t ClockEngine_GetSyncState(void);
ClockState_t ClockEngine_GetState(void);
ClockBehaviorProfile_t ClockEngine_GetBehaviorProfile(void);
ClockEngineTick_t ClockEngine_GetTick(void);
uint8_t ClockEngine_GetStateTrace(ClockEngineStateTrace_t *trace);
uint8_t ClockEngine_GetBeatEvent(ClockEngineBeatEvent_t *event);

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_ENGINE_H */
