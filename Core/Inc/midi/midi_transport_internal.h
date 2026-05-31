#ifndef MIDI_TRANSPORT_INTERNAL_H
#define MIDI_TRANSPORT_INTERNAL_H

/* Internal-only transport header.
 *
 * To prevent dual-truth regressions, this header requires an explicit opt-in
 * macro from approved core timing modules. Non-core consumers should read
 * timing/sync state through ClockEngine APIs.
 */
#if !defined(MIDI_TRANSPORT_INTERNAL_ACCESS)
#error "midi_transport_internal.h is internal-only. Use clock_engine.h from non-core modules."
#endif

#include <stdint.h>

#include "midi_functions.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MIDI_CLOCK_BPM_WINDOW_PULSES    96U
#define MIDI_CLOCK_US_PER_MS            1000U
#define MIDI_CLOCK_LOST_TIMEOUT_MIN_MS  250U
#define MIDI_CLOCK_LOST_TIMEOUT_PAD_MS  40U
#define MIDI_CLOCK_LOST_TIMEOUT_PULSES  8U
#define MIDI_CLOCK_DIAGNOSTICS_ENABLED  1U
#define MIDI_CLOCK_DIAGNOSTIC_REPORT_MS 1000U

extern volatile uint32_t midi_clock_last_pulse_us;
extern volatile uint32_t midi_clock_diag_interval_sum_us;
extern volatile uint32_t midi_clock_diag_interval_min_us;
extern volatile uint32_t midi_clock_diag_interval_max_us;
extern volatile uint16_t midi_clock_diag_interval_count;
extern volatile uint32_t midi_clock_pulse_interval_sum_us;
extern volatile uint8_t midi_clock_pulse_interval_count;
extern volatile uint32_t midi_clock_external_activity_timeout_us;
extern volatile uint32_t midi_clock_last_captured_pulse_us;
extern volatile uint16_t midi_clock_external_bpm_x10;
extern volatile uint8_t midi_clock_external_bpm_valid;
extern volatile uint8_t midi_clock_external_bpm_window_pulses;
extern volatile uint8_t midi_barbeat_valid;
extern volatile uint32_t midi_transport_global_tick_count;
extern volatile uint32_t midi_transport_origin_tick_count;
extern volatile uint32_t midi_transport_last_quarter_note_count;
extern volatile uint32_t midi_transport_quarter_note_event_count;
extern volatile uint32_t midi_transport_last_quarter_note_anchor_us;
extern volatile uint8_t midi_clock_sync_lost;
extern volatile uint8_t midi_clock_recovery_hint;
extern volatile uint8_t midi_transport_running;
extern volatile uint8_t midi_transport_stop_latched;
extern volatile uint8_t midi_transport_rearm_required;
extern volatile MidiTransportEvent_t midi_transport_event;

__attribute__((always_inline))
static inline uint8_t MidiTransport_IsFlashBusyFast(void)
{
    return ((FLASH->SR & FLASH_SR_BSY) != 0U) ? 1U : 0U;
}

__attribute__((section(".RamFunc")))
uint32_t MidiTransport_ComputeActivityTimeoutUs(uint32_t pulse_interval_sum_us,
                                                uint8_t pulse_interval_count);
void MidiTransport_NoteQuarterServiceLatency(uint32_t latency_us);
__attribute__((section(".RamFunc")))
void MidiTransport_ResetClockTracking(void);
__attribute__((section(".RamFunc")))
void MidiTransport_OnStart(uint32_t now);
__attribute__((section(".RamFunc")))
void MidiTransport_OnContinue(uint32_t now);
__attribute__((section(".RamFunc")))
void MidiTransport_OnStop(uint32_t now);
__attribute__((section(".RamFunc")))
void MidiTransport_ResetForInternalTempo(void);
MidiTransportEvent_t MidiTransport_TakeEvent(void);
__attribute__((section(".RamFunc")))
void MidiTransport_OnClockPulse(uint32_t now);
__attribute__((section(".RamFunc")))
uint8_t MidiTransport_HandleRealtimeByteFast(uint8_t byte, uint32_t now);
uint8_t MidiTransportParser_IsSyncByte(uint8_t byte);
__attribute__((section(".RamFunc")))
void MidiTransportCycle_Reset(void);
__attribute__((section(".RamFunc")))
void MidiTransportCycle_Arm(void);
__attribute__((section(".RamFunc")))
uint8_t MidiTransportCycle_OnClockPulse(void);
uint8_t MidiTransportCycle_GetBarBeat(uint8_t *bar, uint8_t *beat);
__attribute__((section(".RamFunc")))
void MidiTransport_ResetObservedState(void);
__attribute__((section(".RamFunc")))
void MidiTransport_ClearRecoveryHint(void);
__attribute__((section(".RamFunc")))
void MidiTransport_NoteClockDuringRecoveryWait(uint32_t now);
uint8_t MidiTransport_IsRecoveryClockActive(void);
void MidiTransport_NoteDiagnosticInterval(uint32_t interval_us);
void MidiTransport_UpdateSyncState(void);
void MidiTransport_ServiceSyncLifecycle(void);
uint8_t MidiTransport_IsExternalClockActive(void);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_TRANSPORT_INTERNAL_H */