#include "midi/midi_output.h"
#include "midi/midi_monitor.h"

#define MIDI_OUTPUT_CLOCK_QUEUE_SIZE 16U
#define MIDI_OUTPUT_MESSAGE_QUEUE_SIZE 128U
#define MIDI_OUTPUT_TX_TIMEOUT_MS 10U
#define MIDI_OUTPUT_BYTE_TIME_US 320U
#define MIDI_OUTPUT_POST_CLOCK_GUARD_US 80U
#define MIDI_OUTPUT_PRE_CLOCK_GUARD_US 80U

static UART_HandleTypeDef *midi_output_uart = NULL;
static uint8_t midi_output_clock_buffer[MIDI_OUTPUT_CLOCK_QUEUE_SIZE];
static volatile uint8_t midi_output_clock_head = 0U;
static volatile uint8_t midi_output_clock_tail = 0U;
static uint8_t midi_output_message_buffer[MIDI_OUTPUT_MESSAGE_QUEUE_SIZE];
static volatile uint8_t midi_output_message_head = 0U;
static volatile uint8_t midi_output_message_tail = 0U;
static volatile uint32_t midi_output_last_clock_us = 0U;
static volatile uint32_t midi_output_clock_interval_us = 0U;

__attribute__((always_inline))
static inline uint32_t MidiOutput_EnterCritical(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    return primask;
}

__attribute__((always_inline))
static inline void MidiOutput_ExitCritical(uint32_t primask)
{
    if (primask == 0U)
        __enable_irq();
}

__attribute__((section(".RamFunc")))
static uint8_t MidiOutput_IsFlashBusy(void);
__attribute__((section(".RamFunc")))
static void MidiOutput_KickTx(void);
__attribute__((section(".RamFunc")))
static uint8_t MidiOutput_MessageCanStartNow(void);
static uint8_t MidiOutput_RingFreeSpace(uint8_t head, uint8_t tail, uint8_t size);
__attribute__((section(".RamFunc")))
static uint32_t MidiOutput_TimerDiff(uint32_t now, uint32_t last);

void MidiOutput_SetUart(UART_HandleTypeDef *uart_handle)
{
    midi_output_uart = uart_handle;
    midi_output_clock_head = 0U;
    midi_output_clock_tail = 0U;
    midi_output_message_head = 0U;
    midi_output_message_tail = 0U;
    midi_output_last_clock_us = 0U;
    midi_output_clock_interval_us = 0U;

    if (midi_output_uart && midi_output_uart->Instance != NULL)
        __HAL_UART_DISABLE_IT(midi_output_uart, UART_IT_TXE);
}

void MidiOutput_ServiceScheduler(void)
{
    uint32_t primask;
    uint8_t clock_pending;
    uint8_t message_pending;
    uint8_t txe_enabled;

    if (!midi_output_uart || midi_output_uart->Instance == NULL)
        return;

    if (!MidiOutput_MessageCanStartNow())
        return;

    primask = MidiOutput_EnterCritical();
    clock_pending = (uint8_t)(midi_output_clock_tail != midi_output_clock_head);
    message_pending = (uint8_t)(midi_output_message_tail != midi_output_message_head);
    txe_enabled = (uint8_t)((midi_output_uart->Instance->CR1 & USART_CR1_TXEIE) != 0U);
    if (!clock_pending && message_pending && !txe_enabled)
    {
        MidiOutput_KickTx();
    }
    MidiOutput_ExitCritical(primask);
}

__attribute__((section(".RamFunc")))
void MidiOutput_HandleTxIrq(void)
{
    if (!midi_output_uart || midi_output_uart->Instance == NULL)
        return;

    if (midi_output_clock_tail != midi_output_clock_head)
    {
        midi_output_uart->Instance->DR = midi_output_clock_buffer[midi_output_clock_tail];
        midi_output_clock_tail = (uint8_t)((midi_output_clock_tail + 1U) % MIDI_OUTPUT_CLOCK_QUEUE_SIZE);
        return;
    }

    if (midi_output_message_tail != midi_output_message_head)
    {
        if (!MidiOutput_MessageCanStartNow())
        {
            __HAL_UART_DISABLE_IT(midi_output_uart, UART_IT_TXE);
            return;
        }

        midi_output_uart->Instance->DR = midi_output_message_buffer[midi_output_message_tail];
        midi_output_message_tail = (uint8_t)((midi_output_message_tail + 1U) % MIDI_OUTPUT_MESSAGE_QUEUE_SIZE);
        return;
    }

    __HAL_UART_DISABLE_IT(midi_output_uart, UART_IT_TXE);
}

__attribute__((section(".RamFunc")))
void UART4_IRQHandler(void)
{
    uint32_t status = UART4->SR;

    if ((status & USART_SR_TXE) && ((UART4->CR1 & USART_CR1_TXEIE) != 0U))
        MidiOutput_HandleTxIrq();

    if (status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE))
    {
        uint8_t byte = (uint8_t)UART4->DR;

        if ((status & USART_SR_RXNE) && !MidiOutput_IsFlashBusy())
            MidiMonitor_ReceiveByte(MIDI_MONITOR_SOURCE_UART4, byte);

        status = UART4->SR;
    }
}

