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
__attribute__((section(".RamFunc")))
void MidiTransport_ResetClockTracking(void);
__attribute__((section(".RamFunc")))
void MidiTransport_OnStart(void);
__attribute__((section(".RamFunc")))
void MidiTransport_OnContinue(void);
__attribute__((section(".RamFunc")))
void MidiTransport_OnStop(void);
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
__attribute__((section(".RamFunc")))
void MidiTransport_ResetObservedState(void);
void MidiTransport_NoteDiagnosticInterval(uint32_t interval_us);
void MidiTransport_UpdateSyncState(void);
uint8_t MidiTransport_IsExternalClockActive(void);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_TRANSPORT_INTERNAL_H */