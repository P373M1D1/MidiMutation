#include "midi/midi_output.h"
#include "midi/midi_monitor.h"

#define MIDI_OUTPUT_CLOCK_QUEUE_SIZE 16U
#define MIDI_OUTPUT_MESSAGE_QUEUE_SIZE 128U
#define MIDI_OUTPUT_TX_TIMEOUT_MS 10U
#define MIDI_OUTPUT_BYTE_TIME_US 320U
#define MIDI_OUTPUT_POST_CLOCK_GUARD_US 80U
#define MIDI_OUTPUT_PRE_CLOCK_GUARD_US 80U
#define MIDI_REALTIME_CLOCK 0xF8U

#define MIDI_TIMEBEND_DECAY_US 2000000UL
#define MIDI_TIMEBEND_SPRING_US 1200000UL
#define MIDI_TIMEBEND_MIN_OFFSET_US 1000L
#define MIDI_TIMEBEND_OFFSET_NUM 9L
#define MIDI_TIMEBEND_OFFSET_DEN 20L
/* Strict +/-50% BPM bend in tempo space.
 * Because interval is inverse of BPM:
 *   +50% BPM -> 66.7% interval
 *   -50% BPM -> 200.0% interval */
#define MIDI_TIMEBEND_MIN_INTERVAL_PCT 67UL
#define MIDI_TIMEBEND_MAX_INTERVAL_PCT 200UL
#define MIDI_TIMEBEND_POSITION_STEPS_FULL_SPAN 192UL
#define MIDI_TIMEBEND_ACTIVE_HOLD_US 120000UL
#define MIDI_TIMEBEND_RELEASE_ACCEL_START_US 250000UL
#define MIDI_TIMEBEND_RELEASE_DECAY_US 450000UL
#define MIDI_TIMEBEND_RELEASE_SPRING_US 500000UL
#define MIDI_TIMEBEND_RELEASE_FORCE_ZERO_US 2800000UL
#define MIDI_TIMEBEND_REST_PHASE_DEADBAND_US 24L
#define MIDI_TIMEBEND_REST_VELOCITY_DEADBAND_US_PER_S 180L
#define MIDI_TIMEBEND_POPUP_PHASE_THRESHOLD_US 40L
#define MIDI_TIMEBEND_POPUP_VELOCITY_THRESHOLD_US_PER_S 260L
#define MIDI_TIMEBEND_MAX_VELOCITY_US_PER_S 240000L
#define MIDI_TIMEBEND_PHASE_STEP_Q24 (1ULL << 24)
#define MIDI_TIMEBEND_TRUTH_TIMEOUT_MULTIPLIER 3U
#define MIDI_TIMEBEND_MAX_CROSSINGS_PER_PASS 64U

