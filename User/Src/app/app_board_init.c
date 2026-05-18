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

static TIM_HandleTypeDef app_board_tim2;
static UART_HandleTypeDef app_board_midi_output_uart;

static void AppBoard_InitMidiOutputUart(void);
static void AppBoard_InitTimingCounter(void);

void AppBoard_InitStartupPeripherals(void)
{
    AppBoard_InitTimingCounter();
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