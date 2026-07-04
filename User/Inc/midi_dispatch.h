#ifndef MIDI_DISPATCH_H
#define MIDI_DISPATCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Keep service deterministic but high enough to drain one preset burst inside
 * a single foreground pass when UI frames are long. */
#define MIDI_DISPATCH_BUDGET         12U
#define MIDI_DISPATCH_COMPLETION_BUDGET 12U
#define MIDI_DISPATCH_QUEUE_CAPACITY 32U
#define MIDI_DISPATCH_INFLIGHT_CAPACITY 128U
#define MIDI_DISPATCH_STALL_THRESHOLD_MS 250U

typedef enum {
    MIDI_COMMAND_TYPE_PROGRAM_CHANGE = 0,
    MIDI_COMMAND_TYPE_CONTROL_CHANGE,
} MidiCommandType_t;

typedef enum {
    MIDI_COMMAND_POLICY_RELIABLE_ORDERED = 0,
    MIDI_COMMAND_POLICY_COALESCE_LATEST,
    MIDI_COMMAND_POLICY_EXPIRE_IF_LATE,
} MidiCommandPolicy_t;

typedef enum {
    MIDI_COMMAND_STATE_PENDING = 0,
    MIDI_COMMAND_STATE_ENQUEUED,
    MIDI_COMMAND_STATE_EMITTED,
    MIDI_COMMAND_STATE_RETIRED,
    MIDI_COMMAND_STATE_EXPIRED,
    MIDI_COMMAND_STATE_REJECTED,
    MIDI_COMMAND_STATE_SUPERSEDED,
} MidiCommandState_t;

typedef struct {
    uint32_t sequence;
    MidiCommandType_t type;
    MidiCommandPolicy_t policy;
    uint8_t channel;
    union {
        struct {
            uint8_t program;
        } program_change;
        struct {
            uint8_t controller;
            uint8_t value;
        } control_change;
    } payload;
    uint32_t submitted_ms;
    uint32_t deadline_ms;
} ReliableMidiCommand_t;

typedef struct {
    uint16_t pending_depth;
    uint16_t pending_peak;
    uint32_t oldest_pending_ms;
    uint32_t oldest_age_ms;
    uint32_t max_age_ms;
    uint32_t submitted_count;
    uint32_t active_count;
    uint32_t transport_inflight;
    uint32_t retired_count;
    uint32_t enqueued_total;
    uint32_t emitted_count;
    uint32_t expired_count;
    uint32_t rejected_count;
    uint32_t superseded_count;
    uint32_t enqueue_fail_count;
    uint32_t inflight_block_count;
    uint32_t stall_count;
    uint32_t completion_mismatch_count;
    uint32_t completion_overflow_count;
    uint16_t completion_depth;
    uint16_t completion_peak;
    uint32_t service_count;
    uint32_t budget_hit_count;
    uint32_t invariant_failure_count;
    uint32_t last_submitted_sequence;
    uint32_t last_enqueued_sequence;
    uint32_t last_emitted_sequence;
    uint32_t last_expired_sequence;
    uint32_t last_rejected_sequence;
    uint8_t stalled;
    uint8_t accounting_ok;
} MidiDispatchProgressSnapshot_t;

/* Submissions are foreground-only. max_age_ms is used only by
 * MIDI_COMMAND_POLICY_EXPIRE_IF_LATE; reliable ordered commands ignore it.
 * A rejected submission still receives a sequence and is included in the
 * accounting totals. */
uint8_t MidiDispatch_SubmitProgramChange(uint8_t channel,
                                         uint8_t program,
                                         MidiCommandPolicy_t policy,
                                         uint32_t max_age_ms,
                                         uint32_t *sequence_out);
uint8_t MidiDispatch_SubmitControlChange(uint8_t channel,
                                         uint8_t controller,
                                         uint8_t value,
                                         MidiCommandPolicy_t policy,
                                         uint32_t max_age_ms,
                                         uint32_t *sequence_out);
void MidiDispatch_Service(void);
uint8_t MidiDispatch_HasSubmissionCapacity(void);
uint8_t MidiDispatch_HasEmittedSequence(uint32_t sequence);
/* Emits one compact aggregate line only when reliability state changes. */
void MidiDispatch_DiagnosticService(void);
/* EMITTED means the UART TX ISR consumed the tracked command's final byte from
 * the normal-message ring and handed it to the UART data register. */
uint8_t MidiDispatch_AccountingIsValid(void);
void MidiDispatch_GetProgressSnapshot(MidiDispatchProgressSnapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_DISPATCH_H */