static UART_HandleTypeDef *midi_output_uart = NULL;
static uint8_t midi_output_clock_buffer[MIDI_OUTPUT_CLOCK_QUEUE_SIZE];
static volatile uint8_t midi_output_clock_head = 0U;
static volatile uint8_t midi_output_clock_tail = 0U;
static uint8_t midi_output_message_buffer[MIDI_OUTPUT_MESSAGE_QUEUE_SIZE];
static volatile uint8_t midi_output_message_head = 0U;
static volatile uint8_t midi_output_message_tail = 0U;
static volatile uint32_t midi_output_last_clock_us = 0U;
static volatile uint32_t midi_output_clock_interval_us = 0U;
static volatile uint8_t midi_output_timebend_active = 0U;
static volatile int32_t midi_output_timebend_phase_offset_us_q16 = 0;
static volatile int32_t midi_output_timebend_velocity_us_per_s_q16 = 0;
static volatile uint32_t midi_output_timebend_last_update_us = 0U;
static volatile uint32_t midi_output_timebend_last_encoder_us = 0U;
static volatile uint32_t midi_output_timebend_last_truth_pulse_us = 0U;
static volatile uint32_t midi_output_timebend_truth_interval_us = 0U;
static volatile uint64_t midi_output_timebend_phase_out_q24 = 0ULL;
static volatile uint64_t midi_output_timebend_next_edge_q24 = MIDI_TIMEBEND_PHASE_STEP_Q24;
static volatile uint32_t midi_output_timebend_last_phase_sample_us = 0U;
static volatile uint32_t midi_output_timebend_next_due_us = 0U;
static volatile uint8_t midi_output_timebend_due_pending = 0U;
static volatile uint8_t midi_output_timebend_due_peak_depth = 0U;
static volatile uint32_t midi_output_timebend_diag_enqueued_count = 0U;
static volatile uint32_t midi_output_timebend_diag_emitted_count = 0U;
static volatile uint32_t midi_output_timebend_diag_dropped_count = 0U;
static volatile uint32_t midi_output_timebend_diag_clamp_min_count = 0U;
static volatile uint32_t midi_output_timebend_diag_clamp_max_count = 0U;
static volatile uint32_t midi_output_timebend_diag_late_sum_us = 0U;
static volatile uint32_t midi_output_timebend_diag_late_max_us = 0U;
static volatile uint32_t midi_output_timebend_diag_late_sample_count = 0U;
static volatile uint32_t midi_output_timebend_diag_emit_interval_sum_us = 0U;
static volatile uint32_t midi_output_timebend_diag_emit_interval_min_us = UINT32_MAX;
static volatile uint32_t midi_output_timebend_diag_emit_interval_max_us = 0U;
static volatile uint32_t midi_output_timebend_diag_emit_interval_sample_count = 0U;
static volatile uint32_t midi_output_timebend_diag_missed_emit_count = 0U;
static volatile uint32_t midi_output_timebend_diag_crossing_backlog_peak = 0U;
static volatile uint32_t midi_output_timebend_diag_phase_nonmono_count = 0U;
static volatile uint8_t midi_output_clock_diag_peak_depth = 0U;

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
static uint8_t MidiOutput_ClockDepthLocked(void);
__attribute__((section(".RamFunc")))
static uint32_t MidiOutput_TimerDiff(uint32_t now, uint32_t last);
__attribute__((section(".RamFunc")))
static uint8_t MidiOutput_TimeReached(uint32_t now, uint32_t due);
__attribute__((section(".RamFunc")))
static void MidiOutput_TimebendResetLocked(void);
__attribute__((section(".RamFunc")))
static void MidiOutput_TimebendUpdateModelLocked(uint32_t now_us);
__attribute__((section(".RamFunc")))
static uint32_t MidiOutput_TimebendComputeTargetIntervalUsLocked(void);
__attribute__((section(".RamFunc")))
static void MidiOutput_TimebendArmCompareLocked(void);
__attribute__((section(".RamFunc")))
static void MidiOutput_TimebendAdvancePhaseLocked(uint32_t now_us);
__attribute__((section(".RamFunc")))
static void MidiOutput_TimebendEmitCrossingsLocked(uint32_t now_us);
__attribute__((section(".RamFunc")))
static uint32_t MidiOutput_TimebendCrossingBacklogLocked(void);
__attribute__((section(".RamFunc")))
static uint8_t MidiOutput_TimebendDueDepthLocked(void);

