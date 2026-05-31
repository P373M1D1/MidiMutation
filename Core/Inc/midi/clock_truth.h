#ifndef CLOCK_TRUTH_H
#define CLOCK_TRUTH_H

#include "midi_functions.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Clock truth contract:
 * - ClockEngine is the single read authority for cross-domain consumers.
 * - Clock/transport internals may mutate sync/phase state.
 * - UI/app/display modules consume derived snapshots only.
 */

typedef enum
{
    CLOCK_TRUTH_MODE_NO_CLOCK = 0,
    CLOCK_TRUTH_MODE_CLOCK_PRESENT,
    CLOCK_TRUTH_MODE_SYNCING,
    CLOCK_TRUTH_MODE_LOCKED,
    CLOCK_TRUTH_MODE_HOLDOVER,
} ClockTruthMode_t;

typedef enum
{
    CLOCK_STATE_OFF = 0,
    CLOCK_STATE_DETECTING,
    CLOCK_STATE_SYNCING,
    CLOCK_STATE_LOCKED,
    CLOCK_STATE_HOLDOVER,
    CLOCK_STATE_LOST,
} ClockState_t;

typedef struct
{
    uint8_t allow_led_beat_pulse;
    uint8_t allow_bar_counter_advance;
    uint8_t allow_metronome_output;
    uint8_t timebend_overlay_only;
} ClockBehaviorProfile_t;

static inline ClockTruthMode_t ClockTruth_ClassifyMode(uint8_t external_signal_present,
                                                       uint8_t running,
                                                       uint8_t sync_lost,
                                                       MidiSyncState_t sync_state)
{
    if (sync_lost)
        return external_signal_present ? CLOCK_TRUTH_MODE_CLOCK_PRESENT : CLOCK_TRUTH_MODE_HOLDOVER;

    if (!external_signal_present)
        return CLOCK_TRUTH_MODE_NO_CLOCK;

    if (sync_state == MIDI_SYNC_STATE_LOCKED && running)
        return CLOCK_TRUTH_MODE_LOCKED;

    if (running)
        return CLOCK_TRUTH_MODE_SYNCING;

    return CLOCK_TRUTH_MODE_CLOCK_PRESENT;
}

/**
 * Maps raw transport/sync flags to a ClockState_t.
 *
 * External-present + not-running always maps to DETECTING.
 *
 * This keeps behavior deterministic for hot-plug scenarios where F8 appears
 * without a START/CONTINUE transition: no beat-derived output is allowed until
 * transport is explicitly running.
 */
static inline ClockState_t ClockTruth_ClassifyState(uint8_t external_signal_present,
                                                    uint8_t running,
                                                    uint8_t sync_lost,
                                                    MidiSyncState_t sync_state)
{
    if (sync_state == MIDI_SYNC_STATE_LOST)
        return CLOCK_STATE_LOST;

    if (sync_state == MIDI_SYNC_STATE_HOLDOVER || sync_lost)
        return CLOCK_STATE_HOLDOVER;

    if (!external_signal_present)
        return CLOCK_STATE_OFF;

    if (!running)
        return CLOCK_STATE_DETECTING;

    if (sync_state == MIDI_SYNC_STATE_LOCKED)
        return CLOCK_STATE_LOCKED;

    return CLOCK_STATE_SYNCING;
}

static inline ClockBehaviorProfile_t ClockTruth_BehaviorForState(ClockState_t state)
{
    ClockBehaviorProfile_t profile = {0U, 0U, 0U, 1U};

    switch (state)
    {
    /**
     * Internal clock is authoritative. Full beat feedback and metronome output
     * are active. Bar counter advances on internal tempo.
     */
    case CLOCK_STATE_OFF:
        profile.allow_led_beat_pulse = 1U;
        profile.allow_bar_counter_advance = 1U;
        profile.allow_metronome_output = 1U;
        break;

    /**
     * External signal is present but transport has not started.
     * No musical time is established: suppress all beat-derived output.
     */
    case CLOCK_STATE_DETECTING:
        break;

    /**
     * Transport is running, acquiring lock. Allow beat feedback and metronome
     * so the user can hear tempo during the sync window. Bar counter advances.
     */
    case CLOCK_STATE_SYNCING:
        profile.allow_led_beat_pulse = 1U;
        profile.allow_bar_counter_advance = 1U;
        profile.allow_metronome_output = 1U;
        break;

    /**
     * Fully locked to external clock. All outputs active.
     * Timebend is an overlay and does not influence clock generation.
     */
    case CLOCK_STATE_LOCKED:
        profile.allow_led_beat_pulse = 1U;
        profile.allow_bar_counter_advance = 1U;
        profile.allow_metronome_output = 1U;
        profile.timebend_overlay_only = 1U;
        break;

    /**
     * External signal lost; internal tempo is continuing from last known phase.
     * Beat feedback and metronome remain active. Bar counter advances.
     */
    case CLOCK_STATE_HOLDOVER:
        profile.allow_led_beat_pulse = 1U;
        profile.allow_bar_counter_advance = 1U;
        profile.allow_metronome_output = 1U;
        break;

    /**
     * Sync was lost without a clean handoff. Internal clock continues.
     * Beat feedback and metronome allowed. Bar counter frozen until re-lock.
     */
    case CLOCK_STATE_LOST:
        profile.allow_led_beat_pulse = 1U;
        profile.allow_metronome_output = 1U;
        break;

    default:
        break;
    }

    return profile;
}

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_TRUTH_H */
