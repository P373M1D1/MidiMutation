#include "midi_functions.h"
#include "midi/midi_monitor.h"

void Error_Handler(void);

#define MIDI_UART_IRQ_PREEMPT_PRIORITY 2U
#define MIDI_UART_IRQ_SUBPRIORITY     1U
#define MIDI_THRU_BUFFER_SIZE         64U

static UART_HandleTypeDef midi_input_uart;
static uint8_t midi_thru_buffer[MIDI_THRU_BUFFER_SIZE];
static uint8_t midi_thru_head = 0U;
static uint8_t midi_thru_tail = 0U;

static void MidiInput_ApplyStandardConfig(UART_HandleTypeDef *uart_handle,
                                          USART_TypeDef *instance,
                                          uint32_t mode);
static void MidiInput_QueueThruByte(uint8_t byte);
static void MidiInput_ServiceThruTx(void);

void MidiInitInput(void)
{
    MidiInput_ApplyStandardConfig(&midi_input_uart, USART2, UART_MODE_TX_RX);
    if (HAL_UART_Init(&midi_input_uart) != HAL_OK)
    {
        Error_Handler();
    }

    midi_thru_head = 0U;
    midi_thru_tail = 0U;
    MidiMonitor_Init();
    HAL_NVIC_SetPriority(USART2_IRQn, MIDI_UART_IRQ_PREEMPT_PRIORITY, MIDI_UART_IRQ_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    __HAL_UART_ENABLE_IT(&midi_input_uart, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&midi_input_uart, UART_IT_ERR);
    __HAL_UART_DISABLE_IT(&midi_input_uart, UART_IT_TXE);
    MidiClockUseInternalTempo();
}

void USART2_IRQHandler(void)
{
    uint32_t status = USART2->SR;

    /* Read RX data first to clear UART error conditions and keep the receive
     * side draining promptly; soft-thru and sync decoding both hang off that
     * same byte stream. */
    if (status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE))
    {
        uint8_t byte = (uint8_t)USART2->DR;

        if (status & USART_SR_RXNE)
        {
            MidiMonitor_ReceiveByte(MIDI_MONITOR_SOURCE_UART2, byte);
            MidiInput_QueueThruByte(byte);
            MidiReceive(byte);
        }

        status = USART2->SR;
    }

    if ((status & USART_SR_TXE) && ((USART2->CR1 & USART_CR1_TXEIE) != 0U))
        MidiInput_ServiceThruTx();
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

static void MidiInput_QueueThruByte(uint8_t byte)
{
    uint8_t next_head = (uint8_t)((midi_thru_head + 1U) % MIDI_THRU_BUFFER_SIZE);

    if (next_head == midi_thru_tail)
    {
        return;
    }

    midi_thru_buffer[midi_thru_head] = byte;
    midi_thru_head = next_head;
    __HAL_UART_ENABLE_IT(&midi_input_uart, UART_IT_TXE);
}

static void MidiInput_ServiceThruTx(void)
{
    if (midi_thru_tail == midi_thru_head)
    {
        __HAL_UART_DISABLE_IT(&midi_input_uart, UART_IT_TXE);
        return;
    }

    midi_input_uart.Instance->DR = midi_thru_buffer[midi_thru_tail];
    midi_thru_tail = (uint8_t)((midi_thru_tail + 1U) % MIDI_THRU_BUFFER_SIZE);

    if (midi_thru_tail == midi_thru_head)
        __HAL_UART_DISABLE_IT(&midi_input_uart, UART_IT_TXE);
}