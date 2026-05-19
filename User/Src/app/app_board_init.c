#include "app/app_board_init.h"

#include "main.h"

#include "midi_functions.h"

#define APP_BOARD_MIDI_OUTPUT_UART_INSTANCE UART4
#define APP_BOARD_MIDI_OUTPUT_RX_GPIO_PORT GPIOD
#define APP_BOARD_MIDI_OUTPUT_RX_PIN GPIO_PIN_0
#define APP_BOARD_MIDI_OUTPUT_RX_AF GPIO_AF11_UART4
#define APP_BOARD_MIDI_OUTPUT_TX_GPIO_PORT GPIOD
#define APP_BOARD_MIDI_OUTPUT_TX_PIN GPIO_PIN_1
#define APP_BOARD_MIDI_OUTPUT_TX_AF GPIO_AF11_UART4
#define APP_BOARD_MIDI_OUTPUT_UART_IRQ_PREEMPT_PRIORITY 1U
#define APP_BOARD_MIDI_OUTPUT_UART_IRQ_SUBPRIORITY 0U

#define APP_BOARD_METRONOME_PWM_GPIO_PORT GPIOB
#define APP_BOARD_METRONOME_PWM_PIN GPIO_PIN_8
#define APP_BOARD_METRONOME_PWM_AF GPIO_AF2_TIM4
#define APP_BOARD_METRONOME_PWM_TIMER_INSTANCE TIM4
#define APP_BOARD_METRONOME_PWM_CHANNEL TIM_CHANNEL_3
#define APP_BOARD_METRONOME_PWM_TIMER_TICK_HZ 1000000UL
#define APP_BOARD_METRONOME_PWM_TIMER_PRESCALER_DIVISOR 96U
#define APP_BOARD_METRONOME_PWM_DEFAULT_FREQUENCY_HZ 1000U
#define APP_BOARD_METRONOME_PWM_MIN_FREQUENCY_HZ 100U
#define APP_BOARD_METRONOME_PWM_MIN_DURATION_MS 1U

static TIM_HandleTypeDef app_board_tim2;
static TIM_HandleTypeDef app_board_metronome_pwm_timer;
static UART_HandleTypeDef app_board_midi_output_uart;
static uint8_t app_board_metronome_pwm_initialized = 0U;
static volatile uint8_t app_board_metronome_pwm_running = 0U;
static volatile uint8_t app_board_metronome_pwm_stop_armed = 0U;
static volatile uint32_t app_board_metronome_pwm_stop_tick = 0U;

static void AppBoard_InitMidiOutputUart(void);
static void AppBoard_InitMetronomePwm(void);
static void AppBoard_InitTimingCounter(void);
static uint32_t AppBoard_MetronomePwmPeriodCounts(uint16_t frequency_hz);
static uint32_t AppBoard_MetronomePwmPulseCounts(uint32_t period_counts, uint8_t volume);
static uint32_t AppBoard_MetronomePwmDurationMs(uint32_t duration_us);
__attribute__((section(".RamFunc")))
static void AppBoard_MetronomePwmStopImmediate(void);

void AppBoard_InitStartupPeripherals(void)
{
    AppBoard_InitTimingCounter();
    AppBoard_InitMetronomePwm();
    MidiInitInput();
    AppBoard_InitMidiOutputUart();
    MidiSetOutputUart(&app_board_midi_output_uart);
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

    HAL_TIM_Base_Start(&app_board_tim2);
}

uint8_t AppBoard_MetronomePwmIsAvailable(void)
{
    return app_board_metronome_pwm_initialized;
}

uint8_t AppBoard_MetronomePwmStart(uint16_t frequency_hz, uint8_t volume, uint32_t duration_us)
{
    uint32_t primask;
    uint32_t period_counts;
    uint32_t pulse_counts;
    uint32_t duration_ms;
    uint32_t start_tick;

    if (!app_board_metronome_pwm_initialized || volume == 0U)
        return 0U;

    period_counts = AppBoard_MetronomePwmPeriodCounts(frequency_hz);
    pulse_counts = AppBoard_MetronomePwmPulseCounts(period_counts, volume);
    if (pulse_counts == 0U)
        return 0U;

    duration_ms = AppBoard_MetronomePwmDurationMs(duration_us);
    start_tick = uwTick;

    primask = __get_PRIMASK();
    __disable_irq();

    AppBoard_MetronomePwmStopImmediate();

    app_board_metronome_pwm_timer.Instance->ARR = period_counts - 1U;
    app_board_metronome_pwm_timer.Instance->CCR3 = pulse_counts;
    app_board_metronome_pwm_timer.Instance->CNT = 0U;
    app_board_metronome_pwm_timer.Instance->EGR = TIM_EGR_UG;
    app_board_metronome_pwm_timer.Instance->CCER |= TIM_CCER_CC3E;
    app_board_metronome_pwm_timer.Instance->CR1 |= TIM_CR1_CEN;

    app_board_metronome_pwm_running = 1U;
    app_board_metronome_pwm_stop_tick = start_tick + duration_ms;
    app_board_metronome_pwm_stop_armed = 1U;

    if (primask == 0U)
        __enable_irq();

    return 1U;
}

void AppBoard_MetronomePwmStop(void)
{
    uint32_t primask;

    if (!app_board_metronome_pwm_initialized)
        return;

    primask = __get_PRIMASK();
    __disable_irq();
    AppBoard_MetronomePwmStopImmediate();
    if (primask == 0U)
        __enable_irq();
}

__attribute__((section(".RamFunc")))
void AppBoard_MetronomePwmHandleSysTickIrq(void)
{
    if (!app_board_metronome_pwm_running
     || !app_board_metronome_pwm_stop_armed)
        return;

    if ((int32_t)(uwTick - app_board_metronome_pwm_stop_tick) < 0)
        return;

    AppBoard_MetronomePwmStopImmediate();
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

static uint32_t AppBoard_MetronomePwmDurationMs(uint32_t duration_us)
{
    uint32_t duration_ms = (duration_us + 999U) / 1000U;

    return (duration_ms >= APP_BOARD_METRONOME_PWM_MIN_DURATION_MS)
        ? duration_ms
        : APP_BOARD_METRONOME_PWM_MIN_DURATION_MS;
}

__attribute__((section(".RamFunc")))
static void AppBoard_MetronomePwmStopImmediate(void)
{
    if (!app_board_metronome_pwm_initialized)
        return;

    app_board_metronome_pwm_timer.Instance->CCER &= ~TIM_CCER_CC3E;
    app_board_metronome_pwm_timer.Instance->CR1 &= ~TIM_CR1_CEN;
    app_board_metronome_pwm_timer.Instance->CCR3 = 0U;
    app_board_metronome_pwm_timer.Instance->CNT = 0U;
    app_board_metronome_pwm_stop_armed = 0U;
    app_board_metronome_pwm_stop_tick = 0U;
    app_board_metronome_pwm_running = 0U;
}