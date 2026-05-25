/* 
This file implements MIDI input handling, including UART configuration, interrupt service routine for receiving MIDI bytes, 
and a queue for handling realtime MIDI events with low latency. It also includes diagnostics for monitoring the realtime event queue.
Also, it implements a MIDI thru buffer to allow received MIDI bytes to be retransmitted with minimal latency, which is important 
for live performance use cases where the device is in the MIDI signal path. The MIDI input UART is configured for the standard MIDI 
baud rate of 31250 bps, and the IRQ handler captures incoming bytes along with a timestamp from TIM2 to allow for precise timing of 
realtime events. The module also integrates with the transport handling code to immediately process realtime MIDI bytes for clock synchronization purposes.
*/

#include "midi_functions.h"
#include "midi/midi_monitor.h"
#include "midi/midi_transport_internal.h"

void Error_Handler(void);

#define MIDI_UART_IRQ_PREEMPT_PRIORITY 2U
#define MIDI_UART_IRQ_SUBPRIORITY     1U
#define MIDI_THRU_BUFFER_SIZE         64U
#define MIDI_REALTIME_QUEUE_SIZE      64U
#define MIDI_REALTIME_STATUS_FIRST    0xF8U

typedef struct {
    uint8_t byte;
    uint32_t timestamp_us;
} MidiRealtimeRxEvent_t;

__attribute__((always_inline))
static inline uint16_t MidiInput_RealtimeQueueDepth(uint8_t head, uint8_t tail)
{
    return (head >= tail)
        ? (uint16_t)(head - tail)
        : (uint16_t)(MIDI_REALTIME_QUEUE_SIZE - tail + head);
}

static UART_HandleTypeDef midi_input_uart;
static uint8_t midi_thru_buffer[MIDI_THRU_BUFFER_SIZE];
static uint8_t midi_thru_head = 0U;
static uint8_t midi_thru_tail = 0U;
static MidiRealtimeRxEvent_t midi_realtime_queue[MIDI_REALTIME_QUEUE_SIZE];
static volatile uint8_t midi_realtime_head = 0U;
static volatile uint8_t midi_realtime_tail = 0U;
static volatile uint16_t midi_realtime_interval_peak_depth = 0U;
static volatile uint16_t midi_realtime_lifetime_peak_depth = 0U;
static volatile uint32_t midi_realtime_total_enqueued_count = 0U;
static volatile uint32_t midi_realtime_total_dropped_count = 0U;
static volatile uint32_t midi_realtime_interval_dropped_count = 0U;
static volatile uint32_t midi_realtime_interval_latency_sum_us = 0U;
static volatile uint32_t midi_realtime_interval_latency_max_us = 0U;
static volatile uint16_t midi_realtime_interval_latency_sample_count = 0U;

__attribute__((section(".RamFunc")))
static uint8_t MidiInput_IsFlashBusy(void);

static void MidiInput_ApplyStandardConfig(UART_HandleTypeDef *uart_handle,
                                          USART_TypeDef *instance,
                                          uint32_t mode);
__attribute__((section(".RamFunc")))
static void MidiInput_QueueThruByte(uint8_t byte);
__attribute__((section(".RamFunc")))
static void MidiInput_ServiceThruTx(void);
__attribute__((section(".RamFunc")))
static void MidiInput_QueueRealtimeByte(uint8_t byte, uint32_t timestamp_us);

