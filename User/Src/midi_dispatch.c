#include "midi_dispatch.h"

#include "midi/midi_monitor.h"
#include "midi/midi_output.h"
#include "midi_functions.h"
#include "stm32f4xx_hal.h"

#include <stdio.h>

#if defined(DEBUG)
#include <assert.h>
#endif

/* Reliable semantic queue. It deliberately knows nothing about UART bytes or
 * clock windows; tracked MIDI_Send* calls remain the ownership boundary. */
typedef struct {
    uint32_t sequence;
    uint8_t type;
    uint8_t channel;
    uint8_t data1;
    uint8_t data2;
} MidiDispatchInflightCommand_t;

static ReliableMidiCommand_t midi_dispatch_queue[MIDI_DISPATCH_QUEUE_CAPACITY];
static uint8_t midi_dispatch_head = 0U;
static uint8_t midi_dispatch_tail = 0U;
static uint8_t midi_dispatch_count = 0U;
static uint8_t midi_dispatch_pending_peak = 0U;
static MidiDispatchInflightCommand_t
    midi_dispatch_inflight_commands[MIDI_DISPATCH_INFLIGHT_CAPACITY];
static uint8_t midi_dispatch_inflight_head = 0U;
static uint8_t midi_dispatch_inflight_tail = 0U;
static uint8_t midi_dispatch_inflight_count = 0U;
static uint32_t midi_dispatch_next_sequence = 1U;
static uint32_t midi_dispatch_max_age_ms = 0U;
static uint32_t midi_dispatch_submitted_count = 0U;
static uint32_t midi_dispatch_transport_inflight = 0U;
static uint32_t midi_dispatch_enqueued_total = 0U;
static uint32_t midi_dispatch_emitted_count = 0U;
static uint32_t midi_dispatch_expired_count = 0U;
static uint32_t midi_dispatch_rejected_count = 0U;
static uint32_t midi_dispatch_superseded_count = 0U;
static uint32_t midi_dispatch_enqueue_fail_count = 0U;
static uint32_t midi_dispatch_inflight_block_count = 0U;
static uint32_t midi_dispatch_stall_count = 0U;
static uint32_t midi_dispatch_completion_mismatch_count = 0U;
static uint32_t midi_dispatch_service_count = 0U;
static uint32_t midi_dispatch_budget_hit_count = 0U;
static uint32_t midi_dispatch_invariant_failure_count = 0U;
static uint32_t midi_dispatch_last_submitted_sequence = 0U;
static uint32_t midi_dispatch_last_enqueued_sequence = 0U;
static uint32_t midi_dispatch_last_emitted_sequence = 0U;
static uint32_t midi_dispatch_last_expired_sequence = 0U;
static uint32_t midi_dispatch_last_rejected_sequence = 0U;
static uint32_t midi_dispatch_last_progress_ms = 0U;
static uint8_t midi_dispatch_stalled = 0U;

static uint32_t MidiDispatch_AllocateSequence(void)
{
    uint32_t sequence = midi_dispatch_next_sequence;

    midi_dispatch_next_sequence++;
    if (midi_dispatch_next_sequence == 0U)
        midi_dispatch_next_sequence = 1U;

    return sequence;
}

static uint8_t MidiDispatch_TimeReached(uint32_t now, uint32_t deadline)
{
    return ((int32_t)(now - deadline) >= 0) ? 1U : 0U;
}

static uint8_t MidiDispatch_PolicyIsSupported(MidiCommandPolicy_t policy)
{
    return (policy == MIDI_COMMAND_POLICY_RELIABLE_ORDERED
         || policy == MIDI_COMMAND_POLICY_EXPIRE_IF_LATE) ? 1U : 0U;
}

uint8_t MidiDispatch_AccountingIsValid(void)
{
    uint32_t active = (uint32_t)midi_dispatch_count
                    + midi_dispatch_transport_inflight;
    uint32_t retired = midi_dispatch_emitted_count
                     + midi_dispatch_expired_count
                     + midi_dispatch_rejected_count
                     + midi_dispatch_superseded_count;

    return ((active + retired) == midi_dispatch_submitted_count
         && midi_dispatch_transport_inflight == midi_dispatch_inflight_count) ? 1U : 0U;
}