void MidiOutput_SetUart(UART_HandleTypeDef *uart_handle)
{
    uint32_t primask;

    midi_output_uart = uart_handle;
    midi_output_clock_head = 0U;
    midi_output_clock_tail = 0U;
    midi_output_message_head = 0U;
    midi_output_message_tail = 0U;
    midi_output_last_clock_us = 0U;
    midi_output_clock_interval_us = 0U;
    midi_output_clock_diag_peak_depth = 0U;

    primask = MidiOutput_EnterCritical();
    MidiOutput_TimebendResetLocked();
    MidiOutput_ExitCritical(primask);

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

    if ((byte == MIDI_REALTIME_CLOCK) && midi_output_timebend_active)
    {
        uint32_t truth_interval_us;

        if (midi_output_timebend_last_truth_pulse_us != 0U)
        {
            truth_interval_us = MidiOutput_TimerDiff(now, midi_output_timebend_last_truth_pulse_us);

            if (truth_interval_us > 0U)
                midi_output_timebend_truth_interval_us = truth_interval_us;
        }
        midi_output_timebend_last_truth_pulse_us = now;

        if (midi_output_timebend_last_phase_sample_us == 0U)
            midi_output_timebend_last_phase_sample_us = now;

        MidiOutput_TimebendUpdateModelLocked(now);
        MidiOutput_TimebendAdvancePhaseLocked(now);
        MidiOutput_TimebendEmitCrossingsLocked(now);
        MidiOutput_TimebendArmCompareLocked();
        MidiOutput_ExitCritical(primask);
        return 1U;
    }

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
    {
        uint8_t clock_depth = MidiOutput_ClockDepthLocked();

        if (clock_depth > midi_output_clock_diag_peak_depth)
            midi_output_clock_diag_peak_depth = clock_depth;
    }
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

void MidiOutput_TimebendSetActive(uint8_t active)
{
    uint32_t primask = MidiOutput_EnterCritical();
    uint8_t next_active = active ? 1U : 0U;
    uint32_t now;

    if (next_active == midi_output_timebend_active)
    {
        MidiOutput_ExitCritical(primask);
        return;
    }

    midi_output_timebend_active = next_active;
    if (!midi_output_timebend_active)
    {
        MidiOutput_TimebendResetLocked();
    }
    else
    {
        now = TIM2->CNT;
        midi_output_timebend_last_update_us = now;
        midi_output_timebend_last_phase_sample_us = (midi_output_last_clock_us != 0U)
            ? midi_output_last_clock_us
            : now;
        midi_output_timebend_last_truth_pulse_us = midi_output_last_clock_us;
        if ((midi_output_timebend_truth_interval_us == 0U) && (midi_output_clock_interval_us != 0U))
            midi_output_timebend_truth_interval_us = midi_output_clock_interval_us;
        midi_output_timebend_phase_out_q24 = 0ULL;
        midi_output_timebend_next_edge_q24 = MIDI_TIMEBEND_PHASE_STEP_Q24;
        midi_output_timebend_next_due_us = 0U;
        midi_output_timebend_due_pending = 0U;
    }

    MidiOutput_ExitCritical(primask);
}

void MidiOutput_TimebendInjectEncoderDelta(int8_t delta)
{
    uint32_t primask;
    uint32_t now;
    uint32_t truth_interval_us;
    uint32_t raw_max_offset_us;
    uint32_t step_us;
    int32_t signed_step_us;
    int64_t max_offset_q16;
    int64_t phase_q16;

    if (delta == 0 || !midi_output_timebend_active)
        return;

    primask = MidiOutput_EnterCritical();
    now = TIM2->CNT;
    MidiOutput_TimebendUpdateModelLocked(now);

    truth_interval_us = midi_output_timebend_truth_interval_us;
    if (truth_interval_us == 0U)
        truth_interval_us = (midi_output_clock_interval_us != 0U) ? midi_output_clock_interval_us : 20833U;

    raw_max_offset_us = (uint32_t)(((uint64_t)truth_interval_us * (uint64_t)MIDI_TIMEBEND_OFFSET_NUM)
        / (uint64_t)MIDI_TIMEBEND_OFFSET_DEN);
    if (raw_max_offset_us < (uint32_t)MIDI_TIMEBEND_MIN_OFFSET_US)
        raw_max_offset_us = (uint32_t)MIDI_TIMEBEND_MIN_OFFSET_US;

    step_us = raw_max_offset_us / MIDI_TIMEBEND_POSITION_STEPS_FULL_SPAN;
    if (step_us == 0U)
        step_us = 1U;

    /* Position model: each detent advances a virtual time knob position.
     * CW  -> faster (negative phase), CCW -> slower (positive phase). */
    signed_step_us = (delta > 0) ? -(int32_t)step_us : (int32_t)step_us;
    phase_q16 = (int64_t)midi_output_timebend_phase_offset_us_q16
        + ((int64_t)signed_step_us * (int64_t)((delta < 0) ? -delta : delta) << 16);

    max_offset_q16 = (int64_t)((uint64_t)raw_max_offset_us << 16);
    if (phase_q16 > max_offset_q16)
        phase_q16 = max_offset_q16;
    else if (phase_q16 < -max_offset_q16)
        phase_q16 = -max_offset_q16;

    midi_output_timebend_phase_offset_us_q16 = (int32_t)phase_q16;
    midi_output_timebend_velocity_us_per_s_q16 = 0;
    midi_output_timebend_last_encoder_us = now;

    MidiOutput_TimebendAdvancePhaseLocked(now);
    MidiOutput_TimebendEmitCrossingsLocked(now);
    MidiOutput_TimebendArmCompareLocked();

    MidiOutput_ExitCritical(primask);
}

uint8_t MidiOutput_TimebendIsEngaged(void)
{
    uint32_t primask;
    int32_t phase_us;
    int32_t velocity_us_per_s;
    uint8_t engaged;

    primask = MidiOutput_EnterCritical();
    phase_us = midi_output_timebend_phase_offset_us_q16 >> 16;
    velocity_us_per_s = midi_output_timebend_velocity_us_per_s_q16 >> 16;
    engaged = (uint8_t)(midi_output_timebend_active
        && ((phase_us >= MIDI_TIMEBEND_POPUP_PHASE_THRESHOLD_US)
            || (phase_us <= -MIDI_TIMEBEND_POPUP_PHASE_THRESHOLD_US)
            || (velocity_us_per_s >= MIDI_TIMEBEND_POPUP_VELOCITY_THRESHOLD_US_PER_S)
            || (velocity_us_per_s <= -MIDI_TIMEBEND_POPUP_VELOCITY_THRESHOLD_US_PER_S)));
    MidiOutput_ExitCritical(primask);

    return engaged;
}

void MidiOutput_TakeTimebendDiagnostics(MidiOutputTimebendDiagnostics_t *diagnostics)
{
    uint32_t primask;
    uint8_t clock_depth;

    if (!diagnostics)
        return;

    primask = MidiOutput_EnterCritical();
    diagnostics->active = midi_output_timebend_active;
    diagnostics->phase_offset_us = midi_output_timebend_phase_offset_us_q16 >> 16;
    diagnostics->velocity_us_per_s = midi_output_timebend_velocity_us_per_s_q16 >> 16;
    diagnostics->due_depth = MidiOutput_TimebendDueDepthLocked();
    diagnostics->due_peak_depth = midi_output_timebend_due_peak_depth;
    diagnostics->enqueued_count = midi_output_timebend_diag_enqueued_count;
    diagnostics->emitted_count = midi_output_timebend_diag_emitted_count;
    diagnostics->dropped_count = midi_output_timebend_diag_dropped_count;
    diagnostics->clamp_min_count = midi_output_timebend_diag_clamp_min_count;
    diagnostics->clamp_max_count = midi_output_timebend_diag_clamp_max_count;
    diagnostics->late_sample_count = midi_output_timebend_diag_late_sample_count;
    diagnostics->late_avg_us = (midi_output_timebend_diag_late_sample_count == 0U)
        ? 0U
        : (midi_output_timebend_diag_late_sum_us / midi_output_timebend_diag_late_sample_count);
    diagnostics->late_max_us = midi_output_timebend_diag_late_max_us;
    diagnostics->emit_interval_sample_count = midi_output_timebend_diag_emit_interval_sample_count;
    diagnostics->emit_interval_avg_us = (midi_output_timebend_diag_emit_interval_sample_count == 0U)
        ? 0U
        : (midi_output_timebend_diag_emit_interval_sum_us / midi_output_timebend_diag_emit_interval_sample_count);
    diagnostics->emit_interval_min_us = (midi_output_timebend_diag_emit_interval_sample_count == 0U)
        ? 0U
        : midi_output_timebend_diag_emit_interval_min_us;
    diagnostics->emit_interval_max_us = (midi_output_timebend_diag_emit_interval_sample_count == 0U)
        ? 0U
        : midi_output_timebend_diag_emit_interval_max_us;
    diagnostics->missed_emit_count = midi_output_timebend_diag_missed_emit_count;
    diagnostics->crossing_backlog_now = MidiOutput_TimebendCrossingBacklogLocked();
    diagnostics->crossing_backlog_peak = midi_output_timebend_diag_crossing_backlog_peak;
    clock_depth = MidiOutput_ClockDepthLocked();
    diagnostics->uart_clock_depth = clock_depth;
    diagnostics->uart_clock_peak_depth = midi_output_clock_diag_peak_depth;
    diagnostics->phase_nonmono_count = midi_output_timebend_diag_phase_nonmono_count;

    midi_output_timebend_due_peak_depth = diagnostics->due_depth;
    midi_output_timebend_diag_enqueued_count = 0U;
    midi_output_timebend_diag_emitted_count = 0U;
    midi_output_timebend_diag_dropped_count = 0U;
    midi_output_timebend_diag_clamp_min_count = 0U;
    midi_output_timebend_diag_clamp_max_count = 0U;
    midi_output_timebend_diag_late_sum_us = 0U;
    midi_output_timebend_diag_late_max_us = 0U;
    midi_output_timebend_diag_late_sample_count = 0U;
    midi_output_timebend_diag_emit_interval_sum_us = 0U;
    midi_output_timebend_diag_emit_interval_min_us = UINT32_MAX;
    midi_output_timebend_diag_emit_interval_max_us = 0U;
    midi_output_timebend_diag_emit_interval_sample_count = 0U;
    midi_output_timebend_diag_missed_emit_count = 0U;
    midi_output_timebend_diag_crossing_backlog_peak = diagnostics->crossing_backlog_now;
    midi_output_timebend_diag_phase_nonmono_count = 0U;
    midi_output_clock_diag_peak_depth = clock_depth;

    MidiOutput_ExitCritical(primask);
}

__attribute__((section(".RamFunc")))
void MidiOutput_HandleTimingCounterIrq(void)
{
    uint32_t now;
    uint32_t due_us;
    uint32_t late_us;

    if (((TIM2->SR & TIM_SR_CC4IF) == 0U) || ((TIM2->DIER & TIM_DIER_CC4IE) == 0U))
        return;

    TIM2->SR = ~TIM_SR_CC4IF;

    now = TIM2->CNT;
    due_us = midi_output_timebend_next_due_us;
    midi_output_timebend_due_pending = 0U;
    midi_output_timebend_next_due_us = 0U;

    if ((due_us != 0U) && MidiOutput_TimeReached(now, due_us))
    {
        late_us = MidiOutput_TimerDiff(now, due_us);
        midi_output_timebend_diag_late_sum_us += late_us;
        if (late_us > midi_output_timebend_diag_late_max_us)
            midi_output_timebend_diag_late_max_us = late_us;
        if (midi_output_timebend_diag_late_sample_count < UINT32_MAX)
            midi_output_timebend_diag_late_sample_count++;
    }

    MidiOutput_TimebendUpdateModelLocked(now);
    MidiOutput_TimebendAdvancePhaseLocked(now);
    MidiOutput_TimebendEmitCrossingsLocked(now);
    MidiOutput_TimebendArmCompareLocked();
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

__attribute__((section(".RamFunc")))
static uint8_t MidiOutput_TimeReached(uint32_t now, uint32_t due)
{
    return ((int32_t)(now - due) >= 0) ? 1U : 0U;
}

__attribute__((section(".RamFunc")))
static void MidiOutput_TimebendResetLocked(void)
{
    midi_output_timebend_phase_offset_us_q16 = 0;
    midi_output_timebend_velocity_us_per_s_q16 = 0;
    midi_output_timebend_last_update_us = 0U;
    midi_output_timebend_last_encoder_us = 0U;
    midi_output_timebend_last_truth_pulse_us = 0U;
    midi_output_timebend_truth_interval_us = 0U;
    midi_output_timebend_phase_out_q24 = 0ULL;
    midi_output_timebend_next_edge_q24 = MIDI_TIMEBEND_PHASE_STEP_Q24;
    midi_output_timebend_last_phase_sample_us = 0U;
    midi_output_timebend_next_due_us = 0U;
    midi_output_timebend_due_pending = 0U;
    midi_output_timebend_due_peak_depth = 0U;
    midi_output_timebend_diag_enqueued_count = 0U;
    midi_output_timebend_diag_emitted_count = 0U;
    midi_output_timebend_diag_dropped_count = 0U;
    midi_output_timebend_diag_clamp_min_count = 0U;
    midi_output_timebend_diag_clamp_max_count = 0U;
    midi_output_timebend_diag_late_sum_us = 0U;
    midi_output_timebend_diag_late_max_us = 0U;
    midi_output_timebend_diag_late_sample_count = 0U;
    midi_output_timebend_diag_emit_interval_sum_us = 0U;
    midi_output_timebend_diag_emit_interval_min_us = UINT32_MAX;
    midi_output_timebend_diag_emit_interval_max_us = 0U;
    midi_output_timebend_diag_emit_interval_sample_count = 0U;
    midi_output_timebend_diag_missed_emit_count = 0U;
    midi_output_timebend_diag_crossing_backlog_peak = 0U;
    midi_output_timebend_diag_phase_nonmono_count = 0U;
    midi_output_clock_diag_peak_depth = MidiOutput_ClockDepthLocked();
    TIM2->DIER &= ~TIM_DIER_CC4IE;
    TIM2->SR = ~TIM_SR_CC4IF;
}

__attribute__((section(".RamFunc")))
static void MidiOutput_TimebendUpdateModelLocked(uint32_t now_us)
{
    uint32_t dt_us;
    uint32_t idle_us = 0U;
    int64_t phase_q16;
    int64_t velocity_q16;
    int64_t max_offset_q16;
    uint32_t truth_interval_us;
    uint32_t raw_max_offset_us;
    int64_t phase_deadband_q16;
    int64_t velocity_deadband_q16;
    int64_t max_velocity_q16;

    if (midi_output_timebend_last_update_us == 0U)
    {
        midi_output_timebend_last_update_us = now_us;
        return;
    }

    dt_us = MidiOutput_TimerDiff(now_us, midi_output_timebend_last_update_us);
    if (dt_us == 0U)
        return;

    if (midi_output_timebend_last_encoder_us != 0U)
        idle_us = MidiOutput_TimerDiff(now_us, midi_output_timebend_last_encoder_us);

    phase_q16 = midi_output_timebend_phase_offset_us_q16;
    velocity_q16 = midi_output_timebend_velocity_us_per_s_q16;

    /* While actively turning, keep commanded bend velocity "alive" so
     * emitted interval shift is clearly audible rather than chorused. */
    if (idle_us < MIDI_TIMEBEND_ACTIVE_HOLD_US)
    {
        phase_q16 += (velocity_q16 * (int64_t)dt_us) / 1000000LL;
    }
    else
    {
        velocity_q16 -= (velocity_q16 * (int64_t)dt_us) / (int64_t)MIDI_TIMEBEND_DECAY_US;
        velocity_q16 -= (phase_q16 * (int64_t)dt_us) / (int64_t)MIDI_TIMEBEND_SPRING_US;
        phase_q16 += (velocity_q16 * (int64_t)dt_us) / 1000000LL;
    }

    if (idle_us >= MIDI_TIMEBEND_RELEASE_FORCE_ZERO_US)
    {
        phase_q16 = 0;
        velocity_q16 = 0;
    }
    else if (idle_us >= MIDI_TIMEBEND_RELEASE_ACCEL_START_US)
    {
        /* Once the encoder is released, accelerate return to truth so the
         * bend settles musically in a short, predictable window. */
        velocity_q16 -= (velocity_q16 * (int64_t)dt_us) / (int64_t)MIDI_TIMEBEND_RELEASE_DECAY_US;
        phase_q16 -= (phase_q16 * (int64_t)dt_us) / (int64_t)MIDI_TIMEBEND_RELEASE_SPRING_US;
    }

    truth_interval_us = midi_output_timebend_truth_interval_us;
    if (truth_interval_us == 0U)
        truth_interval_us = (midi_output_clock_interval_us != 0U) ? midi_output_clock_interval_us : 20833U;

    raw_max_offset_us = (uint32_t)(((uint64_t)truth_interval_us * (uint64_t)MIDI_TIMEBEND_OFFSET_NUM)
        / (uint64_t)MIDI_TIMEBEND_OFFSET_DEN);
    if (raw_max_offset_us < (uint32_t)MIDI_TIMEBEND_MIN_OFFSET_US)
        raw_max_offset_us = (uint32_t)MIDI_TIMEBEND_MIN_OFFSET_US;

    max_offset_q16 = (int64_t)((uint64_t)raw_max_offset_us << 16);
    if (phase_q16 > max_offset_q16)
    {
        phase_q16 = max_offset_q16;
        if (velocity_q16 > 0)
            velocity_q16 = 0;
    }
    else if (phase_q16 < -max_offset_q16)
    {
        phase_q16 = -max_offset_q16;
        if (velocity_q16 < 0)
            velocity_q16 = 0;
    }

    max_velocity_q16 = ((int64_t)MIDI_TIMEBEND_MAX_VELOCITY_US_PER_S << 16);
    if (velocity_q16 > max_velocity_q16)
        velocity_q16 = max_velocity_q16;
    else if (velocity_q16 < -max_velocity_q16)
        velocity_q16 = -max_velocity_q16;

    phase_deadband_q16 = ((int64_t)MIDI_TIMEBEND_REST_PHASE_DEADBAND_US << 16);
    velocity_deadband_q16 = ((int64_t)MIDI_TIMEBEND_REST_VELOCITY_DEADBAND_US_PER_S << 16);
    if ((phase_q16 <= phase_deadband_q16) && (phase_q16 >= -phase_deadband_q16)
        && (velocity_q16 <= velocity_deadband_q16) && (velocity_q16 >= -velocity_deadband_q16))
    {
        /* Snap tiny residual oscillation to exact lock so UI and diagnostics
         * can reliably detect "back on truth clock" instead of hovering near 0. */
        phase_q16 = 0;
        velocity_q16 = 0;
    }

    midi_output_timebend_phase_offset_us_q16 = (int32_t)phase_q16;
    midi_output_timebend_velocity_us_per_s_q16 = (int32_t)velocity_q16;
    midi_output_timebend_last_update_us = now_us;
}

__attribute__((section(".RamFunc")))
static uint32_t MidiOutput_TimebendComputeTargetIntervalUsLocked(void)
{
    int64_t interval_bias_us;
    uint32_t truth_interval_us;
    int64_t target_interval_us;
    uint32_t min_interval_us;
    uint32_t max_interval_us;

    truth_interval_us = midi_output_timebend_truth_interval_us;
    if (truth_interval_us == 0U)
        truth_interval_us = (midi_output_clock_interval_us != 0U) ? midi_output_clock_interval_us : 20833U;

    interval_bias_us = ((int64_t)midi_output_timebend_phase_offset_us_q16) >> 16;
    target_interval_us = (int64_t)truth_interval_us + interval_bias_us;

    min_interval_us = (uint32_t)(((uint64_t)truth_interval_us * MIDI_TIMEBEND_MIN_INTERVAL_PCT) / 100ULL);
    max_interval_us = (uint32_t)(((uint64_t)truth_interval_us * MIDI_TIMEBEND_MAX_INTERVAL_PCT) / 100ULL);
    if (min_interval_us < MIDI_OUTPUT_POST_CLOCK_GUARD_US)
        min_interval_us = MIDI_OUTPUT_POST_CLOCK_GUARD_US;
    if (max_interval_us < min_interval_us)
        max_interval_us = min_interval_us;

    if (target_interval_us < (int64_t)min_interval_us)
    {
        target_interval_us = (int64_t)min_interval_us;
        midi_output_timebend_diag_clamp_min_count++;
    }
    else if (target_interval_us > (int64_t)max_interval_us)
    {
        target_interval_us = (int64_t)max_interval_us;
        midi_output_timebend_diag_clamp_max_count++;
    }

    return (uint32_t)target_interval_us;
}

__attribute__((section(".RamFunc")))
static void MidiOutput_TimebendAdvancePhaseLocked(uint32_t now_us)
{
    uint32_t dt_us;
    uint32_t target_interval_us;
    uint64_t phase_advance_q24;
    uint64_t phase_before_q24;
    uint32_t backlog_now;

    if (midi_output_timebend_last_phase_sample_us == 0U)
    {
        midi_output_timebend_last_phase_sample_us = now_us;
        return;
    }

    dt_us = MidiOutput_TimerDiff(now_us, midi_output_timebend_last_phase_sample_us);
    if (dt_us == 0U)
        return;

    target_interval_us = MidiOutput_TimebendComputeTargetIntervalUsLocked();
    if (target_interval_us == 0U)
        target_interval_us = 1U;

    phase_advance_q24 = (((uint64_t)dt_us * MIDI_TIMEBEND_PHASE_STEP_Q24)
                       + ((uint64_t)target_interval_us / 2ULL))
        / (uint64_t)target_interval_us;

    phase_before_q24 = midi_output_timebend_phase_out_q24;
    midi_output_timebend_phase_out_q24 += phase_advance_q24;
    if (midi_output_timebend_phase_out_q24 < phase_before_q24)
    {
        midi_output_timebend_phase_out_q24 = phase_before_q24;
        if (midi_output_timebend_diag_phase_nonmono_count < UINT32_MAX)
            midi_output_timebend_diag_phase_nonmono_count++;
    }

    midi_output_timebend_last_phase_sample_us = now_us;

    backlog_now = MidiOutput_TimebendCrossingBacklogLocked();
    if (backlog_now > midi_output_timebend_diag_crossing_backlog_peak)
        midi_output_timebend_diag_crossing_backlog_peak = backlog_now;
}

__attribute__((section(".RamFunc")))
static void MidiOutput_TimebendEmitCrossingsLocked(uint32_t now_us)
{
    uint32_t crossings_processed = 0U;

    while (midi_output_timebend_phase_out_q24 >= midi_output_timebend_next_edge_q24)
    {
        uint8_t next_head = (uint8_t)((midi_output_clock_head + 1U) % MIDI_OUTPUT_CLOCK_QUEUE_SIZE);
        uint32_t emitted_interval_us = 0U;

        if (next_head == midi_output_clock_tail)
        {
            if (midi_output_timebend_diag_dropped_count < UINT32_MAX)
                midi_output_timebend_diag_dropped_count++;
            if (midi_output_timebend_diag_missed_emit_count < UINT32_MAX)
                midi_output_timebend_diag_missed_emit_count++;
        }
        else
        {
            if ((midi_output_last_clock_us != 0U) && (now_us != midi_output_last_clock_us))
            {
                emitted_interval_us = MidiOutput_TimerDiff(now_us, midi_output_last_clock_us);
                midi_output_clock_interval_us = emitted_interval_us;
            }
            midi_output_last_clock_us = now_us;

            if (emitted_interval_us > 0U)
            {
                midi_output_timebend_diag_emit_interval_sum_us += emitted_interval_us;
                if (emitted_interval_us < midi_output_timebend_diag_emit_interval_min_us)
                    midi_output_timebend_diag_emit_interval_min_us = emitted_interval_us;
                if (emitted_interval_us > midi_output_timebend_diag_emit_interval_max_us)
                    midi_output_timebend_diag_emit_interval_max_us = emitted_interval_us;
                if (midi_output_timebend_diag_emit_interval_sample_count < UINT32_MAX)
                    midi_output_timebend_diag_emit_interval_sample_count++;
            }

            midi_output_clock_buffer[midi_output_clock_head] = MIDI_REALTIME_CLOCK;
            midi_output_clock_head = next_head;
            if (midi_output_timebend_diag_emitted_count < UINT32_MAX)
                midi_output_timebend_diag_emitted_count++;
            {
                uint8_t clock_depth = MidiOutput_ClockDepthLocked();

                if (clock_depth > midi_output_clock_diag_peak_depth)
                    midi_output_clock_diag_peak_depth = clock_depth;
            }
            MidiOutput_KickTx();
        }

        midi_output_timebend_next_edge_q24 += MIDI_TIMEBEND_PHASE_STEP_Q24;
        crossings_processed++;
        if (crossings_processed >= MIDI_TIMEBEND_MAX_CROSSINGS_PER_PASS)
        {
            uint32_t backlog_now = MidiOutput_TimebendCrossingBacklogLocked();

            if (backlog_now > 0U)
            {
                midi_output_timebend_next_edge_q24 += ((uint64_t)backlog_now * MIDI_TIMEBEND_PHASE_STEP_Q24);
                if ((UINT32_MAX - midi_output_timebend_diag_dropped_count) < backlog_now)
                    midi_output_timebend_diag_dropped_count = UINT32_MAX;
                else
                    midi_output_timebend_diag_dropped_count += backlog_now;

                if ((UINT32_MAX - midi_output_timebend_diag_missed_emit_count) < backlog_now)
                    midi_output_timebend_diag_missed_emit_count = UINT32_MAX;
                else
                    midi_output_timebend_diag_missed_emit_count += backlog_now;
            }
            break;
        }

        now_us = TIM2->CNT;
    }
}

__attribute__((section(".RamFunc")))
static uint32_t MidiOutput_TimebendCrossingBacklogLocked(void)
{
    uint64_t backlog64;

    if (midi_output_timebend_phase_out_q24 < midi_output_timebend_next_edge_q24)
        return 0U;

    backlog64 = ((midi_output_timebend_phase_out_q24 - midi_output_timebend_next_edge_q24)
        / MIDI_TIMEBEND_PHASE_STEP_Q24) + 1ULL;

    if (backlog64 > UINT32_MAX)
        return UINT32_MAX;

    return (uint32_t)backlog64;
}

__attribute__((section(".RamFunc")))
static void MidiOutput_TimebendArmCompareLocked(void)
{
    uint32_t now_us;
    uint32_t truth_interval_us;
    uint32_t target_interval_us;
    uint32_t wait_us;
    uint64_t remaining_phase_q24;
    uint64_t elapsed_since_truth_us;

    now_us = TIM2->CNT;

    truth_interval_us = midi_output_timebend_truth_interval_us;
    if (truth_interval_us == 0U)
        truth_interval_us = (midi_output_clock_interval_us != 0U) ? midi_output_clock_interval_us : 20833U;

    elapsed_since_truth_us = (midi_output_timebend_last_truth_pulse_us == 0U)
        ? UINT64_MAX
        : (uint64_t)MidiOutput_TimerDiff(now_us, midi_output_timebend_last_truth_pulse_us);
    if ((midi_output_timebend_last_truth_pulse_us == 0U)
        || (elapsed_since_truth_us > ((uint64_t)truth_interval_us * (uint64_t)MIDI_TIMEBEND_TRUTH_TIMEOUT_MULTIPLIER)))
    {
        midi_output_timebend_due_pending = 0U;
        midi_output_timebend_next_due_us = 0U;
        TIM2->DIER &= ~TIM_DIER_CC4IE;
        TIM2->SR = ~TIM_SR_CC4IF;
        return;
    }

    target_interval_us = MidiOutput_TimebendComputeTargetIntervalUsLocked();

    if (midi_output_timebend_phase_out_q24 >= midi_output_timebend_next_edge_q24)
    {
        wait_us = MIDI_OUTPUT_POST_CLOCK_GUARD_US;
    }
    else
    {
        remaining_phase_q24 = midi_output_timebend_next_edge_q24 - midi_output_timebend_phase_out_q24;
        wait_us = (uint32_t)((remaining_phase_q24 * (uint64_t)target_interval_us
            + (MIDI_TIMEBEND_PHASE_STEP_Q24 / 2ULL))
            / MIDI_TIMEBEND_PHASE_STEP_Q24);
        if (wait_us < MIDI_OUTPUT_POST_CLOCK_GUARD_US)
            wait_us = MIDI_OUTPUT_POST_CLOCK_GUARD_US;
    }

    midi_output_timebend_next_due_us = now_us + wait_us;
    midi_output_timebend_due_pending = 1U;
    midi_output_timebend_diag_enqueued_count++;
    if (midi_output_timebend_due_peak_depth < 1U)
        midi_output_timebend_due_peak_depth = 1U;

    TIM2->CCR4 = midi_output_timebend_next_due_us;
    TIM2->SR = ~TIM_SR_CC4IF;
    TIM2->DIER |= TIM_DIER_CC4IE;
}

__attribute__((section(".RamFunc")))
static uint8_t MidiOutput_TimebendDueDepthLocked(void)
{
    return midi_output_timebend_due_pending ? 1U : 0U;
}

__attribute__((section(".RamFunc")))
static uint8_t MidiOutput_ClockDepthLocked(void)
{
    return (midi_output_clock_head >= midi_output_clock_tail)
        ? (uint8_t)(midi_output_clock_head - midi_output_clock_tail)
        : (uint8_t)(MIDI_OUTPUT_CLOCK_QUEUE_SIZE - midi_output_clock_tail + midi_output_clock_head);
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