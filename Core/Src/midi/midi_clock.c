/*

This is the MIDI clock output module, which owns the internal clock generation and output stream on TIM6. 
It also provides the API for aligning the internal clock phase to the external clock and for tracking external
pulse intervals to adapt the internal tempo. The external clock estimation and synchronization lifecycle management 
all live in midi_clock_estimator.c, which feeds recovered tempo and phase information back to this module for output adjustments.



*/

#include "midi_functions.h"
#include "midi/midi_clock_estimator.h"
#include "midi/midi_clock_internal.h"
#include "midi/midi_feedback.h"
#include "midi/midi_output.h"
#define MIDI_TRANSPORT_INTERNAL_ACCESS 1
#include "midi/midi_transport_internal.h"
#undef MIDI_TRANSPORT_INTERNAL_ACCESS

#include "app/app_metronome.h"

#if !MIDI_CLOCK_LOOPBACK_MONITOR_ONLY
#include "app/app_midi_clock_sync.h"
#endif

void Error_Handler(void);

/* TIM6-owned internal MIDI clock generation.
 *
 * Incoming transport/sync parsing lives in midi_transport.c. This module keeps
 * only the hardware timer/output side plus the internal quarter-note counter
 * used by the TIM6-driven realtime clock stream.
 */

static TIM_HandleTypeDef midi_clock_output_timer;
static uint8_t midi_internal_clock_pulse_count = 0U;
static uint32_t midi_internal_transport_pulse_count = 0U;
static volatile uint8_t midi_clock_realtime_output_enabled = 1U;

#define MIDI_REALTIME_CLOCK                0xF8U
#define MIDI_CLOCK_OUTPUT_TIMER_TICK_HZ    100000U
#define MIDI_CLOCK_OUTPUT_COUNTS_PER_MINUTE (MIDI_CLOCK_OUTPUT_TIMER_TICK_HZ * 60U)
#define MIDI_CLOCK_OUTPUT_TIMER_PRESCALER_DIVISOR 960U
#define MIDI_CLOCK_OUTPUT_IRQ_PREEMPT_PRIORITY 0U
#define MIDI_CLOCK_OUTPUT_IRQ_SUBPRIORITY  0U

static void midi_clock_output_apply_pulse_counts(uint32_t pulse_counts);
__attribute__((section(".RamFunc")))
static uint8_t midi_clock_external_signal_present_fast(void);
__attribute__((section(".RamFunc")))
static uint8_t midi_clock_flash_busy(void);

#if !MIDI_CLOCK_LOOPBACK_MONITOR_ONLY
static uint32_t midi_clock_output_counts_for_pulse_interval_us(uint32_t pulse_interval_us);
#endif

void MidiClockOutputInit(uint16_t bpm)
{
    __HAL_RCC_TIM6_CLK_ENABLE();
    midi_clock_output_timer.Instance = TIM6;
    midi_clock_output_timer.Init.Prescaler = MIDI_CLOCK_OUTPUT_TIMER_PRESCALER_DIVISOR - 1U;
    midi_clock_output_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    midi_clock_output_timer.Init.Period = MidiClockOutputTimerPeriodForBpm(bpm);
    midi_clock_output_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&midi_clock_output_timer) != HAL_OK)
        Error_Handler();

    __HAL_TIM_CLEAR_FLAG(&midi_clock_output_timer, TIM_FLAG_UPDATE);
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn,
                         MIDI_CLOCK_OUTPUT_IRQ_PREEMPT_PRIORITY,
                         MIDI_CLOCK_OUTPUT_IRQ_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);

    if (HAL_TIM_Base_Start_IT(&midi_clock_output_timer) != HAL_OK)
        Error_Handler();
}

__attribute__((section(".RamFunc")))
void MidiClockOutputIrqHandler(void)
{
    if (TIM6->SR & TIM_SR_UIF)
    {
        TIM6->SR = ~TIM_SR_UIF;
        if (MidiClockHandleInternalPulse() && !midi_clock_external_signal_present_fast())
        {
            /**
             * Internal beat feedback is only emitted while no active external
             * signal is present. If external clock pulses are seen without
             * transport running (e.g. hot-plug without START/CONTINUE), beat
             * output stays suppressed until transport is explicitly armed.
             */
            uint32_t now = TIM2->CNT;

            if (!midi_clock_flash_busy())
                MidiFeedback_PulseInternalBeatAt(now);
            AppMetronome_OnQuarterNoteAt(APP_METRONOME_SOURCE_INTERNAL, now);
        }
    }

    if (DAC->SR & (DAC_SR_DMAUDR1 | DAC_SR_DMAUDR2))
        DAC->SR |= (DAC_SR_DMAUDR1 | DAC_SR_DMAUDR2);
}