static void MidiDispatch_CheckAccounting(void)
{
    uint8_t accounting_ok = MidiDispatch_AccountingIsValid();

    if (!accounting_ok)
        midi_dispatch_invariant_failure_count++;

#if defined(DEBUG)
    assert(accounting_ok);
#else
    (void)accounting_ok;
#endif
}

static uint8_t MidiDispatch_Submit(ReliableMidiCommand_t *command,
                                   uint8_t command_is_valid,
                                   uint32_t *sequence_out)
{
    uint32_t sequence;

    if (!command)
        return 0U;

    sequence = MidiDispatch_AllocateSequence();
    command->sequence = sequence;
    command->submitted_ms = HAL_GetTick();
    command->deadline_ms =
        (command->policy == MIDI_COMMAND_POLICY_EXPIRE_IF_LATE)
        ? command->submitted_ms + command->deadline_ms
        : 0U;

    midi_dispatch_submitted_count++;
    midi_dispatch_last_submitted_sequence = sequence;
    if (sequence_out)
        *sequence_out = sequence;

    if (__get_IPSR() != 0U
     || !command_is_valid
     || !MidiDispatch_PolicyIsSupported(command->policy)
     || midi_dispatch_count >= MIDI_DISPATCH_QUEUE_CAPACITY)
    {
        midi_dispatch_rejected_count++;
        midi_dispatch_last_rejected_sequence = sequence;
        MidiDispatch_CheckAccounting();
        return 0U;
    }

    midi_dispatch_queue[midi_dispatch_head] = *command;
    midi_dispatch_head = (uint8_t)((midi_dispatch_head + 1U) % MIDI_DISPATCH_QUEUE_CAPACITY);
    midi_dispatch_count++;
    if (midi_dispatch_count > midi_dispatch_pending_peak)
        midi_dispatch_pending_peak = midi_dispatch_count;
    if (midi_dispatch_count == 1U)
    {
        midi_dispatch_last_progress_ms = command->submitted_ms;
        midi_dispatch_stalled = 0U;
    }

    MidiDispatch_CheckAccounting();
    return 1U;
}

uint8_t MidiDispatch_SubmitProgramChange(uint8_t channel,
                                         uint8_t program,
                                         MidiCommandPolicy_t policy,
                                         uint32_t max_age_ms,
                                         uint32_t *sequence_out)
{
    uint8_t command_is_valid =
        (channel >= 1U && channel <= 16U && program <= 127U) ? 1U : 0U;
    ReliableMidiCommand_t command = {
        .type = MIDI_COMMAND_TYPE_PROGRAM_CHANGE,
        .policy = policy,
        .channel = channel,
        .payload.program_change = {
            .program = program,
        },
        .deadline_ms = max_age_ms,
    };

    return MidiDispatch_Submit(&command, command_is_valid, sequence_out);
}

uint8_t MidiDispatch_SubmitControlChange(uint8_t channel,
                                         uint8_t controller,
                                         uint8_t value,
                                         MidiCommandPolicy_t policy,
                                         uint32_t max_age_ms,
                                         uint32_t *sequence_out)
{
    uint8_t command_is_valid =
        (channel >= 1U && channel <= 16U && controller <= 127U && value <= 127U) ? 1U : 0U;
    ReliableMidiCommand_t command = {
        .type = MIDI_COMMAND_TYPE_CONTROL_CHANGE,
        .policy = policy,
        .channel = channel,
        .payload.control_change = {
            .controller = controller,
            .value = value,
        },
        .deadline_ms = max_age_ms,
    };

    return MidiDispatch_Submit(&command, command_is_valid, sequence_out);
}

static uint8_t MidiDispatch_TryEnqueue(const ReliableMidiCommand_t *command)
{
    if (!command)
        return 0U;

    switch (command->type)
    {
    case MIDI_COMMAND_TYPE_PROGRAM_CHANGE:
        return MIDI_SendProgramChangeTracked(command->channel,
                                             command->payload.program_change.program,
                                             command->sequence);

    case MIDI_COMMAND_TYPE_CONTROL_CHANGE:
        return MIDI_SendCCTracked(command->channel,
                                  command->payload.control_change.controller,
                                  command->payload.control_change.value,
                                  command->sequence);

    default:
        return 0U;
    }
}

