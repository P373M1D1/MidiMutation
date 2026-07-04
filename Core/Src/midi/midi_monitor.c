#include "midi/midi_monitor.h"

#include "app_event.h"
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
#define MIDI_MONITOR_SOURCE_INDEX_COUNT    10U

static MidiMonitorEntry_t midi_monitor_entries[MIDI_MONITOR_ENTRY_CAPACITY];
static volatile uint8_t midi_monitor_head = 0U;
static volatile uint8_t midi_monitor_count = 0U;
static volatile uint32_t midi_monitor_revision = 0U;
static volatile uint8_t midi_monitor_changed_event_pending = 0U;
static MidiMonitorParserState_t midi_monitor_uart2_parser = { 0U };
static MidiMonitorParserState_t midi_monitor_uart4_parser = { 0U };
static MidiMonitorParserState_t midi_monitor_uart5_parser = { 0U };
static MidiMonitorParserState_t midi_monitor_usart6_parser = { 0U };
static MidiMonitorParserState_t midi_monitor_uart9_parser = { 0U };
static uint32_t midi_monitor_last_timestamp_us[MIDI_MONITOR_SOURCE_INDEX_COUNT];
static uint16_t midi_monitor_timestamp_valid_mask = 0U;

static void MidiMonitor_ResetParser(MidiMonitorParserState_t *parser);
static uint8_t MidiMonitor_ExpectedDataCount(uint8_t status);
static MidiMonitorParserState_t *MidiMonitor_GetParser(uint8_t source_uart);
static void MidiMonitor_QueueChangedEvent(void);
static void MidiMonitor_PushEntry(uint8_t source_uart,
                                  uint8_t type,
                                  uint8_t channel,
                                  uint8_t value1,
                                  uint8_t value2,
                                  uint32_t timestamp_us);

void MidiMonitor_Init(void)
{
    midi_monitor_head = 0U;
    midi_monitor_count = 0U;
    midi_monitor_revision = 0U;
    midi_monitor_changed_event_pending = 0U;
    midi_monitor_timestamp_valid_mask = 0U;
    for (uint8_t source = 0U; source < MIDI_MONITOR_SOURCE_INDEX_COUNT; ++source)
        midi_monitor_last_timestamp_us[source] = 0U;
    MidiMonitor_ResetParser(&midi_monitor_uart2_parser);
    MidiMonitor_ResetParser(&midi_monitor_uart4_parser);
    MidiMonitor_ResetParser(&midi_monitor_uart5_parser);
    MidiMonitor_ResetParser(&midi_monitor_usart6_parser);
    MidiMonitor_ResetParser(&midi_monitor_uart9_parser);
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
                                  MIDI_MONITOR_VALUE_UNUSED,
                                  TIM2->CNT);
        }
        else if (byte == MIDI_MONITOR_REALTIME_CONTINUE)
        {
            MidiMonitor_PushEntry(source_uart,
                                  MIDI_MONITOR_MESSAGE_CONTINUE,
                                  0U,
                                  MIDI_MONITOR_VALUE_UNUSED,
                                  MIDI_MONITOR_VALUE_UNUSED,
                                  TIM2->CNT);
        }
        else if (byte == MIDI_MONITOR_REALTIME_STOP)
        {
            MidiMonitor_PushEntry(source_uart,
                                  MIDI_MONITOR_MESSAGE_STOP,
                                  0U,
                                  MIDI_MONITOR_VALUE_UNUSED,
                                  MIDI_MONITOR_VALUE_UNUSED,
                                  TIM2->CNT);
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
                              MIDI_MONITOR_VALUE_UNUSED,
                              TIM2->CNT);
        break;

    case MIDI_MONITOR_CONTROL_CHANGE_STATUS:
        MidiMonitor_PushEntry(source_uart,
                              MIDI_MONITOR_MESSAGE_CONTROL_CHANGE,
                              (uint8_t)((parser->running_status & MIDI_MONITOR_CHANNEL_STATUS_MASK) + 1U),
                              parser->data[0],
                              parser->data[1],
                              TIM2->CNT);
        break;

    default:
        break;
    }

    parser->data_count = 0U;
}

void MidiMonitor_RecordSentMessage(uint8_t type,
                                   uint8_t channel,
                                   uint8_t value1,
                                   uint8_t value2,
                                   uint32_t timestamp_us)
{
    uint32_t primask;

    if (channel < 1U || channel > 16U)
        return;

    if (type == MIDI_MONITOR_MESSAGE_PROGRAM_CHANGE)
    {
        if (value1 > MIDI_MONITOR_DATA_MASK)
            return;
        value2 = MIDI_MONITOR_VALUE_UNUSED;
    }
    else if (type == MIDI_MONITOR_MESSAGE_CONTROL_CHANGE)
    {
        if (value1 > MIDI_MONITOR_DATA_MASK || value2 > MIDI_MONITOR_DATA_MASK)
            return;
    }
    else
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    MidiMonitor_PushEntry(MIDI_MONITOR_SOURCE_OUTPUT,
                          type,
                          channel,
                          value1,
                          value2,
                          timestamp_us);
    if (primask == 0U)
        __enable_irq();
}

