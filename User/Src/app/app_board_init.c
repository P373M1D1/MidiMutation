#include "app/app_board_init.h"

#include "app/app_metronome.h"

#include "main.h"

#include "led_functions.h"
#include "midi_functions.h"

#define APP_BOARD_MIDI_OUTPUT_UART_INSTANCE UART4
#define APP_BOARD_MIDI_OUTPUT_RX_GPIO_PORT GPIOD
#define APP_BOARD_MIDI_OUTPUT_RX_PIN GPIO_PIN_0
#define APP_BOARD_MIDI_OUTPUT_RX_AF GPIO_AF11_UART4
#define APP_BOARD_MIDI_OUTPUT_TX_GPIO_PORT GPIOD
#define APP_BOARD_MIDI_OUTPUT_TX_PIN GPIO_PIN_1
#define APP_BOARD_MIDI_OUTPUT_TX_AF GPIO_AF11_UART4
#define APP_BOARD_MIDI_OUTPUT_UART_IRQ_PREEMPT_PRIORITY 2U
#define APP_BOARD_MIDI_OUTPUT_UART_IRQ_SUBPRIORITY 0U

#define APP_BOARD_TIMING_COUNTER_IRQ_PREEMPT_PRIORITY 1U
#define APP_BOARD_TIMING_COUNTER_IRQ_SUBPRIORITY 0U

#define APP_BOARD_RELAY1_GPIO_PORT GPIOG
#define APP_BOARD_RELAY1_PIN GPIO_PIN_10

#define APP_BOARD_METRONOME_PWM_GPIO_PORT GPIOB
#define APP_BOARD_METRONOME_PWM_PIN GPIO_PIN_8
#define APP_BOARD_METRONOME_PWM_AF GPIO_AF2_TIM4
#define APP_BOARD_METRONOME_PWM_TIMER_INSTANCE TIM4
#define APP_BOARD_METRONOME_PWM_CHANNEL TIM_CHANNEL_3
#define APP_BOARD_METRONOME_PWM_TIMER_TICK_HZ 1000000UL
#define APP_BOARD_METRONOME_PWM_TIMER_PRESCALER_DIVISOR 96U
#define APP_BOARD_METRONOME_PWM_DEFAULT_FREQUENCY_HZ 1000U
#define APP_BOARD_METRONOME_PWM_MIN_FREQUENCY_HZ 100U
#define APP_BOARD_TIMING_COMPARE_GUARD_US 20UL

static TIM_HandleTypeDef app_board_tim2;
static TIM_HandleTypeDef app_board_metronome_pwm_timer;
static UART_HandleTypeDef app_board_midi_output_uart;
static uint8_t app_board_metronome_pwm_initialized = 0U;
static volatile uint8_t app_board_metronome_pwm_running = 0U;

__attribute__((always_inline))
static inline uint32_t AppBoard_EnterCritical(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    return primask;
}

__attribute__((always_inline))
static inline void AppBoard_ExitCritical(uint32_t primask)
{
    if (primask == 0U)
        __enable_irq();
}

static void AppBoard_InitMidiOutputUart(void);
static void AppBoard_InitMetronomePwm(void);
static void AppBoard_InitTimingCounter(void);
static void AppBoard_InitRelayOutputs(void);
__attribute__((section(".RamFunc")))
static uint8_t AppBoard_TimingCompareReached(uint32_t now_us, uint32_t due_us);
__attribute__((section(".RamFunc")))
static uint32_t AppBoard_MetronomePwmPeriodCounts(uint16_t frequency_hz);
__attribute__((section(".RamFunc")))
static uint32_t AppBoard_MetronomePwmPulseCounts(uint32_t period_counts, uint8_t volume);
__attribute__((section(".RamFunc")))
static void AppBoard_MetronomePwmStopImmediate(void);

void AppBoard_InitStartupPeripherals(void)
{
    AppBoard_InitTimingCounter();
    AppBoard_InitMetronomePwm();
    AppBoard_InitRelayOutputs();
    MidiInitInput();
    AppBoard_InitMidiOutputUart();
    MidiSetOutputUart(&app_board_midi_output_uart);
}

