#ifndef MIDI_OUTPUT_H
#define MIDI_OUTPUT_H

#include <stdint.h>

#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	uint8_t active;
	int32_t phase_offset_us;
	int32_t velocity_us_per_s;
	uint8_t due_depth;
	uint8_t due_peak_depth;
	uint32_t enqueued_count;
	uint32_t emitted_count;
	uint32_t dropped_count;
	uint32_t clamp_min_count;
	uint32_t clamp_max_count;
	uint32_t late_avg_us;
	uint32_t late_max_us;
	uint32_t late_sample_count;
	uint32_t scheduling_jitter_est_us;
	uint32_t emit_interval_avg_us;
	uint32_t emit_interval_min_us;
	uint32_t emit_interval_max_us;
	uint32_t emit_interval_sample_count;
	uint32_t missed_emit_count;
	uint32_t crossing_backlog_now;
	uint32_t crossing_backlog_peak;
	uint8_t uart_clock_depth;
	uint8_t uart_clock_peak_depth;
	uint8_t uart_message_depth;
	uint8_t uart_message_peak_depth;
	uint32_t message_enqueue_attempts;
	uint32_t message_enqueue_successes;
	uint32_t message_enqueue_failures;
	uint32_t phase_nonmono_count;
} MidiOutputTimebendDiagnostics_t;

typedef struct
{
	uint8_t active;
	uint8_t due_depth;
	uint8_t uart_clock_depth;
	uint8_t uart_message_depth;
	uint32_t crossing_backlog_now;
	uint32_t crossing_backlog_peak;
	uint32_t dropped_count;
	uint32_t missed_emit_count;
	uint32_t scheduling_jitter_est_us;
} MidiOutputTimebendBacklogSnapshot_t;

typedef struct
{
	uint8_t completion_depth;
	uint8_t completion_peak_depth;
	uint32_t completion_overflow_count;
} MidiOutputTrackedDiagnostics_t;

void MidiOutput_SetUart(UART_HandleTypeDef *uart_handle);
void MidiOutput_ServiceScheduler(void);
void MidiOutput_HandleTxIrq(void);
void MidiOutput_HandleTimingCounterIrq(void);
uint8_t MidiOutput_QueueMessageBytes(const uint8_t *bytes, uint16_t length);
uint8_t MidiOutput_QueueTrackedMessageBytes(const uint8_t *bytes,
						    uint16_t length,
						    uint32_t sequence);
uint8_t MidiOutput_TakeTrackedCompletion(uint32_t *sequence,
                                         uint32_t *completed_us);
void MidiOutput_GetTrackedDiagnostics(MidiOutputTrackedDiagnostics_t *diagnostics);
uint8_t MidiOutput_QueueRealtimeByte(uint8_t byte);
void MidiOutput_ResetRealtimePacingGuard(void);
void MidiOutput_TimebendSetEncoderEnabled(uint8_t enabled);
void MidiOutput_TimebendSetExpressionEnabled(uint8_t enabled);
void MidiOutput_TimebendInjectEncoderDelta(int8_t delta);
uint8_t MidiOutput_TimebendIsEngaged(void);
void MidiOutput_TakeTimebendDiagnostics(MidiOutputTimebendDiagnostics_t *diagnostics);
void MidiOutput_GetTimebendBacklogSnapshot(MidiOutputTimebendBacklogSnapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_OUTPUT_H */
