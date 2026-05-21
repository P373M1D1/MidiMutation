#ifndef MIDI_CLOCK_ESTIMATOR_H
#define MIDI_CLOCK_ESTIMATOR_H

#include <stdint.h>

#include "midi_functions.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	uint8_t estimator_valid;
	MidiClockLockQuality_t lock_quality;
	uint8_t publication_ready;
	uint8_t window_pulses;
	uint8_t observed_pulses;
} MidiClockEstimatorStatus_t;

void MidiClockEstimator_Reset(void);
__attribute__((section(".RamFunc")))
void MidiClockEstimator_AnchorPulse(uint32_t now_us);
__attribute__((section(".RamFunc")))
void MidiClockEstimator_NotePulseInterval(uint32_t now_us, uint32_t interval_us);
void MidiClockEstimator_GetStatus(MidiClockEstimatorStatus_t *status);
__attribute__((section(".RamFunc")))
uint8_t MidiClockEstimator_GetRecoveredPulseTimestampUs(uint32_t *pulse_us);
__attribute__((section(".RamFunc")))
uint8_t MidiClockEstimator_GetRecoveredPulseIntervalUs(uint32_t *interval_us);
void MidiClockEstimator_GetRecoveredTimingSnapshot(uint32_t *pulse_us,
												   uint32_t *interval_us);
void MidiClockEstimator_GetRecoveredPllDiagnostics(int32_t *phase_error_us,
											   int32_t *phase_correction_us,
											   int32_t *frequency_correction_us,
											   uint8_t *fast_mode);
uint8_t MidiClockEstimator_GetRecoveredBpmX10(uint16_t *bpm_x10);
uint8_t MidiClockEstimator_GetMeasuredBpmX10(uint16_t *bpm_x10);
uint8_t MidiClockEstimator_GetRawBpmX10(uint16_t *bpm_x10);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_CLOCK_ESTIMATOR_H */