__attribute__((section(".RamFunc")))
uint8_t MidiClockHandleInternalPulse(void)
{
    if (midi_clock_realtime_output_enabled)
        (void)MidiOutput_QueueRealtimeByte(MIDI_REALTIME_CLOCK);

    midi_internal_transport_pulse_count++;
    midi_internal_clock_pulse_count++;
    if (midi_internal_clock_pulse_count < MIDI_CLOCK_PULSES_PER_QUARTER_NOTE)
        return 0U;

    midi_internal_clock_pulse_count = 0U;
    return 1U;
}

void MidiClockSetRealtimeOutputEnabled(uint8_t enabled)
{
    uint8_t next_enabled = enabled ? 1U : 0U;

    if (next_enabled == midi_clock_realtime_output_enabled)
        return;

    midi_clock_realtime_output_enabled = next_enabled;

    if (!midi_clock_realtime_output_enabled)
        MidiOutput_ResetRealtimePacingGuard();
}

__attribute__((section(".RamFunc")))
static uint8_t midi_clock_external_signal_present_fast(void)
{
    uint32_t last_pulse_us = midi_clock_last_pulse_us;

    if (midi_transport_running)
        return 1U;

    if (last_pulse_us == 0U)
        return 0U;

    return (uint8_t)((TIM2->CNT - last_pulse_us) <= midi_clock_external_activity_timeout_us);
}

__attribute__((section(".RamFunc")))
static uint8_t midi_clock_flash_busy(void)
{
    return ((FLASH->SR & FLASH_SR_BSY) != 0U) ? 1U : 0U;
}

uint32_t MidiClockOutputTimerPeriodForBpm(uint16_t bpm)
{
    uint32_t denominator = (uint32_t)bpm * MIDI_CLOCK_PULSES_PER_QUARTER_NOTE;
    uint32_t pulse_counts = (MIDI_CLOCK_OUTPUT_COUNTS_PER_MINUTE + (denominator / 2U)) / denominator;

    if (pulse_counts == 0U)
        pulse_counts = 1U;

    return pulse_counts - 1U;
}

void MidiClockOutputSetTempoBpm(uint16_t bpm)
{
    midi_clock_output_apply_pulse_counts(MidiClockOutputTimerPeriodForBpm(bpm) + 1U);
}

__attribute__((section(".RamFunc")))
void MidiClock_ResetInternalPulseCount(void)
{
    midi_internal_clock_pulse_count = 0U;
    midi_internal_transport_pulse_count = 0U;
}

__attribute__((section(".RamFunc")))
void MidiClock_AlignInternalPhaseToExternal(uint32_t now_us,
                                            uint32_t last_pulse_us,
                                            uint32_t external_pulse_count)
{
    uint32_t pulse_counts;
    uint32_t phase_counts;
    uint32_t transport_pulse_count;
    uint8_t pulse_phase;
    uint32_t primask;
    uint64_t elapsed_counts;
    uint64_t elapsed_pulses;

    if (last_pulse_us == 0U)
        return;

    pulse_counts = midi_clock_output_timer.Instance->ARR + 1U;
    if (pulse_counts == 0U)
        pulse_counts = 1U;

    elapsed_counts = (((uint64_t)(now_us - last_pulse_us) * (uint64_t)MIDI_CLOCK_OUTPUT_TIMER_TICK_HZ) + 500000ULL)
        / 1000000ULL;
    elapsed_pulses = elapsed_counts / (uint64_t)pulse_counts;
    phase_counts = (uint32_t)(elapsed_counts % (uint64_t)pulse_counts);
    transport_pulse_count = external_pulse_count + (uint32_t)elapsed_pulses;
    pulse_phase = (uint8_t)((external_pulse_count + (uint32_t)elapsed_pulses)
        % MIDI_CLOCK_PULSES_PER_QUARTER_NOTE);

    primask = __get_PRIMASK();
    __disable_irq();
    midi_internal_transport_pulse_count = transport_pulse_count;
    midi_internal_clock_pulse_count = pulse_phase;
    midi_clock_output_timer.Instance->CNT = phase_counts;
    if (primask == 0U)
        __enable_irq();
}

