#ifndef MIDI_MONITOR_H
#define MIDI_MONITOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIDI_MONITOR_ENTRY_CAPACITY 50U
#define MIDI_MONITOR_VALUE_UNUSED   0xFFU
#define MIDI_MONITOR_DELTA_UNAVAILABLE UINT32_MAX

typedef enum
{
    MIDI_MONITOR_SOURCE_OUTPUT = 0,
    MIDI_MONITOR_SOURCE_UART2 = 2,
    MIDI_MONITOR_SOURCE_UART4 = 4,
    MIDI_MONITOR_SOURCE_UART5 = 5,
    MIDI_MONITOR_SOURCE_USART6 = 6,
    MIDI_MONITOR_SOURCE_UART9 = 9,
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
    uint32_t timestamp_us;
    uint32_t delta_us;
    uint32_t revision;
} MidiMonitorEntry_t;

void MidiMonitor_Init(void);
void MidiMonitor_ReceiveByte(uint8_t source_uart, uint8_t byte);
/* Records a command after its final byte has been handed to the MIDI UART.
 * timestamp_us must come from the free-running 1 MHz timing counter at that
 * handoff point. */
void MidiMonitor_RecordSentMessage(uint8_t type,
                                   uint8_t channel,
                                   uint8_t value1,
                                   uint8_t value2,
                                   uint32_t timestamp_us);
void MidiMonitor_Clear(void);
uint32_t MidiMonitor_GetRevision(void);
void MidiMonitor_AcknowledgeChangedEvent(void);
uint8_t MidiMonitor_CopyEntries(MidiMonitorEntry_t *dest, uint8_t capacity);
uint8_t MidiMonitor_TryGetLatestEntry(MidiMonitorEntry_t *entry_out, uint32_t *revision_out);
uint8_t MidiMonitor_TryGetLatestControlValue(uint8_t source_uart,
                                             uint8_t channel,
                                             uint8_t cc_number,
                                             uint8_t *value_out);
uint8_t MidiMonitor_TryGetLatestControlValueAnySource(uint8_t channel,
                                                      uint8_t cc_number,
                                                      uint8_t *value_out);
uint8_t MidiMonitor_TryGetLatestControlChangeAnySource(uint8_t *channel_out,
                                                       uint8_t *cc_out,
                                                       uint8_t *value_out);
uint8_t MidiMonitor_TryGetLatestProgramChangeAnySource(uint8_t *channel_out,
                                                       uint8_t *program_out);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_MONITOR_H */
