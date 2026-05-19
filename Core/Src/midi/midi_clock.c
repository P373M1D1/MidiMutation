#include "midi_functions.h"
#include "midi/midi_clock_internal.h"
#include "midi/midi_feedback.h"
#include "midi/midi_output.h"
#include "midi/midi_transport_internal.h"

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

#define MIDI_REALTIME_CLOCK                0xF8U
#define MIDI_CLOCK_OUTPUT_TIMER_TICK_HZ    100000U
#define MIDI_CLOCK_OUTPUT_COUNTS_PER_MINUTE (MIDI_CLOCK_OUTPUT_TIMER_TICK_HZ * 60U)
#define MIDI_CLOCK_OUTPUT_TIMER_PRESCALER_DIVISOR 960U
#define MIDI_CLOCK_OUTPUT_IRQ_PREEMPT_PRIORITY 2U
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
            if (!midi_clock_flash_busy())
                MidiFeedback_PulseInternalBeat();
            AppMetronome_OnQuarterNote(APP_METRONOME_SOURCE_INTERNAL);
        }
    }

    if (DAC->SR & (DAC_SR_DMAUDR1 | DAC_SR_DMAUDR2))
        DAC->SR |= (DAC_SR_DMAUDR1 | DAC_SR_DMAUDR2);
}

__attribute__((section(".RamFunc")))
uint8_t MidiClockHandleInternalPulse(void)
{
    (void)MidiOutput_QueueRealtimeByte(MIDI_REALTIME_CLOCK);

    midi_internal_clock_pulse_count++;
    if (midi_internal_clock_pulse_count < MIDI_CLOCK_PULSES_PER_QUARTER_NOTE)
        return 0U;

    midi_internal_clock_pulse_count = 0U;
    return 1U;
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