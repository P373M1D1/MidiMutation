#ifndef MIDI_TRANSPORT_INTERNAL_H
#define MIDI_TRANSPORT_INTERNAL_H

#include <stdint.h>

#include "midi_functions.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MIDI_CLOCK_BPM_WINDOW_PULSES    96U
#define MIDI_CLOCK_US_PER_MS            1000U
#define MIDI_CLOCK_LOST_TIMEOUT_MIN_MS  250U
#define MIDI_CLOCK_LOST_TIMEOUT_PAD_MS  20U
#define MIDI_CLOCK_LOST_TIMEOUT_PULSES  4U
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
extern volatile uint16_t midi_clock_external_bpm_x10;
extern volatile uint8_t midi_clock_external_bpm_valid;
extern volatile uint8_t midi_barbeat_valid;
extern volatile uint8_t midi_barbeat_bar;
extern volatile uint8_t midi_barbeat_beat;
extern volatile uint8_t midi_clock_sync_lost;
extern volatile uint8_t midi_transport_running;
extern volatile uint8_t midi_transport_stop_latched;
extern volatile MidiTransportEvent_t midi_transport_event;

uint32_t MidiTransport_ComputeActivityTimeoutUs(uint32_t pulse_interval_sum_us,
                                                uint8_t pulse_interval_count);
void MidiTransport_ResetClockTracking(void);
void MidiTransport_OnStart(void);
void MidiTransport_OnContinue(void);
void MidiTransport_OnStop(void);
void MidiTransport_ResetForInternalTempo(void);
MidiTransportEvent_t MidiTransport_TakeEvent(void);
void MidiTransport_OnClockPulse(uint32_t now);
uint8_t MidiTransportParser_IsSyncByte(uint8_t byte);
void MidiTransportCycle_Reset(void);
void MidiTransportCycle_Arm(void);
uint8_t MidiTransportCycle_OnClockPulse(void);
void MidiTransport_ResetObservedState(void);
void MidiTransport_NoteDiagnosticInterval(uint32_t interval_us);
void MidiTransport_UpdateSyncState(void);
uint8_t MidiTransport_IsExternalClockActive(void);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_TRANSPORT_INTERNAL_H */