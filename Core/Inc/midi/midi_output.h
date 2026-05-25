#ifndef MIDI_OUTPUT_H
#define MIDI_OUTPUT_H

#include <stdint.h>

#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

void MidiOutput_SetUart(UART_HandleTypeDef *uart_handle);
void MidiOutput_ServiceScheduler(void);
void MidiOutput_HandleTxIrq(void);
uint8_t MidiOutput_QueueMessageBytes(const uint8_t *bytes, uint16_t length);
uint8_t MidiOutput_QueueRealtimeByte(uint8_t byte);
void MidiOutput_ResetRealtimePacingGuard(void);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_OUTPUT_H */