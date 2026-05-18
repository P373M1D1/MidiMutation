#include "midi/midi_monitor.h"

#include "main.h"

typedef struct
{
    uint8_t running_status;
    uint8_t data[2];
    uint8_t data_count;
    uint8_t expected_data_count;
    uint8_t in_sysex;
} MidiMonitorParserState_t;

#define MIDI_MONITOR_CHANNEL_STATUS_MASK   0x0FU
#define MIDI_MONITOR_DATA_MASK             0x7FU
#define MIDI_MONITOR_STATUS_BIT            0x80U
#define MIDI_MONITOR_PROGRAM_CHANGE_STATUS 0xC0U
#define MIDI_MONITOR_CONTROL_CHANGE_STATUS 0xB0U
#define MIDI_MONITOR_REALTIME_START        0xFAU
#define MIDI_MONITOR_REALTIME_CONTINUE     0xFBU
#define MIDI_MONITOR_REALTIME_STOP         0xFCU
#define MIDI_MONITOR_REALTIME_STATUS_FIRST 0xF8U

static MidiMonitorEntry_t midi_monitor_entries[MIDI_MONITOR_ENTRY_CAPACITY];
static volatile uint8_t midi_monitor_head = 0U;
static volatile uint8_t midi_monitor_count = 0U;
static volatile uint32_t midi_monitor_revision = 0U;
static MidiMonitorParserState_t midi_monitor_uart2_parser = { 0U };
static MidiMonitorParserState_t midi_monitor_uart4_parser = { 0U };

static void MidiMonitor_ResetParser(MidiMonitorParserState_t *parser);
static uint8_t MidiMonitor_ExpectedDataCount(uint8_t status);
static MidiMonitorParserState_t *MidiMonitor_GetParser(uint8_t source_uart);
static void MidiMonitor_PushEntry(uint8_t source_uart,
                                  uint8_t type,
                                  uint8_t channel,
                                  uint8_t value1,
                                  uint8_t value2);

void MidiMonitor_Init(void)
{
    midi_monitor_head = 0U;
    midi_monitor_count = 0U;
    midi_monitor_revision = 0U;
    MidiMonitor_ResetParser(&midi_monitor_uart2_parser);
    MidiMonitor_ResetParser(&midi_monitor_uart4_parser);
}

void MidiMonitor_ReceiveByte(uint8_t source_uart, uint8_t byte)
{
    MidiMonitorParserState_t *parser = MidiMonitor_GetParser(source_uart);

    if (!parser)
        return;

    if (byte >= MIDI_MONITOR_REALTIME_STATUS_FIRST)
    {
        if (byte == MIDI_MONITOR_REALTIME_START)
        {
            MidiMonitor_PushEntry(source_uart,
                                  MIDI_MONITOR_MESSAGE_START,
                                  0U,
                                  MIDI_MONITOR_VALUE_UNUSED,
                                  MIDI_MONITOR_VALUE_UNUSED);
        }
        else if (byte == MIDI_MONITOR_REALTIME_CONTINUE)
        {
            MidiMonitor_PushEntry(source_uart,
                                  MIDI_MONITOR_MESSAGE_CONTINUE,
                                  0U,
                                  MIDI_MONITOR_VALUE_UNUSED,
                                  MIDI_MONITOR_VALUE_UNUSED);
        }
        else if (byte == MIDI_MONITOR_REALTIME_STOP)
        {
            MidiMonitor_PushEntry(source_uart,
                                  MIDI_MONITOR_MESSAGE_STOP,
                                  0U,
                                  MIDI_MONITOR_VALUE_UNUSED,
                                  MIDI_MONITOR_VALUE_UNUSED);
        }

        return;
    }

    if (parser->in_sysex)
    {
        if (byte == 0xF7U)
            parser->in_sysex = 0U;

        return;
    }

    if ((byte & MIDI_MONITOR_STATUS_BIT) != 0U)
    {
        parser->data_count = 0U;
        parser->expected_data_count = 0U;

        if (byte == 0xF0U)
        {
            parser->running_status = 0U;
            parser->in_sysex = 1U;
            return;
        }

        if (byte >= 0xF0U)
        {
            parser->running_status = 0U;
            return;
        }

        parser->running_status = byte;
        parser->expected_data_count = MidiMonitor_ExpectedDataCount(byte);
        if (parser->expected_data_count == 0U)
            parser->running_status = 0U;

        return;
    }

    if (parser->running_status == 0U || parser->expected_data_count == 0U)
        return;

    parser->data[parser->data_count++] = (uint8_t)(byte & MIDI_MONITOR_DATA_MASK);
    if (parser->data_count < parser->expected_data_count)
        return;

    switch (parser->running_status & 0xF0U)
    {
    case MIDI_MONITOR_PROGRAM_CHANGE_STATUS:
        MidiMonitor_PushEntry(source_uart,
                              MIDI_MONITOR_MESSAGE_PROGRAM_CHANGE,
                              (uint8_t)((parser->running_status & MIDI_MONITOR_CHANNEL_STATUS_MASK) + 1U),
                              parser->data[0],
                              MIDI_MONITOR_VALUE_UNUSED);
        break;

    case MIDI_MONITOR_CONTROL_CHANGE_STATUS:
        MidiMonitor_PushEntry(source_uart,
                              MIDI_MONITOR_MESSAGE_CONTROL_CHANGE,
                              (uint8_t)((parser->running_status & MIDI_MONITOR_CHANNEL_STATUS_MASK) + 1U),
                              parser->data[0],
                              parser->data[1]);
        break;

    default:
        break;
    }

    parser->data_count = 0U;
}

