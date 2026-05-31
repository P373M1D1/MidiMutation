#ifndef CLOCK_ENGINE_H
#define CLOCK_ENGINE_H

#include <stdint.h>
#include "midi_functions.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    CLOCK_ENGINE_SOURCE_NONE = 0,
    CLOCK_ENGINE_SOURCE_INTERNAL,
    CLOCK_ENGINE_SOURCE_EXTERNAL,
} ClockEngineSource_t;

typedef struct
{
    uint32_t phase;
    uint32_t tick;
    uint8_t downbeat;
    uint8_t running;
    uint8_t external_signal_present;
    uint8_t sync_lost;
    uint8_t estimator_valid;
    uint8_t publication_ready;
    uint8_t estimator_window_pulses;
    MidiClockTransportConfidence_t transport_confidence;
    MidiClockLockQuality_t lock_quality;
    MidiSyncState_t sync_state;
    ClockEngineSource_t source;
} ClockEngineSnapshot_t;

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

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_ENGINE_H */