uint8_t MidiOutput_QueueMessageBytes(const uint8_t *bytes, uint16_t length)
{
    uint32_t start_tick;

    if (!bytes || length == 0U || length >= MIDI_OUTPUT_MESSAGE_QUEUE_SIZE
        || !midi_output_uart || midi_output_uart->Instance == NULL)
    {
        return 0U;
    }

    if (__get_IPSR() != 0U)
        return 0U;

    start_tick = HAL_GetTick();
    do
    {
        uint32_t primask = __get_PRIMASK();
        uint8_t free_space;

        primask = MidiOutput_EnterCritical();
        free_space = MidiOutput_RingFreeSpace(midi_output_message_head,
                                              midi_output_message_tail,
                                              MIDI_OUTPUT_MESSAGE_QUEUE_SIZE);
        if (free_space >= length)
        {
            for (uint16_t index = 0U; index < length; index++)
            {
                midi_output_message_buffer[midi_output_message_head] = bytes[index];
                midi_output_message_head = (uint8_t)((midi_output_message_head + 1U) % MIDI_OUTPUT_MESSAGE_QUEUE_SIZE);
            }

            MidiOutput_KickTx();
            MidiOutput_ExitCritical(primask);
            return 1U;
        }

        MidiOutput_ExitCritical(primask);
    }

    while ((HAL_GetTick() - start_tick) < MIDI_OUTPUT_TX_TIMEOUT_MS);

    return 0U;
}

__attribute__((section(".RamFunc")))
uint8_t MidiOutput_QueueRealtimeByte(uint8_t byte)
{
    uint32_t primask = MidiOutput_EnterCritical();
    uint32_t now = TIM2->CNT;
    uint8_t next_head;

    if (midi_output_last_clock_us != 0U && now != midi_output_last_clock_us)
    {
        midi_output_clock_interval_us = MidiOutput_TimerDiff(now, midi_output_last_clock_us);
    }
    midi_output_last_clock_us = now;

    next_head = (uint8_t)((midi_output_clock_head + 1U) % MIDI_OUTPUT_CLOCK_QUEUE_SIZE);
    if (next_head == midi_output_clock_tail)
    {
        MidiOutput_ExitCritical(primask);
        return 0U;
    }

    midi_output_clock_buffer[midi_output_clock_head] = byte;
    midi_output_clock_head = next_head;
    MidiOutput_KickTx();
    MidiOutput_ExitCritical(primask);
    return 1U;
}

void MidiOutput_ResetRealtimePacingGuard(void)
{
    uint32_t primask = MidiOutput_EnterCritical();

    midi_output_last_clock_us = 0U;
    midi_output_clock_interval_us = 0U;

    MidiOutput_ExitCritical(primask);
}

__attribute__((section(".RamFunc")))
static uint8_t MidiOutput_IsFlashBusy(void)
{
    return ((FLASH->SR & FLASH_SR_BSY) != 0U) ? 1U : 0U;
}

__attribute__((section(".RamFunc")))
static void MidiOutput_KickTx(void)
{
    if (midi_output_uart && midi_output_uart->Instance != NULL)
        __HAL_UART_ENABLE_IT(midi_output_uart, UART_IT_TXE);
}

__attribute__((section(".RamFunc")))
static uint8_t MidiOutput_MessageCanStartNow(void)
{
    uint32_t primask = MidiOutput_EnterCritical();
    uint32_t interval_us = midi_output_clock_interval_us;
    uint32_t last_clock_us = midi_output_last_clock_us;
    uint32_t elapsed_us;
    uint32_t time_until_next_clock;

    interval_us = midi_output_clock_interval_us;
    last_clock_us = midi_output_last_clock_us;
    MidiOutput_ExitCritical(primask);

    if (interval_us == 0U || last_clock_us == 0U)
        return 1U;

    elapsed_us = MidiOutput_TimerDiff(TIM2->CNT, last_clock_us);
    if (elapsed_us < MIDI_OUTPUT_POST_CLOCK_GUARD_US)
        return 0U;

    if (elapsed_us >= interval_us)
        return 0U;

    time_until_next_clock = interval_us - elapsed_us;
    return (uint8_t)(time_until_next_clock > (MIDI_OUTPUT_BYTE_TIME_US + MIDI_OUTPUT_PRE_CLOCK_GUARD_US));
}

static uint8_t MidiOutput_RingFreeSpace(uint8_t head, uint8_t tail, uint8_t size)
{
    return (tail > head)
        ? (uint8_t)(tail - head - 1U)
        : (uint8_t)(size - head + tail - 1U);
}

__attribute__((section(".RamFunc")))
static uint32_t MidiOutput_TimerDiff(uint32_t now, uint32_t last)
{
    return now - last;
}