#ifndef MIDI_MONITOR_H
#define MIDI_MONITOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIDI_MONITOR_ENTRY_CAPACITY 50U
#define MIDI_MONITOR_VALUE_UNUSED   0xFFU

typedef enum
{
    MIDI_MONITOR_SOURCE_UART2 = 2,
    MIDI_MONITOR_SOURCE_UART4 = 4,
} MidiMonitorSource_t;

typedef enum
{
    MIDI_MONITOR_MESSAGE_PROGRAM_CHANGE = 0,
    MIDI_MONITOR_MESSAGE_CONTROL_CHANGE,
    MIDI_MONITOR_MESSAGE_START,
    MIDI_MONITOR_MESSAGE_CONTINUE,
    MIDI_MONITOR_MESSAGE_STOP,
} MidiMonitorMessageType_t;

typedef struct
{
    uint8_t source_uart;
    uint8_t channel;
    uint8_t type;
    uint8_t value1;
    uint8_t value2;
} MidiMonitorEntry_t;

void MidiMonitor_Init(void);
void MidiMonitor_ReceiveByte(uint8_t source_uart, uint8_t byte);
void MidiMonitor_Clear(void);
uint32_t MidiMonitor_GetRevision(void);
uint8_t MidiMonitor_CopyEntries(MidiMonitorEntry_t *dest, uint8_t capacity);
uint8_t MidiMonitor_TryGetLatestControlValue(uint8_t source_uart,
                                             uint8_t channel,
                                             uint8_t cc_number,
                                             uint8_t *value_out);
uint8_t MidiMonitor_TryGetLatestControlValueAnySource(uint8_t channel,
                                                      uint8_t cc_number,
                                                      uint8_t *value_out);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_MONITOR_H */