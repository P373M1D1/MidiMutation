#include "midi_preset_transaction.h"

#include "midi/midi_monitor.h"
#include "midi_dispatch.h"
#include "runtime_config.h"
#include "stm32f4xx_hal.h"

#include <stdio.h>
#include <string.h>

#if defined(DEBUG)
#include <assert.h>
#endif

/* Preset expansion submits a bounded burst each foreground pass so long UI
 * frames do not stretch command dispatch latency across many frame intervals. */

typedef enum {
    MIDI_PRESET_TRANSACTION_COMMAND_PROGRAM_CHANGE = 0,
    MIDI_PRESET_TRANSACTION_COMMAND_CONTROL_CHANGE,
} MidiPresetTransactionCommandType_t;

typedef struct {
    uint8_t type;
    uint8_t channel;
    uint8_t data1;
    uint8_t data2;
} MidiPresetTransactionCommand_t;

typedef struct {
    uint32_t transaction_id;
    uint32_t created_ms;
    uint32_t first_submitted_ms;
    uint32_t first_sequence;
    uint32_t last_sequence;
    uint8_t command_count;
    uint8_t command_index;
    uint8_t target_programs[PRESET_DEVICE_SLOTS];
    MidiPresetTransactionCommand_t commands[MIDI_PRESET_TRANSACTION_MAX_COMMANDS];
} MidiPresetTransaction_t;

static MidiPresetTransaction_t
    midi_preset_transaction_queue[MIDI_PRESET_TRANSACTION_QUEUE_CAPACITY];
static MidiPresetTransaction_t midi_preset_transaction_build_scratch;
static uint8_t midi_preset_transaction_head = 0U;
static uint8_t midi_preset_transaction_tail = 0U;
static uint8_t midi_preset_transaction_count = 0U;
static uint8_t midi_preset_transaction_peak = 0U;
static uint32_t midi_preset_transaction_next_id = 1U;
static uint32_t midi_preset_transaction_max_age_ms = 0U;
static uint32_t midi_preset_transaction_max_start_delay_ms = 0U;
static uint32_t midi_preset_transaction_last_start_delay_ms = 0U;
static uint32_t midi_preset_transaction_last_completion_age_ms = 0U;
static uint32_t midi_preset_transaction_submitted_count = 0U;
static uint32_t midi_preset_transaction_accepted_count = 0U;
static uint32_t midi_preset_transaction_completed_count = 0U;
static uint32_t midi_preset_transaction_rejected_count = 0U;
static uint32_t midi_preset_transaction_failed_count = 0U;
static uint32_t midi_preset_transaction_superseded_count = 0U;
static uint32_t midi_preset_transaction_command_submitted_count = 0U;
static uint32_t midi_preset_transaction_command_completed_count = 0U;
static uint32_t midi_preset_transaction_dispatcher_wait_count = 0U;
static uint32_t midi_preset_transaction_invariant_failure_count = 0U;
static uint32_t midi_preset_transaction_last_submitted_id = 0U;
static uint32_t midi_preset_transaction_last_completed_id = 0U;
static uint32_t midi_preset_transaction_last_superseded_id = 0U;
static uint32_t midi_preset_transaction_last_terminal_sequence = 0U;

uint8_t MidiPresetTransaction_AccountingIsValid(void)
{
    uint32_t active = midi_preset_transaction_count;
    uint32_t retired = midi_preset_transaction_completed_count
                     + midi_preset_transaction_rejected_count
                     + midi_preset_transaction_failed_count
                     + midi_preset_transaction_superseded_count;

    return ((active + retired) == midi_preset_transaction_submitted_count)
        ? 1U
        : 0U;
}

static void MidiPresetTransaction_CheckAccounting(void)
{
    uint8_t accounting_ok = MidiPresetTransaction_AccountingIsValid();

    if (!accounting_ok)
        midi_preset_transaction_invariant_failure_count++;

#if defined(DEBUG)
    assert(accounting_ok);
#else
    (void)accounting_ok;
#endif
}

static uint32_t MidiPresetTransaction_AllocateId(void)
{
    uint32_t transaction_id = midi_preset_transaction_next_id;

    midi_preset_transaction_next_id++;
    if (midi_preset_transaction_next_id == 0U)
        midi_preset_transaction_next_id = 1U;

    return transaction_id;
}