void AppBoard_SetRelayState(uint8_t relay_index, uint8_t closed)
{
    if (relay_index != 0U)
        return;

    HAL_GPIO_WritePin(APP_BOARD_RELAY1_GPIO_PORT,
                      APP_BOARD_RELAY1_PIN,
                      closed ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void AppBoard_InitMidiOutputUart(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_UART4_CLK_ENABLE();

    gpio_init.Pin = APP_BOARD_MIDI_OUTPUT_RX_PIN;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_init.Alternate = APP_BOARD_MIDI_OUTPUT_RX_AF;
    HAL_GPIO_Init(APP_BOARD_MIDI_OUTPUT_RX_GPIO_PORT, &gpio_init);

    gpio_init.Pin = APP_BOARD_MIDI_OUTPUT_TX_PIN;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_init.Alternate = APP_BOARD_MIDI_OUTPUT_TX_AF;
    HAL_GPIO_Init(APP_BOARD_MIDI_OUTPUT_TX_GPIO_PORT, &gpio_init);

    app_board_midi_output_uart.Instance = APP_BOARD_MIDI_OUTPUT_UART_INSTANCE;
    app_board_midi_output_uart.Init.BaudRate = MIDI_BAUD_RATE;
    app_board_midi_output_uart.Init.WordLength = UART_WORDLENGTH_8B;
    app_board_midi_output_uart.Init.StopBits = UART_STOPBITS_1;
    app_board_midi_output_uart.Init.Parity = UART_PARITY_NONE;
    app_board_midi_output_uart.Init.Mode = UART_MODE_TX_RX;
    app_board_midi_output_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    app_board_midi_output_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&app_board_midi_output_uart) != HAL_OK)
        Error_Handler();

    HAL_NVIC_SetPriority(UART4_IRQn,
                         APP_BOARD_MIDI_OUTPUT_UART_IRQ_PREEMPT_PRIORITY,
                         APP_BOARD_MIDI_OUTPUT_UART_IRQ_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(UART4_IRQn);
    __HAL_UART_ENABLE_IT(&app_board_midi_output_uart, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&app_board_midi_output_uart, UART_IT_ERR);
}

static void AppBoard_InitTimingCounter(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();
    app_board_tim2.Instance = TIM2;
    app_board_tim2.Init.Prescaler = 95U;
    app_board_tim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    app_board_tim2.Init.Period = 0xFFFFFFFFU;
    app_board_tim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    app_board_tim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&app_board_tim2) != HAL_OK)
        Error_Handler();

    app_board_tim2.Instance->CCMR1 &= ~(TIM_CCMR1_CC1S | TIM_CCMR1_OC1M
                                      | TIM_CCMR1_CC2S | TIM_CCMR1_OC2M);
    app_board_tim2.Instance->CCMR2 &= ~(TIM_CCMR2_CC3S | TIM_CCMR2_OC3M
                                      | TIM_CCMR2_CC4S | TIM_CCMR2_OC4M);
    app_board_tim2.Instance->CCER &= ~(TIM_CCER_CC1E | TIM_CCER_CC2E
                                     | TIM_CCER_CC3E | TIM_CCER_CC4E);
    app_board_tim2.Instance->CCR1 = 0U;
    app_board_tim2.Instance->CCR2 = 0U;
    app_board_tim2.Instance->CCR3 = 0U;
    app_board_tim2.Instance->CCR4 = 0U;
    app_board_tim2.Instance->DIER &= ~(TIM_DIER_CC1IE | TIM_DIER_CC2IE
                                     | TIM_DIER_CC3IE | TIM_DIER_CC4IE);
    app_board_tim2.Instance->SR = 0U;

    HAL_NVIC_SetPriority(TIM2_IRQn,
                         APP_BOARD_TIMING_COUNTER_IRQ_PREEMPT_PRIORITY,
                         APP_BOARD_TIMING_COUNTER_IRQ_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);

    HAL_TIM_Base_Start(&app_board_tim2);
}