static void MidiDispatch_RecordInflight(const ReliableMidiCommand_t *command)
{
    MidiDispatchInflightCommand_t *inflight;

    if (!command)
        return;

    inflight = &midi_dispatch_inflight_commands[midi_dispatch_inflight_head];
    inflight->sequence = command->sequence;
    inflight->channel = command->channel;
    if (command->type == MIDI_COMMAND_TYPE_PROGRAM_CHANGE)
    {
        inflight->type = MIDI_MONITOR_MESSAGE_PROGRAM_CHANGE;
        inflight->data1 = command->payload.program_change.program;
        inflight->data2 = MIDI_MONITOR_VALUE_UNUSED;
    }
    else
    {
        inflight->type = MIDI_MONITOR_MESSAGE_CONTROL_CHANGE;
        inflight->data1 = command->payload.control_change.controller;
        inflight->data2 = command->payload.control_change.value;
    }

    midi_dispatch_inflight_head =
        (uint8_t)((midi_dispatch_inflight_head + 1U)
                  % MIDI_DISPATCH_INFLIGHT_CAPACITY);
    midi_dispatch_inflight_count++;
}

static void MidiDispatch_ProcessCompletions(void)
{
    for (uint8_t completed = 0U;
         completed < MIDI_DISPATCH_COMPLETION_BUDGET;
         ++completed)
    {
        uint32_t sequence;
        uint32_t completed_us;
        MidiDispatchInflightCommand_t *inflight;

        if (!MidiOutput_TakeTrackedCompletion(&sequence, &completed_us))
            break;

        inflight = &midi_dispatch_inflight_commands[midi_dispatch_inflight_tail];
        if (midi_dispatch_inflight_count == 0U
         || inflight->sequence != sequence)
        {
            midi_dispatch_completion_mismatch_count++;
            continue;
        }

        MidiMonitor_RecordSentMessage(inflight->type,
                                      inflight->channel,
                                      inflight->data1,
                                      inflight->data2,
                                      completed_us);
        midi_dispatch_inflight_tail =
            (uint8_t)((midi_dispatch_inflight_tail + 1U)
                      % MIDI_DISPATCH_INFLIGHT_CAPACITY);
        midi_dispatch_inflight_count--;
        midi_dispatch_transport_inflight--;
        midi_dispatch_emitted_count++;
        midi_dispatch_last_emitted_sequence = sequence;
        MidiDispatch_CheckAccounting();
    }
}

static void MidiDispatch_RetirePendingHead(void)
{
    if (midi_dispatch_count == 0U)
        return;

    midi_dispatch_tail = (uint8_t)((midi_dispatch_tail + 1U) % MIDI_DISPATCH_QUEUE_CAPACITY);
    midi_dispatch_count--;
}

void MidiDispatch_Service(void)
{
    uint8_t budget_used = 0U;
    uint32_t service_now;

    midi_dispatch_service_count++;
    MidiDispatch_ProcessCompletions();

    for (budget_used = 0U; budget_used < MIDI_DISPATCH_BUDGET; ++budget_used)
    {
        ReliableMidiCommand_t *command;
        uint32_t now;
        uint32_t age_ms;

        if (midi_dispatch_count == 0U)
            break;

        command = &midi_dispatch_queue[midi_dispatch_tail];
        now = HAL_GetTick();
        age_ms = now - command->submitted_ms;
        if (age_ms > midi_dispatch_max_age_ms)
            midi_dispatch_max_age_ms = age_ms;

        if (command->policy == MIDI_COMMAND_POLICY_EXPIRE_IF_LATE
         && MidiDispatch_TimeReached(now, command->deadline_ms))
        {
            midi_dispatch_expired_count++;
            midi_dispatch_last_expired_sequence = command->sequence;
            MidiDispatch_RetirePendingHead();
            midi_dispatch_last_progress_ms = now;
            midi_dispatch_stalled = 0U;
            MidiDispatch_CheckAccounting();
            continue;
        }

        if (midi_dispatch_inflight_count >= MIDI_DISPATCH_INFLIGHT_CAPACITY)
        {
            midi_dispatch_inflight_block_count++;
            break;
        }

        /* Exactly one transport enqueue attempt is allowed for this budget
         * slot. Failure leaves the command pending and ends this service pass. */
        if (!MidiDispatch_TryEnqueue(command))
        {
            midi_dispatch_enqueue_fail_count++;
            break;
        }

        midi_dispatch_transport_inflight++;
        midi_dispatch_enqueued_total++;
        midi_dispatch_last_enqueued_sequence = command->sequence;
        MidiDispatch_RecordInflight(command);
        MidiDispatch_RetirePendingHead();
        midi_dispatch_last_progress_ms = now;
        midi_dispatch_stalled = 0U;
        MidiDispatch_CheckAccounting();
    }

    if (budget_used == MIDI_DISPATCH_BUDGET && midi_dispatch_count != 0U)
        midi_dispatch_budget_hit_count++;

    service_now = HAL_GetTick();
    if (midi_dispatch_count == 0U)
    {
        midi_dispatch_stalled = 0U;
    }
    else if (!midi_dispatch_stalled
          && (service_now - midi_dispatch_last_progress_ms) >= MIDI_DISPATCH_STALL_THRESHOLD_MS)
    {
        midi_dispatch_stalled = 1U;
        midi_dispatch_stall_count++;
    }
}