void MidiInitInput(void)
{
    MidiInput_ApplyStandardConfig(&midi_input_uart, USART2, UART_MODE_TX_RX);
    if (HAL_UART_Init(&midi_input_uart) != HAL_OK)
    {
        Error_Handler();
    }

    midi_thru_head = 0U;
    midi_thru_tail = 0U;
    midi_realtime_head = 0U;
    midi_realtime_tail = 0U;
    midi_realtime_interval_peak_depth = 0U;
    midi_realtime_lifetime_peak_depth = 0U;
    midi_realtime_total_enqueued_count = 0U;
    midi_realtime_total_dropped_count = 0U;
    midi_realtime_interval_dropped_count = 0U;
    midi_realtime_interval_latency_sum_us = 0U;
    midi_realtime_interval_latency_max_us = 0U;
    midi_realtime_interval_latency_sample_count = 0U;
    MidiMonitor_Init();
    HAL_NVIC_SetPriority(USART2_IRQn, MIDI_UART_IRQ_PREEMPT_PRIORITY, MIDI_UART_IRQ_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    __HAL_UART_ENABLE_IT(&midi_input_uart, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&midi_input_uart, UART_IT_ERR);
    __HAL_UART_DISABLE_IT(&midi_input_uart, UART_IT_TXE);
    MidiClockUseInternalTempo();
}

__attribute__((section(".RamFunc")))
void USART2_IRQHandler(void)
{
    uint32_t status = USART2->SR;

    /* Read RX data first to clear UART error conditions and keep the receive
     * side draining promptly. Transport realtime bytes are handled immediately
     * from the captured TIM2 timestamp so quarter-note scheduling does not wait
     * on the foreground loop under UI load. */
    if (status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE))
    {
        uint8_t byte = (uint8_t)USART2->DR;
        uint8_t flash_busy = MidiInput_IsFlashBusy();
        uint32_t now_us = TIM2->CNT;

        if (status & USART_SR_RXNE)
        {
            MidiInput_QueueThruByte(byte);

            if (byte >= MIDI_REALTIME_STATUS_FIRST)
            {
                if (byte == MIDI_REALTIME_STATUS_FIRST)
                    midi_clock_last_captured_pulse_us = now_us;

                if (!MidiTransport_HandleRealtimeByteFast(byte, now_us))
                    MidiInput_QueueRealtimeByte(byte, now_us);

                if (!flash_busy)
                    MidiMonitor_ReceiveByte(MIDI_MONITOR_SOURCE_UART2, byte);
            }
            else if (!flash_busy)
            {
                MidiMonitor_ReceiveByte(MIDI_MONITOR_SOURCE_UART2, byte);
                MidiReceive(byte);
            }
        }

        status = USART2->SR;
    }

    if ((status & USART_SR_TXE) && ((USART2->CR1 & USART_CR1_TXEIE) != 0U))
        MidiInput_ServiceThruTx();
}

void MidiInput_ServiceRealtimeRx(void)
{
    while (midi_realtime_tail != midi_realtime_head)
    {
        MidiRealtimeRxEvent_t event = midi_realtime_queue[midi_realtime_tail];
        uint32_t latency_us;

        midi_realtime_tail = (uint8_t)((midi_realtime_tail + 1U) % MIDI_REALTIME_QUEUE_SIZE);
        latency_us = TIM2->CNT - event.timestamp_us;
        midi_realtime_interval_latency_sum_us += latency_us;
        if (latency_us > midi_realtime_interval_latency_max_us)
            midi_realtime_interval_latency_max_us = latency_us;
        if (midi_realtime_interval_latency_sample_count < UINT16_MAX)
            midi_realtime_interval_latency_sample_count++;
        (void)MidiTransport_HandleRealtimeByteFast(event.byte, event.timestamp_us);
    }
}

void MidiInput_TakeRealtimeRxDiagnostics(MidiInputRealtimeRxDiagnostics_t *diagnostics)
{
    uint32_t primask;
    uint16_t current_depth;
    uint32_t latency_sum_us;
    uint16_t latency_sample_count;

    if (!diagnostics)
        return;

    primask = __get_PRIMASK();
    __disable_irq();

    current_depth = MidiInput_RealtimeQueueDepth(midi_realtime_head, midi_realtime_tail);
    diagnostics->current_depth = current_depth;
    diagnostics->interval_peak_depth = midi_realtime_interval_peak_depth;
    diagnostics->lifetime_peak_depth = midi_realtime_lifetime_peak_depth;
    diagnostics->total_enqueued_count = midi_realtime_total_enqueued_count;
    diagnostics->total_dropped_count = midi_realtime_total_dropped_count;
    diagnostics->interval_dropped_count = midi_realtime_interval_dropped_count;
    diagnostics->interval_latency_max_us = midi_realtime_interval_latency_max_us;
    latency_sum_us = midi_realtime_interval_latency_sum_us;
    latency_sample_count = midi_realtime_interval_latency_sample_count;
    diagnostics->interval_latency_sample_count = latency_sample_count;

    midi_realtime_interval_peak_depth = current_depth;
    midi_realtime_interval_dropped_count = 0U;
    midi_realtime_interval_latency_sum_us = 0U;
    midi_realtime_interval_latency_max_us = 0U;
    midi_realtime_interval_latency_sample_count = 0U;

    if (primask == 0U)
        __enable_irq();

    diagnostics->interval_latency_average_us = (latency_sample_count != 0U)
        ? (latency_sum_us / (uint32_t)latency_sample_count)
        : 0U;
}