void MidiMonitor_Clear(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    midi_monitor_head = 0U;
    midi_monitor_count = 0U;
    midi_monitor_timestamp_valid_mask = 0U;
    midi_monitor_revision++;
    MidiMonitor_QueueChangedEvent();
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

void MidiMonitor_AcknowledgeChangedEvent(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    midi_monitor_changed_event_pending = 0U;
    if (primask == 0U)
        __enable_irq();
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

uint8_t MidiMonitor_TryGetLatestEntry(MidiMonitorEntry_t *entry_out, uint32_t *revision_out)
{
    uint32_t primask = __get_PRIMASK();

    if (!entry_out || !revision_out)
        return 0U;

    __disable_irq();
    if (midi_monitor_count == 0U)
    {
        if (primask == 0U)
            __enable_irq();
        return 0U;
    }

    for (uint8_t offset = 0U; offset < midi_monitor_count; ++offset)
    {
        uint8_t index =
            (uint8_t)((MIDI_MONITOR_ENTRY_CAPACITY + midi_monitor_head - 1U - offset)
                      % MIDI_MONITOR_ENTRY_CAPACITY);

        if (midi_monitor_entries[index].source_uart == MIDI_MONITOR_SOURCE_OUTPUT)
            continue;

        *entry_out = midi_monitor_entries[index];
        *revision_out = midi_monitor_entries[index].revision;
        if (primask == 0U)
            __enable_irq();
        return 1U;
    }

    if (primask == 0U)
        __enable_irq();

    return 0U;
}

uint8_t MidiMonitor_TryGetLatestControlValue(uint8_t source_uart,
                                             uint8_t channel,
                                             uint8_t cc_number,
                                             uint8_t *value_out)
{
    uint32_t primask = __get_PRIMASK();
    uint8_t count;
    uint8_t head;

    if (!value_out)
        return 0U;

    __disable_irq();
    count = midi_monitor_count;
    head = midi_monitor_head;

    for (uint8_t offset = 0U; offset < count; ++offset)
    {
        uint8_t index = (uint8_t)((MIDI_MONITOR_ENTRY_CAPACITY + head - 1U - offset) % MIDI_MONITOR_ENTRY_CAPACITY);
        const MidiMonitorEntry_t *entry = &midi_monitor_entries[index];

        if (entry->type != MIDI_MONITOR_MESSAGE_CONTROL_CHANGE)
            continue;
        if (entry->source_uart != source_uart)
            continue;
        if (entry->channel != channel)
            continue;
        if (entry->value1 != cc_number)
            continue;

        *value_out = entry->value2;
        if (primask == 0U)
            __enable_irq();
        return 1U;
    }

    if (primask == 0U)
        __enable_irq();

    return 0U;
}

uint8_t MidiMonitor_TryGetLatestControlValueAnySource(uint8_t channel,
                                                      uint8_t cc_number,
                                                      uint8_t *value_out)
{
    uint32_t primask = __get_PRIMASK();
    uint8_t count;
    uint8_t head;

    if (!value_out)
        return 0U;

    __disable_irq();
    count = midi_monitor_count;
    head = midi_monitor_head;

    for (uint8_t offset = 0U; offset < count; ++offset)
    {
        uint8_t index = (uint8_t)((MIDI_MONITOR_ENTRY_CAPACITY + head - 1U - offset) % MIDI_MONITOR_ENTRY_CAPACITY);
        const MidiMonitorEntry_t *entry = &midi_monitor_entries[index];

        if (entry->type != MIDI_MONITOR_MESSAGE_CONTROL_CHANGE)
            continue;
        if (entry->source_uart == MIDI_MONITOR_SOURCE_OUTPUT)
            continue;
        if (entry->channel != channel)
            continue;
        if (entry->value1 != cc_number)
            continue;

        *value_out = entry->value2;
        if (primask == 0U)
            __enable_irq();
        return 1U;
    }

    if (primask == 0U)
        __enable_irq();

    return 0U;
}

uint8_t MidiMonitor_TryGetLatestControlChangeAnySource(uint8_t *channel_out,
                                                       uint8_t *cc_out,
                                                       uint8_t *value_out)
{
    uint32_t primask = __get_PRIMASK();
    uint8_t count;
    uint8_t head;

    if (!channel_out || !cc_out || !value_out)
        return 0U;

    __disable_irq();
    count = midi_monitor_count;
    head = midi_monitor_head;

    for (uint8_t offset = 0U; offset < count; ++offset)
    {
        uint8_t index = (uint8_t)((MIDI_MONITOR_ENTRY_CAPACITY + head - 1U - offset) % MIDI_MONITOR_ENTRY_CAPACITY);
        const MidiMonitorEntry_t *entry = &midi_monitor_entries[index];

        if (entry->type != MIDI_MONITOR_MESSAGE_CONTROL_CHANGE)
            continue;
        if (entry->source_uart == MIDI_MONITOR_SOURCE_OUTPUT)
            continue;

        *channel_out = entry->channel;
        *cc_out = entry->value1;
        *value_out = entry->value2;
        if (primask == 0U)
            __enable_irq();
        return 1U;
    }

    if (primask == 0U)
        __enable_irq();

    return 0U;
}

uint8_t MidiMonitor_TryGetLatestProgramChangeAnySource(uint8_t *channel_out,
                                                       uint8_t *program_out)
{
    uint32_t primask = __get_PRIMASK();
    uint8_t count;
    uint8_t head;

    if (!channel_out || !program_out)
        return 0U;

    __disable_irq();
    count = midi_monitor_count;
    head = midi_monitor_head;

    for (uint8_t offset = 0U; offset < count; ++offset)
    {
        uint8_t index = (uint8_t)((MIDI_MONITOR_ENTRY_CAPACITY + head - 1U - offset) % MIDI_MONITOR_ENTRY_CAPACITY);
        const MidiMonitorEntry_t *entry = &midi_monitor_entries[index];

        if (entry->type != MIDI_MONITOR_MESSAGE_PROGRAM_CHANGE)
            continue;
        if (entry->source_uart == MIDI_MONITOR_SOURCE_OUTPUT)
            continue;

        *channel_out = entry->channel;
        *program_out = entry->value1;
        if (primask == 0U)
            __enable_irq();
        return 1U;
    }

    if (primask == 0U)
        __enable_irq();

    return 0U;
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

    case MIDI_MONITOR_SOURCE_UART5:
        return &midi_monitor_uart5_parser;

    case MIDI_MONITOR_SOURCE_USART6:
        return &midi_monitor_usart6_parser;

    case MIDI_MONITOR_SOURCE_UART9:
        return &midi_monitor_uart9_parser;

    default:
        return NULL;
    }
}

static void MidiMonitor_PushEntry(uint8_t source_uart,
                                  uint8_t type,
                                  uint8_t channel,
                                  uint8_t value1,
                                  uint8_t value2,
                                  uint32_t timestamp_us)
{
    uint8_t entry_index = midi_monitor_head;
    uint16_t source_mask = (source_uart < MIDI_MONITOR_SOURCE_INDEX_COUNT)
        ? (uint16_t)(1U << source_uart)
        : 0U;
    uint32_t entry_revision = midi_monitor_revision + 1U;

    midi_monitor_entries[entry_index].source_uart = source_uart;
    midi_monitor_entries[entry_index].type = type;
    midi_monitor_entries[entry_index].channel = channel;
    midi_monitor_entries[entry_index].value1 = value1;
    midi_monitor_entries[entry_index].value2 = value2;
    midi_monitor_entries[entry_index].timestamp_us = timestamp_us;
    midi_monitor_entries[entry_index].delta_us =
        (source_mask != 0U && (midi_monitor_timestamp_valid_mask & source_mask) != 0U)
        ? timestamp_us - midi_monitor_last_timestamp_us[source_uart]
        : MIDI_MONITOR_DELTA_UNAVAILABLE;
    midi_monitor_entries[entry_index].revision = entry_revision;

    if (source_mask != 0U)
    {
        midi_monitor_last_timestamp_us[source_uart] = timestamp_us;
        midi_monitor_timestamp_valid_mask |= source_mask;
    }

    midi_monitor_head = (uint8_t)((midi_monitor_head + 1U) % MIDI_MONITOR_ENTRY_CAPACITY);
    if (midi_monitor_count < MIDI_MONITOR_ENTRY_CAPACITY)
        midi_monitor_count++;

    midi_monitor_revision = entry_revision;
    MidiMonitor_QueueChangedEvent();
}

static void MidiMonitor_QueueChangedEvent(void)
{
    AppEvent_t event;

    if (midi_monitor_changed_event_pending)
        return;

    event.type = APP_EVENT_TYPE_MIDI_MONITOR_CHANGED;
    event.source = APP_EVENT_SOURCE_NONE;
    event.value = 0;
    event.tick = HAL_GetTick();
    if (AppEvent_Push(&event))
        midi_monitor_changed_event_pending = 1U;
}
