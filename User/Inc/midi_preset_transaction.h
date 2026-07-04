#ifndef MIDI_PRESET_TRANSACTION_H
#define MIDI_PRESET_TRANSACTION_H

#include <stdint.h>

#include "presets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Presets use active + latest semantics: one transaction may be in progress
 * and one untouched future transaction may hold the newest user request. */
#define MIDI_PRESET_TRANSACTION_QUEUE_CAPACITY 2U
#define MIDI_PRESET_TRANSACTION_MAX_COMMANDS   72U
/* Submit a bounded command burst each foreground pass so preset recalls are
 * not stretched by slow UI frames, while still keeping deterministic limits. */
#define MIDI_PRESET_TRANSACTION_SERVICE_BUDGET 8U

typedef struct {
    uint8_t pending_depth;
    uint8_t pending_peak;
    uint8_t active_command_index;
    uint8_t active_command_count;
    uint32_t active_transaction_id;
    uint32_t active_age_ms;
    uint32_t max_age_ms;
    uint32_t active_start_delay_ms;
    uint32_t max_start_delay_ms;
    uint32_t last_start_delay_ms;
    uint32_t last_completion_age_ms;
    uint32_t submitted_count;
    uint32_t accepted_count;
    uint32_t completed_count;
    uint32_t rejected_count;
    uint32_t failed_count;
    uint32_t superseded_count;
    uint32_t active_count;
    uint32_t retired_count;
    uint32_t command_submitted_count;
    uint32_t command_completed_count;
    uint32_t dispatcher_wait_count;
    uint32_t invariant_failure_count;
    uint32_t last_submitted_transaction_id;
    uint32_t last_completed_transaction_id;
    uint32_t last_superseded_transaction_id;
    uint32_t last_terminal_sequence;
    uint8_t accounting_ok;
} MidiPresetTransactionDiagnostics_t;

uint8_t MidiPresetTransaction_Schedule(const Preset_t *preset,
                                        const Preset_t *previous_preset,
                                        uint8_t send_auto_transitions);
void MidiPresetTransaction_Service(void);
void MidiPresetTransaction_DiagnosticService(void);
uint8_t MidiPresetTransaction_AccountingIsValid(void);
void MidiPresetTransaction_GetDiagnostics(MidiPresetTransactionDiagnostics_t *diagnostics);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_PRESET_TRANSACTION_H */