static void MidiInput_ApplyStandardConfig(UART_HandleTypeDef *uart_handle,
                                          USART_TypeDef *instance,
                                          uint32_t mode)
{
    uart_handle->Instance = instance;
    uart_handle->Init.BaudRate = MIDI_BAUD_RATE;
    uart_handle->Init.WordLength = UART_WORDLENGTH_8B;
    uart_handle->Init.StopBits = UART_STOPBITS_1;
    uart_handle->Init.Parity = UART_PARITY_NONE;
    uart_handle->Init.Mode = mode;
    uart_handle->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uart_handle->Init.OverSampling = UART_OVERSAMPLING_16;
}

__attribute__((section(".RamFunc")))
static uint8_t MidiInput_IsFlashBusy(void)
{
    return ((FLASH->SR & FLASH_SR_BSY) != 0U) ? 1U : 0U;
}

__attribute__((section(".RamFunc")))
static void MidiInput_QueueRealtimeByte(uint8_t byte, uint32_t timestamp_us)
{
    uint8_t next_head = (uint8_t)((midi_realtime_head + 1U) % MIDI_REALTIME_QUEUE_SIZE);
    uint16_t depth_after_enqueue;

    if (next_head == midi_realtime_tail)
    {
        midi_realtime_total_dropped_count++;
        midi_realtime_interval_dropped_count++;
        return;
    }

    midi_realtime_queue[midi_realtime_head].byte = byte;
    midi_realtime_queue[midi_realtime_head].timestamp_us = timestamp_us;
    midi_realtime_head = next_head;
    midi_realtime_total_enqueued_count++;

    depth_after_enqueue = MidiInput_RealtimeQueueDepth(next_head, midi_realtime_tail);
    if (depth_after_enqueue > midi_realtime_interval_peak_depth)
        midi_realtime_interval_peak_depth = depth_after_enqueue;
    if (depth_after_enqueue > midi_realtime_lifetime_peak_depth)
        midi_realtime_lifetime_peak_depth = depth_after_enqueue;
}

__attribute__((section(".RamFunc")))
static void MidiInput_QueueThruByte(uint8_t byte)
{
    uint8_t next_head = (uint8_t)((midi_thru_head + 1U) % MIDI_THRU_BUFFER_SIZE);

    if (next_head == midi_thru_tail)
    {
        return;
    }

    midi_thru_buffer[midi_thru_head] = byte;
    midi_thru_head = next_head;
    midi_input_uart.Instance->CR1 |= USART_CR1_TXEIE;
}

__attribute__((section(".RamFunc")))
static void MidiInput_ServiceThruTx(void)
{
    if (midi_thru_tail == midi_thru_head)
    {
        midi_input_uart.Instance->CR1 &= ~USART_CR1_TXEIE;
        return;
    }

    midi_input_uart.Instance->DR = midi_thru_buffer[midi_thru_tail];
    midi_thru_tail = (uint8_t)((midi_thru_tail + 1U) % MIDI_THRU_BUFFER_SIZE);

    if (midi_thru_tail == midi_thru_head)
        midi_input_uart.Instance->CR1 &= ~USART_CR1_TXEIE;
}