uint8_t MidiDispatch_HasSubmissionCapacity(void)
{
    return (midi_dispatch_count < MIDI_DISPATCH_QUEUE_CAPACITY) ? 1U : 0U;
}

uint8_t MidiDispatch_HasEmittedSequence(uint32_t sequence)
{
    if (sequence == 0U || midi_dispatch_last_emitted_sequence == 0U)
        return 0U;

    return ((int32_t)(midi_dispatch_last_emitted_sequence - sequence) >= 0)
        ? 1U
        : 0U;
}

void MidiDispatch_GetProgressSnapshot(MidiDispatchProgressSnapshot_t *snapshot)
{
    MidiOutputTrackedDiagnostics_t tracked_diagnostics = { 0U, 0U, 0U };
    uint32_t now;

    if (!snapshot)
        return;

    now = HAL_GetTick();
    MidiOutput_GetTrackedDiagnostics(&tracked_diagnostics);
    snapshot->pending_depth = midi_dispatch_count;
    snapshot->pending_peak = midi_dispatch_pending_peak;
    snapshot->oldest_pending_ms = (midi_dispatch_count != 0U)
        ? midi_dispatch_queue[midi_dispatch_tail].submitted_ms
        : 0U;
    snapshot->oldest_age_ms = (midi_dispatch_count != 0U)
        ? now - snapshot->oldest_pending_ms
        : 0U;
    if (snapshot->oldest_age_ms > midi_dispatch_max_age_ms)
        midi_dispatch_max_age_ms = snapshot->oldest_age_ms;
    snapshot->max_age_ms = midi_dispatch_max_age_ms;
    snapshot->submitted_count = midi_dispatch_submitted_count;
    snapshot->active_count = (uint32_t)midi_dispatch_count
                           + midi_dispatch_transport_inflight;
    snapshot->transport_inflight = midi_dispatch_transport_inflight;
    snapshot->retired_count = midi_dispatch_emitted_count
                            + midi_dispatch_expired_count
                            + midi_dispatch_rejected_count
                            + midi_dispatch_superseded_count;
    snapshot->enqueued_total = midi_dispatch_enqueued_total;
    snapshot->emitted_count = midi_dispatch_emitted_count;
    snapshot->expired_count = midi_dispatch_expired_count;
    snapshot->rejected_count = midi_dispatch_rejected_count;
    snapshot->superseded_count = midi_dispatch_superseded_count;
    snapshot->enqueue_fail_count = midi_dispatch_enqueue_fail_count;
    snapshot->inflight_block_count = midi_dispatch_inflight_block_count;
    snapshot->stall_count = midi_dispatch_stall_count;
    snapshot->completion_mismatch_count = midi_dispatch_completion_mismatch_count;
    snapshot->completion_overflow_count = tracked_diagnostics.completion_overflow_count;
    snapshot->completion_depth = tracked_diagnostics.completion_depth;
    snapshot->completion_peak = tracked_diagnostics.completion_peak_depth;
    snapshot->service_count = midi_dispatch_service_count;
    snapshot->budget_hit_count = midi_dispatch_budget_hit_count;
    snapshot->invariant_failure_count = midi_dispatch_invariant_failure_count;
    snapshot->last_submitted_sequence = midi_dispatch_last_submitted_sequence;
    snapshot->last_enqueued_sequence = midi_dispatch_last_enqueued_sequence;
    snapshot->last_emitted_sequence = midi_dispatch_last_emitted_sequence;
    snapshot->last_expired_sequence = midi_dispatch_last_expired_sequence;
    snapshot->last_rejected_sequence = midi_dispatch_last_rejected_sequence;
    snapshot->stalled = midi_dispatch_stalled;
    snapshot->accounting_ok = MidiDispatch_AccountingIsValid();
}