__attribute__((section(".RamFunc")))
void AppBoard_HandleTimingCounterIrq(void)
{
    if (((TIM2->SR & TIM_SR_CC2IF) != 0U)
     && ((TIM2->DIER & TIM_DIER_CC2IE) != 0U))
    {
        AppBoard_MetronomePwmStopImmediate();
    }

    MidiHandleTimingCounterIrq();

    /**
     * Dispatch due compare edges immediately in IRQ context so beat LED and
     * metronome timing are not quantized by the 10 ms foreground service.
     * These handlers are lightweight and operate on pre-queued compare state.
     */
    LED_HandleTimingCounterIrq();
    AppMetronome_HandleTimingCounterIrq();
}

__attribute__((section(".RamFunc")))
uint8_t AppBoard_MetronomePwmIsAvailable(void)
{
    return app_board_metronome_pwm_initialized;
}

__attribute__((section(".RamFunc")))
uint8_t AppBoard_MetronomePwmStart(uint16_t frequency_hz, uint8_t volume, uint32_t duration_us)
{
    uint32_t primask;
    uint32_t period_counts;
    uint32_t pulse_counts;
    uint32_t now_us;
    uint32_t stop_due_us;

    if (!app_board_metronome_pwm_initialized || volume == 0U)
        return 0U;

    period_counts = AppBoard_MetronomePwmPeriodCounts(frequency_hz);
    pulse_counts = AppBoard_MetronomePwmPulseCounts(period_counts, volume);
    if (pulse_counts == 0U)
        return 0U;

    primask = AppBoard_EnterCritical();

    AppBoard_MetronomePwmStopImmediate();

    app_board_metronome_pwm_timer.Instance->ARR = period_counts - 1U;
    app_board_metronome_pwm_timer.Instance->CCR3 = pulse_counts;
    app_board_metronome_pwm_timer.Instance->CNT = 0U;
    app_board_metronome_pwm_timer.Instance->EGR = TIM_EGR_UG;
    app_board_metronome_pwm_timer.Instance->CCER |= TIM_CCER_CC3E;
    app_board_metronome_pwm_timer.Instance->CR1 |= TIM_CR1_CEN;

    now_us = TIM2->CNT;
    stop_due_us = now_us + duration_us;
    if (AppBoard_TimingCompareReached(now_us + APP_BOARD_TIMING_COMPARE_GUARD_US, stop_due_us))
        stop_due_us = now_us + APP_BOARD_TIMING_COMPARE_GUARD_US;

    TIM2->CCR2 = stop_due_us;
    TIM2->SR = ~TIM_SR_CC2IF;
    TIM2->DIER |= TIM_DIER_CC2IE;

    app_board_metronome_pwm_running = 1U;

    AppBoard_ExitCritical(primask);

    return 1U;
}

void AppBoard_MetronomePwmStop(void)
{
    uint32_t primask;

    if (!app_board_metronome_pwm_initialized)
        return;

    primask = AppBoard_EnterCritical();
    AppBoard_MetronomePwmStopImmediate();
    AppBoard_ExitCritical(primask);
}

__attribute__((section(".RamFunc")))
static uint8_t AppBoard_TimingCompareReached(uint32_t now_us, uint32_t due_us)
{
    return ((int32_t)(now_us - due_us) >= 0) ? 1U : 0U;
}