__attribute__((section(".RamFunc")))
void MidiClock_HandoffExternalPhaseToInternal(uint32_t now_us)
{
    uint32_t last_pulse_us;
    uint32_t total_tick_count;
    uint32_t origin_tick_count;
    uint32_t primask;
    MidiClockEstimatorStatus_t estimator_status;

    primask = __get_PRIMASK();
    __disable_irq();
    last_pulse_us = midi_clock_last_captured_pulse_us;
    total_tick_count = midi_transport_global_tick_count;
    origin_tick_count = midi_transport_origin_tick_count;
    if (primask == 0U)
        __enable_irq();

    MidiClockEstimator_GetStatus(&estimator_status);
    if (estimator_status.live_lock == MIDI_CLOCK_LOCK_QUALITY_LOCKED
     && estimator_status.publication_ready)
    {
        (void)MidiClockEstimator_GetRecoveredPulseTimestampUs(&last_pulse_us);
    }

    MidiClock_AlignInternalPhaseToExternal(now_us,
                                           last_pulse_us,
                                           total_tick_count - origin_tick_count);
}

__attribute__((section(".RamFunc")))
uint32_t MidiClock_GetOutputPulseIntervalUs(void)
{
    uint32_t primask;
    uint32_t pulse_counts;

    primask = __get_PRIMASK();
    __disable_irq();
    pulse_counts = midi_clock_output_timer.Instance->ARR + 1U;
    if (primask == 0U)
        __enable_irq();

    if (pulse_counts == 0U)
        pulse_counts = 1U;

    return (uint32_t)((((uint64_t)pulse_counts * 1000000ULL)
        + ((uint64_t)MIDI_CLOCK_OUTPUT_TIMER_TICK_HZ / 2ULL))
        / (uint64_t)MIDI_CLOCK_OUTPUT_TIMER_TICK_HZ);
}

void MidiClock_GetInternalPhaseSnapshot(uint32_t *pulse_count,
                                        uint32_t *phase_counts,
                                        uint32_t *pulse_counts)
{
    TIM_TypeDef *timer = midi_clock_output_timer.Instance;
    uint32_t total_pulse_count = midi_internal_transport_pulse_count;
    uint32_t timer_phase_counts = 0U;
    uint32_t timer_pulse_counts = 1U;

    if (timer != NULL)
    {
        timer_phase_counts = timer->CNT;
        timer_pulse_counts = timer->ARR + 1U;
        if (timer_pulse_counts == 0U)
            timer_pulse_counts = 1U;

        if ((timer->SR & TIM_SR_UIF) != 0U)
        {
            total_pulse_count++;
            timer_phase_counts = timer->CNT;
        }
    }

    if (pulse_count)
        *pulse_count = total_pulse_count;
    if (phase_counts)
        *phase_counts = timer_phase_counts;
    if (pulse_counts)
        *pulse_counts = timer_pulse_counts;
}

static void midi_clock_output_apply_pulse_counts(uint32_t pulse_counts)
{
    uint32_t primask = __get_PRIMASK();

    if (pulse_counts == 0U)
        pulse_counts = 1U;

    __disable_irq();
    midi_clock_output_timer.Instance->ARR = pulse_counts - 1U;
    midi_clock_output_timer.Instance->CNT = 0U;
    if (primask == 0U)
        __enable_irq();
}

#if !MIDI_CLOCK_LOOPBACK_MONITOR_ONLY
static uint32_t midi_clock_output_counts_for_pulse_interval_us(uint32_t pulse_interval_us)
{
    uint64_t pulse_counts;

    if (pulse_interval_us == 0U)
        return 1U;

    pulse_counts = (((uint64_t)MIDI_CLOCK_OUTPUT_TIMER_TICK_HZ * (uint64_t)pulse_interval_us) + 500000ULL) / 1000000ULL;
    if (pulse_counts == 0U)
        pulse_counts = 1U;
    else if (pulse_counts > 0x10000ULL)
        pulse_counts = 0x10000ULL;

    return (uint32_t)pulse_counts;
}

void MidiClock_ResetOutputPhase(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    midi_clock_output_timer.Instance->CNT = 0U;
    if (primask == 0U)
        __enable_irq();
}

void MidiClock_TrackExternalPulseInterval(uint32_t interval_us)
{
    AppMidiClock_TrackExternalPulseInterval(interval_us);

    midi_clock_output_apply_pulse_counts(midi_clock_output_counts_for_pulse_interval_us(interval_us));
}
#endif