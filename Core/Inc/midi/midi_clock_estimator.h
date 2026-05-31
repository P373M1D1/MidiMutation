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
	MidiClockTransportConfidence_t history_confidence;
	MidiClockLockQuality_t live_lock;
	uint8_t publication_ready;
	uint8_t window_pulses;
	uint8_t observed_pulses;
} MidiClockEstimatorStatus_t;

typedef struct
{
	uint8_t acquire_aggression_level;
	uint8_t tracking_bandwidth_level;
	uint8_t hysteresis_level;
	uint8_t acquire_phase_gain_divisor;
	uint8_t acquire_frequency_gain_divisor;
	uint8_t track_phase_gain_divisor;
	uint8_t track_frequency_gain_divisor;
	uint8_t lock_enter_threshold_divisor;
	uint8_t lock_exit_threshold_divisor;
	uint8_t lock_stable_pulses;
} MidiClockEstimatorAdaptiveTuning_t;

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
void MidiClockEstimator_GetRecoveredAbsPhaseErrorAvgUs(uint32_t *abs_phase_error_avg_us);
void MidiClockEstimator_AdjustAcquireAggressiveness(int8_t delta_steps);
void MidiClockEstimator_AdjustTrackingBandwidth(int8_t delta_steps);
void MidiClockEstimator_AdjustLockHysteresis(int8_t delta_steps);
void MidiClockEstimator_GetAdaptiveTuning(MidiClockEstimatorAdaptiveTuning_t *tuning);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_CLOCK_ESTIMATOR_H */