static void AppBoard_InitMetronomePwm(void)
{
    GPIO_InitTypeDef gpio_init = {0};
    TIM_OC_InitTypeDef pwm_config = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();

    gpio_init.Pin = APP_BOARD_METRONOME_PWM_PIN;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_init.Alternate = APP_BOARD_METRONOME_PWM_AF;
    HAL_GPIO_Init(APP_BOARD_METRONOME_PWM_GPIO_PORT, &gpio_init);

    app_board_metronome_pwm_timer.Instance = APP_BOARD_METRONOME_PWM_TIMER_INSTANCE;
    app_board_metronome_pwm_timer.Init.Prescaler = APP_BOARD_METRONOME_PWM_TIMER_PRESCALER_DIVISOR - 1U;
    app_board_metronome_pwm_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    app_board_metronome_pwm_timer.Init.Period = AppBoard_MetronomePwmPeriodCounts(
        APP_BOARD_METRONOME_PWM_DEFAULT_FREQUENCY_HZ) - 1U;
    app_board_metronome_pwm_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    app_board_metronome_pwm_timer.Init.RepetitionCounter = 0U;
    app_board_metronome_pwm_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&app_board_metronome_pwm_timer) != HAL_OK)
        Error_Handler();

    pwm_config.OCMode = TIM_OCMODE_PWM1;
    pwm_config.Pulse = 0U;
    pwm_config.OCPolarity = TIM_OCPOLARITY_HIGH;
    pwm_config.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    pwm_config.OCFastMode = TIM_OCFAST_DISABLE;
    pwm_config.OCIdleState = TIM_OCIDLESTATE_RESET;
    pwm_config.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if (HAL_TIM_PWM_ConfigChannel(&app_board_metronome_pwm_timer,
                                  &pwm_config,
                                  APP_BOARD_METRONOME_PWM_CHANNEL) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_TIM_SET_COUNTER(&app_board_metronome_pwm_timer, 0U);
    app_board_metronome_pwm_initialized = 1U;
}

static void AppBoard_InitRelayOutputs(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOG_CLK_ENABLE();

    gpio_init.Pin = APP_BOARD_RELAY1_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(APP_BOARD_RELAY1_GPIO_PORT, &gpio_init);

    /* Default to open relay (transistor off) on startup. */
    HAL_GPIO_WritePin(APP_BOARD_RELAY1_GPIO_PORT, APP_BOARD_RELAY1_PIN, GPIO_PIN_RESET);
}

__attribute__((section(".RamFunc")))
static uint32_t AppBoard_MetronomePwmPeriodCounts(uint16_t frequency_hz)
{
    uint32_t safe_frequency_hz = (frequency_hz < APP_BOARD_METRONOME_PWM_MIN_FREQUENCY_HZ)
        ? APP_BOARD_METRONOME_PWM_MIN_FREQUENCY_HZ
        : (uint32_t)frequency_hz;
    uint32_t period_counts = (APP_BOARD_METRONOME_PWM_TIMER_TICK_HZ + (safe_frequency_hz / 2U))
        / safe_frequency_hz;

    if (period_counts < 2U)
        period_counts = 2U;
    else if (period_counts > 0x10000UL)
        period_counts = 0x10000UL;

    return period_counts;
}

__attribute__((section(".RamFunc")))
static uint32_t AppBoard_MetronomePwmPulseCounts(uint32_t period_counts, uint8_t volume)
{
    uint32_t safe_volume = (volume > 100U) ? 100U : (uint32_t)volume;
    uint32_t pulse_counts;

    if (safe_volume == 0U || period_counts < 2U)
        return 0U;

    pulse_counts = ((period_counts * safe_volume) + 199U) / 200U;
    if (pulse_counts == 0U)
        pulse_counts = 1U;
    else if (pulse_counts >= period_counts)
        pulse_counts = period_counts - 1U;

    return pulse_counts;
}

__attribute__((section(".RamFunc")))
static void AppBoard_MetronomePwmStopImmediate(void)
{
    if (!app_board_metronome_pwm_initialized)
        return;

    TIM2->DIER &= ~TIM_DIER_CC2IE;
    TIM2->SR = ~TIM_SR_CC2IF;
    app_board_metronome_pwm_timer.Instance->CCER &= ~TIM_CCER_CC3E;
    app_board_metronome_pwm_timer.Instance->CR1 &= ~TIM_CR1_CEN;
    app_board_metronome_pwm_timer.Instance->CCR3 = 0U;
    app_board_metronome_pwm_timer.Instance->CNT = 0U;
    app_board_metronome_pwm_running = 0U;
}