static uint8_t MidiPresetTransaction_ChannelIsValid(uint8_t channel)
{
    return (channel >= 1U && channel <= 16U) ? 1U : 0U;
}

static uint8_t MidiPresetTransaction_AddProgramChange(
    MidiPresetTransaction_t *transaction,
    uint8_t channel,
    uint8_t program)
{
    MidiPresetTransactionCommand_t *command;

    if (!transaction || !MidiPresetTransaction_ChannelIsValid(channel) || program > 127U)
        return 1U;

    if (transaction->command_count >= MIDI_PRESET_TRANSACTION_MAX_COMMANDS)
        return 0U;

    command = &transaction->commands[transaction->command_count++];
    command->type = MIDI_PRESET_TRANSACTION_COMMAND_PROGRAM_CHANGE;
    command->channel = channel;
    command->data1 = program;
    command->data2 = 0U;
    return 1U;
}

static uint8_t MidiPresetTransaction_AddControlChange(
    MidiPresetTransaction_t *transaction,
    uint8_t channel,
    uint8_t controller,
    uint8_t value)
{
    MidiPresetTransactionCommand_t *command;

    if (!transaction
     || !MidiPresetTransaction_ChannelIsValid(channel)
     || controller > 127U
     || value > 127U)
    {
        return 1U;
    }

    if (transaction->command_count >= MIDI_PRESET_TRANSACTION_MAX_COMMANDS)
        return 0U;

    command = &transaction->commands[transaction->command_count++];
    command->type = MIDI_PRESET_TRANSACTION_COMMAND_CONTROL_CHANGE;
    command->channel = channel;
    command->data1 = controller;
    command->data2 = value;
    return 1U;
}