void MidiDispatch_DiagnosticService(void)
{
    static MidiDispatchProgressSnapshot_t last = { 0U };
    static uint32_t last_report_ms = 0U;
    static uint8_t report_valid = 0U;
    MidiDispatchProgressSnapshot_t current = { 0U };
    uint32_t now = HAL_GetTick();
    uint8_t changed;

    MidiDispatch_GetProgressSnapshot(&current);
    changed = (uint8_t)(!report_valid
        || current.pending_depth != last.pending_depth
        || current.submitted_count != last.submitted_count
        || current.enqueued_total != last.enqueued_total
        || current.emitted_count != last.emitted_count
        || current.expired_count != last.expired_count
        || current.rejected_count != last.rejected_count
        || current.superseded_count != last.superseded_count
        || current.enqueue_fail_count != last.enqueue_fail_count
        || current.inflight_block_count != last.inflight_block_count
        || current.stall_count != last.stall_count
        || current.completion_mismatch_count != last.completion_mismatch_count
        || current.completion_overflow_count != last.completion_overflow_count
        || current.invariant_failure_count != last.invariant_failure_count
        || current.stalled != last.stalled
        || current.accounting_ok != last.accounting_ok);
    if (!changed)
        return;

    printf("MIDIREL window_ms=%lu active=%lu pending=%u transport=%lu retired=%lu "
           "submitted=%lu enqueued=%lu emitted=%lu expired=%lu rejected=%lu superseded=%lu "
           "fail=%lu inflight_block=%lu stalls=%lu peak=%u age_ms=%lu max_age_ms=%lu "
           "budget_hits=%lu completion_depth=%u completion_peak=%u completion_overflow=%lu "
           "completion_mismatch=%lu ok=%u invariant_fail=%lu "
           "d_submit=%lu d_enqueue=%lu d_emit=%lu d_fail=%lu d_reject=%lu "
           "last_submit=%lu last_enqueue=%lu last_emit=%lu\r\n",
           (unsigned long)(report_valid ? (now - last_report_ms) : now),
           (unsigned long)current.active_count,
           (unsigned)current.pending_depth,
           (unsigned long)current.transport_inflight,
           (unsigned long)current.retired_count,
           (unsigned long)current.submitted_count,
           (unsigned long)current.enqueued_total,
           (unsigned long)current.emitted_count,
           (unsigned long)current.expired_count,
           (unsigned long)current.rejected_count,
           (unsigned long)current.superseded_count,
           (unsigned long)current.enqueue_fail_count,
           (unsigned long)current.inflight_block_count,
           (unsigned long)current.stall_count,
           (unsigned)current.pending_peak,
           (unsigned long)current.oldest_age_ms,
           (unsigned long)current.max_age_ms,
           (unsigned long)current.budget_hit_count,
           (unsigned)current.completion_depth,
           (unsigned)current.completion_peak,
           (unsigned long)current.completion_overflow_count,
           (unsigned long)current.completion_mismatch_count,
           (unsigned)current.accounting_ok,
           (unsigned long)current.invariant_failure_count,
           (unsigned long)(current.submitted_count - last.submitted_count),
           (unsigned long)(current.enqueued_total - last.enqueued_total),
           (unsigned long)(current.emitted_count - last.emitted_count),
           (unsigned long)(current.enqueue_fail_count - last.enqueue_fail_count),
           (unsigned long)(current.rejected_count - last.rejected_count),
           (unsigned long)current.last_submitted_sequence,
           (unsigned long)current.last_enqueued_sequence,
           (unsigned long)current.last_emitted_sequence);

    last = current;
    last_report_ms = now;
    report_valid = 1U;
}