void MidiMonitor_Clear(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    midi_monitor_head = 0U;
    midi_monitor_count = 0U;
    midi_monitor_revision++;
    if (primask == 0U)
        __enable_irq();
}

uint32_t MidiMonitor_GetRevision(void)
{
    uint32_t primask = __get_PRIMASK();
    uint32_t revision;

    __disable_irq();
    revision = midi_monitor_revision;
    if (primask == 0U)
        __enable_irq();

    return revision;
}

uint8_t MidiMonitor_CopyEntries(MidiMonitorEntry_t *dest, uint8_t capacity)
{
    uint32_t primask = __get_PRIMASK();
    uint8_t count;
    uint8_t head;
    uint8_t start_index;

    if (!dest || capacity == 0U)
        return 0U;

    __disable_irq();
    count = midi_monitor_count;
    head = midi_monitor_head;
    if (count > capacity)
        count = capacity;

    start_index = (uint8_t)((MIDI_MONITOR_ENTRY_CAPACITY + head - count) % MIDI_MONITOR_ENTRY_CAPACITY);
    for (uint8_t index = 0U; index < count; ++index)
        dest[index] = midi_monitor_entries[(uint8_t)((start_index + index) % MIDI_MONITOR_ENTRY_CAPACITY)];

    if (primask == 0U)
        __enable_irq();

    return count;
}

static void MidiMonitor_ResetParser(MidiMonitorParserState_t *parser)
{
    if (!parser)
        return;

    parser->running_status = 0U;
    parser->data[0] = 0U;
    parser->data[1] = 0U;
    parser->data_count = 0U;
    parser->expected_data_count = 0U;
    parser->in_sysex = 0U;
}

static uint8_t MidiMonitor_ExpectedDataCount(uint8_t status)
{
    switch (status & 0xF0U)
    {
    case 0x80U:
    case 0x90U:
    case 0xA0U:
    case 0xB0U:
    case 0xE0U:
        return 2U;

    case 0xC0U:
    case 0xD0U:
        return 1U;

    default:
        return 0U;
    }
}

static MidiMonitorParserState_t *MidiMonitor_GetParser(uint8_t source_uart)
{
    switch (source_uart)
    {
    case MIDI_MONITOR_SOURCE_UART2:
        return &midi_monitor_uart2_parser;

    case MIDI_MONITOR_SOURCE_UART4:
        return &midi_monitor_uart4_parser;

    default:
        return NULL;
    }
}

static void MidiMonitor_PushEntry(uint8_t source_uart,
                                  uint8_t type,
                                  uint8_t channel,
                                  uint8_t value1,
                                  uint8_t value2)
{
    uint8_t entry_index = midi_monitor_head;

    midi_monitor_entries[entry_index].source_uart = source_uart;
    midi_monitor_entries[entry_index].type = type;
    midi_monitor_entries[entry_index].channel = channel;
    midi_monitor_entries[entry_index].value1 = value1;
    midi_monitor_entries[entry_index].value2 = value2;

    midi_monitor_head = (uint8_t)((midi_monitor_head + 1U) % MIDI_MONITOR_ENTRY_CAPACITY);
    if (midi_monitor_count < MIDI_MONITOR_ENTRY_CAPACITY)
        midi_monitor_count++;

    midi_monitor_revision++;
}