static uint8_t MidiPresetTransaction_AddAutoCcMessages(
    MidiPresetTransaction_t *transaction,
    const PresetCCSlot_t *messages)
{
    if (!transaction || !messages)
        return 1U;

    for (uint8_t index = 0U; index < RUNTIME_CONFIG_DEVICE_AUTO_CC_COUNT; ++index)
    {
        const PresetCCSlot_t *message = &messages[index];

        if (message->channel == PRESET_CC_CHANNEL_UNUSED
         || message->cc_number == PRESET_CC_NUMBER_UNUSED
         || message->value == PRESET_CC_VALUE_UNUSED)
        {
            continue;
        }

        if (!MidiPresetTransaction_AddControlChange(transaction,
                                                    message->channel,
                                                    message->cc_number,
                                                    message->value))
        {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t MidiPresetTransaction_AddFeedbackTaperCc(
    MidiPresetTransaction_t *transaction,
    uint8_t channel,
    uint8_t controller,
    uint8_t threshold,
    uint8_t reduce)
{
    uint8_t current_value = 0U;
    uint8_t tapered_value;

    if (controller == PRESET_CC_NUMBER_UNUSED || reduce == 0U)
        return 1U;

    if (!MidiMonitor_TryGetLatestControlValueAnySource(channel,
                                                       controller,
                                                       &current_value)
     || current_value <= threshold)
    {
        return 1U;
    }

    tapered_value = (current_value > reduce)
        ? (uint8_t)(current_value - reduce)
        : 0U;
    if (tapered_value == current_value)
        return 1U;

    return MidiPresetTransaction_AddControlChange(transaction,
                                                  channel,
                                                  controller,
                                                  tapered_value);
}

static uint8_t MidiPresetTransaction_Build(
    MidiPresetTransaction_t *transaction,
    const Preset_t *preset,
    const uint8_t *previous_programs,
    uint8_t send_auto_transitions)
{
    const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();

    if (!transaction || !preset)
        return 0U;

    for (uint8_t device_index = 0U;
         device_index < PRESET_DEVICE_SLOTS;
         ++device_index)
    {
        const RuntimeConfigDevice_t *device = RuntimeConfig_GetDevice(device_index);
        uint8_t program = preset->prg[device_index].program;
        uint8_t previous_program = previous_programs
            ? previous_programs[device_index]
            : PRESET_PROGRAM_NONE;
        uint8_t previous_active = (previous_program == PRESET_PROGRAM_NONE) ? 0U : 1U;
        uint8_t next_active = (program == PRESET_PROGRAM_NONE) ? 0U : 1U;

        transaction->target_programs[device_index] = program;

        if (!device)
            continue;

        if (program == PRESET_PROGRAM_NONE)
        {
            if (device->bypass.cc != PRESET_CC_NUMBER_UNUSED
             && !MidiPresetTransaction_AddControlChange(transaction,
                                                        device->channel,
                                                        device->bypass.cc,
                                                        device->bypass.value))
            {
                return 0U;
            }
        }
        else
        {
            if (!MidiPresetTransaction_AddProgramChange(transaction,
                                                        device->channel,
                                                        program))
            {
                return 0U;
            }

            if (device->active.cc != PRESET_CC_NUMBER_UNUSED
             && !MidiPresetTransaction_AddControlChange(transaction,
                                                        device->channel,
                                                        device->active.cc,
                                                        device->active.value))
            {
                return 0U;
            }
        }

        if (send_auto_transitions && previous_active != next_active)
        {
            const PresetCCSlot_t *messages = next_active
                ? device->active_auto_cc
                : device->bypass_auto_cc;

            if (!MidiPresetTransaction_AddAutoCcMessages(transaction, messages))
                return 0U;
        }

        if (program == PRESET_PROGRAM_NONE
         && global
         && global->feedback_taper_enabled)
        {
            if (!MidiPresetTransaction_AddFeedbackTaperCc(
                    transaction,
                    device->channel,
                    device->decay1.cc,
                    global->feedback_taper_threshold,
                    global->feedback_taper_reduce))
            {
                return 0U;
            }

            if (device->decay2.cc != device->decay1.cc
             && !MidiPresetTransaction_AddFeedbackTaperCc(
                    transaction,
                    device->channel,
                    device->decay2.cc,
                    global->feedback_taper_threshold,
                    global->feedback_taper_reduce))
            {
                return 0U;
            }
        }
    }

    for (uint8_t cc_index = 0U; cc_index < PRESET_CC_SLOT_COUNT; ++cc_index)
    {
        const PresetCCSlot_t *cc = &preset->cc[cc_index];

        if (cc->channel == PRESET_CC_CHANNEL_UNUSED
         || cc->cc_number == PRESET_CC_NUMBER_UNUSED
         || cc->value == PRESET_CC_VALUE_UNUSED)
        {
            continue;
        }

        if (!MidiPresetTransaction_AddControlChange(transaction,
                                                    cc->channel,
                                                    cc->cc_number,
                                                    cc->value))
        {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t MidiPresetTransaction_NewestQueueIndex(void)
{
    return (uint8_t)((midi_preset_transaction_head
                    + MIDI_PRESET_TRANSACTION_QUEUE_CAPACITY
                    - 1U)
                   % MIDI_PRESET_TRANSACTION_QUEUE_CAPACITY);
}

static void MidiPresetTransaction_CopyPresetPrograms(
    uint8_t *programs,
    const Preset_t *preset)
{
    if (!programs)
        return;

    for (uint8_t device_index = 0U;
         device_index < PRESET_DEVICE_SLOTS;
         ++device_index)
    {
        programs[device_index] = preset
            ? preset->prg[device_index].program
            : PRESET_PROGRAM_NONE;
    }
}

static uint8_t MidiPresetTransaction_Initialize(
    MidiPresetTransaction_t *transaction,
    uint32_t transaction_id,
    const Preset_t *preset,
    const uint8_t *previous_programs,
    uint8_t send_auto_transitions)
{
    if (!transaction || !preset)
        return 0U;

    memset(transaction, 0, sizeof(*transaction));
    transaction->transaction_id = transaction_id;
    transaction->created_ms = HAL_GetTick();
    return MidiPresetTransaction_Build(transaction,
                                       preset,
                                       previous_programs,
                                       send_auto_transitions);
}

uint8_t MidiPresetTransaction_Schedule(const Preset_t *preset,
                                        const Preset_t *previous_preset,
                                        uint8_t send_auto_transitions)
{
    MidiPresetTransaction_t *transaction;
    const uint8_t *previous_programs;
    uint8_t fallback_previous_programs[PRESET_DEVICE_SLOTS];
    uint32_t transaction_id = MidiPresetTransaction_AllocateId();

    midi_preset_transaction_submitted_count++;
    midi_preset_transaction_last_submitted_id = transaction_id;

    if (!preset)
    {
        midi_preset_transaction_rejected_count++;
        MidiPresetTransaction_CheckAccounting();
        return 0U;
    }

    MidiPresetTransaction_CopyPresetPrograms(fallback_previous_programs,
                                             previous_preset);

    if (midi_preset_transaction_count >= MIDI_PRESET_TRANSACTION_QUEUE_CAPACITY)
    {
        uint8_t newest_index = MidiPresetTransaction_NewestQueueIndex();
        MidiPresetTransaction_t *newest =
            &midi_preset_transaction_queue[newest_index];

        /* A transaction becomes immutable as soon as its first semantic
         * command has been submitted. Only untouched queued work may be
         * replaced by a newer user intent. */
        if (newest->command_index != 0U || newest->first_sequence != 0U)
        {
            midi_preset_transaction_rejected_count++;
            MidiPresetTransaction_CheckAccounting();
            return 0U;
        }

        if (midi_preset_transaction_count > 1U)
        {
            uint8_t predecessor_index =
                (uint8_t)((newest_index
                         + MIDI_PRESET_TRANSACTION_QUEUE_CAPACITY
                         - 1U)
                        % MIDI_PRESET_TRANSACTION_QUEUE_CAPACITY);
            previous_programs =
                midi_preset_transaction_queue[predecessor_index].target_programs;
        }
        else
        {
            previous_programs = fallback_previous_programs;
        }

        /* Build away from the live queue so a malformed replacement cannot
         * damage the still-valid transaction it was trying to supersede. */
        if (!MidiPresetTransaction_Initialize(
                &midi_preset_transaction_build_scratch,
                transaction_id,
                preset,
                previous_programs,
                send_auto_transitions))
        {
            midi_preset_transaction_failed_count++;
            MidiPresetTransaction_CheckAccounting();
            return 0U;
        }

        midi_preset_transaction_superseded_count++;
        midi_preset_transaction_last_superseded_id = newest->transaction_id;
        *newest = midi_preset_transaction_build_scratch;
        midi_preset_transaction_accepted_count++;
        MidiPresetTransaction_CheckAccounting();
        return 1U;
    }

    previous_programs = fallback_previous_programs;
    if (midi_preset_transaction_count != 0U)
    {
        previous_programs =
            midi_preset_transaction_queue[
                MidiPresetTransaction_NewestQueueIndex()].target_programs;
    }

    transaction = &midi_preset_transaction_queue[midi_preset_transaction_head];
    if (!MidiPresetTransaction_Initialize(transaction,
                                          transaction_id,
                                          preset,
                                          previous_programs,
                                          send_auto_transitions))
    {
        midi_preset_transaction_failed_count++;
        MidiPresetTransaction_CheckAccounting();
        return 0U;
    }

    midi_preset_transaction_head =
        (uint8_t)((midi_preset_transaction_head + 1U)
                  % MIDI_PRESET_TRANSACTION_QUEUE_CAPACITY);
    midi_preset_transaction_count++;
    if (midi_preset_transaction_count > midi_preset_transaction_peak)
        midi_preset_transaction_peak = midi_preset_transaction_count;
    midi_preset_transaction_accepted_count++;
    MidiPresetTransaction_CheckAccounting();
    return 1U;
}

static void MidiPresetTransaction_RetireHead(void)
{
    midi_preset_transaction_tail =
        (uint8_t)((midi_preset_transaction_tail + 1U)
                  % MIDI_PRESET_TRANSACTION_QUEUE_CAPACITY);
    midi_preset_transaction_count--;
}

void MidiPresetTransaction_Service(void)
{
    MidiPresetTransaction_t *transaction;
    uint32_t now;

    if (midi_preset_transaction_count == 0U)
        return;

    transaction = &midi_preset_transaction_queue[midi_preset_transaction_tail];
    now = HAL_GetTick();
    if ((now - transaction->created_ms) > midi_preset_transaction_max_age_ms)
        midi_preset_transaction_max_age_ms = now - transaction->created_ms;

    if (transaction->command_index >= transaction->command_count)
    {
        if (transaction->last_sequence != 0U
         && !MidiDispatch_HasEmittedSequence(transaction->last_sequence))
        {
            return;
        }

        midi_preset_transaction_completed_count++;
        midi_preset_transaction_command_completed_count += transaction->command_count;
        midi_preset_transaction_last_completed_id = transaction->transaction_id;
        midi_preset_transaction_last_terminal_sequence = transaction->last_sequence;
        midi_preset_transaction_last_start_delay_ms =
            (transaction->first_sequence != 0U)
            ? transaction->first_submitted_ms - transaction->created_ms
            : 0U;
        midi_preset_transaction_last_completion_age_ms =
            now - transaction->created_ms;
        MidiPresetTransaction_RetireHead();
        MidiPresetTransaction_CheckAccounting();
        return;
    }

    for (uint8_t budget = 0U;
         budget < MIDI_PRESET_TRANSACTION_SERVICE_BUDGET;
         ++budget)
    {
        MidiPresetTransactionCommand_t *command;
        uint32_t sequence = 0U;
        uint8_t submitted;

        if (transaction->command_index >= transaction->command_count)
            break;

        if (!MidiDispatch_HasSubmissionCapacity())
        {
            midi_preset_transaction_dispatcher_wait_count++;
            break;
        }

        command = &transaction->commands[transaction->command_index];
        if (command->type == MIDI_PRESET_TRANSACTION_COMMAND_PROGRAM_CHANGE)
        {
            submitted = MidiDispatch_SubmitProgramChange(
                command->channel,
                command->data1,
                MIDI_COMMAND_POLICY_RELIABLE_ORDERED,
                0U,
                &sequence);
        }
        else
        {
            submitted = MidiDispatch_SubmitControlChange(
                command->channel,
                command->data1,
                command->data2,
                MIDI_COMMAND_POLICY_RELIABLE_ORDERED,
                0U,
                &sequence);
        }

        if (!submitted)
        {
            midi_preset_transaction_failed_count++;
            MidiPresetTransaction_RetireHead();
            MidiPresetTransaction_CheckAccounting();
            return;
        }

        if (transaction->first_sequence == 0U)
        {
            uint32_t start_delay_ms = now - transaction->created_ms;

            transaction->first_sequence = sequence;
            transaction->first_submitted_ms = now;
            if (start_delay_ms > midi_preset_transaction_max_start_delay_ms)
                midi_preset_transaction_max_start_delay_ms = start_delay_ms;
        }

        transaction->last_sequence = sequence;
        transaction->command_index++;
        midi_preset_transaction_command_submitted_count++;
    }
}

void MidiPresetTransaction_GetDiagnostics(
    MidiPresetTransactionDiagnostics_t *diagnostics)
{
    MidiPresetTransaction_t *active = NULL;
    uint32_t now;

    if (!diagnostics)
        return;

    now = HAL_GetTick();
    if (midi_preset_transaction_count != 0U)
        active = &midi_preset_transaction_queue[midi_preset_transaction_tail];

    diagnostics->pending_depth = midi_preset_transaction_count;
    diagnostics->pending_peak = midi_preset_transaction_peak;
    diagnostics->active_command_index = active ? active->command_index : 0U;
    diagnostics->active_command_count = active ? active->command_count : 0U;
    diagnostics->active_transaction_id = active ? active->transaction_id : 0U;
    diagnostics->active_age_ms = active ? (now - active->created_ms) : 0U;
    diagnostics->max_age_ms = midi_preset_transaction_max_age_ms;
    diagnostics->active_start_delay_ms = active
        ? (active->first_sequence != 0U
            ? active->first_submitted_ms - active->created_ms
            : now - active->created_ms)
        : 0U;
    diagnostics->max_start_delay_ms =
        midi_preset_transaction_max_start_delay_ms;
    diagnostics->last_start_delay_ms =
        midi_preset_transaction_last_start_delay_ms;
    diagnostics->last_completion_age_ms =
        midi_preset_transaction_last_completion_age_ms;
    diagnostics->submitted_count = midi_preset_transaction_submitted_count;
    diagnostics->accepted_count = midi_preset_transaction_accepted_count;
    diagnostics->completed_count = midi_preset_transaction_completed_count;
    diagnostics->rejected_count = midi_preset_transaction_rejected_count;
    diagnostics->failed_count = midi_preset_transaction_failed_count;
    diagnostics->superseded_count =
        midi_preset_transaction_superseded_count;
    diagnostics->active_count = midi_preset_transaction_count;
    diagnostics->retired_count = midi_preset_transaction_completed_count
                               + midi_preset_transaction_rejected_count
                               + midi_preset_transaction_failed_count
                               + midi_preset_transaction_superseded_count;
    diagnostics->command_submitted_count =
        midi_preset_transaction_command_submitted_count;
    diagnostics->command_completed_count =
        midi_preset_transaction_command_completed_count;
    diagnostics->dispatcher_wait_count =
        midi_preset_transaction_dispatcher_wait_count;
    diagnostics->invariant_failure_count =
        midi_preset_transaction_invariant_failure_count;
    diagnostics->last_submitted_transaction_id =
        midi_preset_transaction_last_submitted_id;
    diagnostics->last_completed_transaction_id =
        midi_preset_transaction_last_completed_id;
    diagnostics->last_superseded_transaction_id =
        midi_preset_transaction_last_superseded_id;
    diagnostics->last_terminal_sequence =
        midi_preset_transaction_last_terminal_sequence;
    diagnostics->accounting_ok = MidiPresetTransaction_AccountingIsValid();
}

void MidiPresetTransaction_DiagnosticService(void)
{
    static MidiPresetTransactionDiagnostics_t last = { 0U };
    static uint8_t report_valid = 0U;
    MidiPresetTransactionDiagnostics_t current = { 0U };
    uint8_t changed;

    MidiPresetTransaction_GetDiagnostics(&current);
    changed = (uint8_t)(!report_valid
        || current.pending_depth != last.pending_depth
        || current.submitted_count != last.submitted_count
        || current.completed_count != last.completed_count
        || current.rejected_count != last.rejected_count
        || current.failed_count != last.failed_count
        || current.superseded_count != last.superseded_count
        || current.max_start_delay_ms != last.max_start_delay_ms
        || current.last_start_delay_ms != last.last_start_delay_ms
        || current.last_completion_age_ms != last.last_completion_age_ms
        || current.command_submitted_count != last.command_submitted_count
        || current.command_completed_count != last.command_completed_count
        || current.dispatcher_wait_count != last.dispatcher_wait_count
        || current.invariant_failure_count != last.invariant_failure_count
        || current.accounting_ok != last.accounting_ok);
    if (!changed)
        return;

    printf("MIDIPRESETTX depth=%u peak=%u active=%lu retired=%lu active_id=%lu "
           "cursor=%u/%u age_ms=%lu max_age_ms=%lu start_delay_ms=%lu "
           "max_start_delay_ms=%lu last_start_delay_ms=%lu "
           "last_complete_age_ms=%lu "
           "submitted=%lu accepted=%lu "
           "completed=%lu rejected=%lu superseded=%lu "
           "failed=%lu cmd_submit=%lu cmd_complete=%lu dispatcher_wait=%lu "
           "ok=%u invariant_fail=%lu last_submit_id=%lu last_complete_id=%lu "
           "last_superseded_id=%lu last_sequence=%lu\r\n",
           (unsigned)current.pending_depth,
           (unsigned)current.pending_peak,
           (unsigned long)current.active_count,
           (unsigned long)current.retired_count,
           (unsigned long)current.active_transaction_id,
           (unsigned)current.active_command_index,
           (unsigned)current.active_command_count,
           (unsigned long)current.active_age_ms,
           (unsigned long)current.max_age_ms,
           (unsigned long)current.active_start_delay_ms,
           (unsigned long)current.max_start_delay_ms,
           (unsigned long)current.last_start_delay_ms,
           (unsigned long)current.last_completion_age_ms,
           (unsigned long)current.submitted_count,
           (unsigned long)current.accepted_count,
           (unsigned long)current.completed_count,
           (unsigned long)current.rejected_count,
           (unsigned long)current.superseded_count,
           (unsigned long)current.failed_count,
           (unsigned long)current.command_submitted_count,
           (unsigned long)current.command_completed_count,
           (unsigned long)current.dispatcher_wait_count,
           (unsigned)current.accounting_ok,
           (unsigned long)current.invariant_failure_count,
           (unsigned long)current.last_submitted_transaction_id,
           (unsigned long)current.last_completed_transaction_id,
           (unsigned long)current.last_superseded_transaction_id,
           (unsigned long)current.last_terminal_sequence);

    last = current;
    report_valid = 1U;
}
