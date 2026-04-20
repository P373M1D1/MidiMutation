#include "midi_functions.h"

/*
 * Multi-port MIDI TX.
 * APB1 clock = 36 MHz → BRR = 36 000 000 / 31 250 = 1152 (exact, no jitter).
 * Each port has its own UART handle; only TX pin is configured per port.
 */

static UART_HandleTypeDef huart[MIDI_PORT_COUNT];
static uint8_t            port_ready[MIDI_PORT_COUNT]; /* 1 once initialised */

/* --------------------------------------------------------------------------
 * Enable the RCC clock for the given UART instance.
 * -------------------------------------------------------------------------- */
static void uart_clk_enable(USART_TypeDef *uart)
{
    if      (uart == USART1)  { __HAL_RCC_USART1_CLK_ENABLE(); }
    else if (uart == USART2)  { __HAL_RCC_USART2_CLK_ENABLE(); }
    else if (uart == USART3)  { __HAL_RCC_USART3_CLK_ENABLE(); }
    else if (uart == UART4)   { __HAL_RCC_UART4_CLK_ENABLE();  }
    else if (uart == UART5)   { __HAL_RCC_UART5_CLK_ENABLE();  }
    else if (uart == USART6)  { __HAL_RCC_USART6_CLK_ENABLE(); }
#if defined(UART7)
    else if (uart == UART7)   { __HAL_RCC_UART7_CLK_ENABLE();  }
#endif
#if defined(UART8)
    else if (uart == UART8)   { __HAL_RCC_UART8_CLK_ENABLE();  }
#endif
#if defined(UART9)
    else if (uart == UART9)   { __HAL_RCC_UART9_CLK_ENABLE();  }
#endif
#if defined(UART10)
    else if (uart == UART10)  { __HAL_RCC_UART10_CLK_ENABLE(); }
#endif
}

/* Enable the RCC clock for a GPIO port (A–H). */
static void gpio_clk_enable(GPIO_TypeDef *gp)
{
    if      (gp == GPIOA) { __HAL_RCC_GPIOA_CLK_ENABLE(); }
    else if (gp == GPIOB) { __HAL_RCC_GPIOB_CLK_ENABLE(); }
    else if (gp == GPIOC) { __HAL_RCC_GPIOC_CLK_ENABLE(); }
    else if (gp == GPIOD) { __HAL_RCC_GPIOD_CLK_ENABLE(); }
    else if (gp == GPIOE) { __HAL_RCC_GPIOE_CLK_ENABLE(); }
    else if (gp == GPIOF) { __HAL_RCC_GPIOF_CLK_ENABLE(); }
    else if (gp == GPIOG) { __HAL_RCC_GPIOG_CLK_ENABLE(); }
#if defined(GPIOH)
    else if (gp == GPIOH) { __HAL_RCC_GPIOH_CLK_ENABLE(); }
#endif
}

/* -------------------------------------------------------------------------- */

void MIDI_InitPort(uint8_t port, USART_TypeDef *uart,
                   GPIO_TypeDef *gpio_port, uint16_t pin, uint8_t af)
{
    if (port >= MIDI_PORT_COUNT) return;

    gpio_clk_enable(gpio_port);
    uart_clk_enable(uart);

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = pin;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = af;
    HAL_GPIO_Init(gpio_port, &gpio);

    huart[port].Instance          = uart;
    huart[port].Init.BaudRate     = 31250;
    huart[port].Init.WordLength   = UART_WORDLENGTH_8B;
    huart[port].Init.StopBits     = UART_STOPBITS_1;
    huart[port].Init.Parity       = UART_PARITY_NONE;
    huart[port].Init.Mode         = UART_MODE_TX;
    huart[port].Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart[port].Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart[port]);

    port_ready[port] = 1U;
}

/* -------------------------------------------------------------------------- */

void MIDI_SendProgramChange(uint8_t port, uint8_t channel, uint8_t program)
{
    if (port >= MIDI_PORT_COUNT || !port_ready[port]) return;

    uint8_t msg[2] = {
        (uint8_t)(0xC0U | ((channel - 1U) & 0x0FU)),
        (uint8_t)(program & 0x7FU),
    };
    HAL_UART_Transmit(&huart[port], msg, sizeof(msg), 10U);
}

void MIDI_SendCC(uint8_t port, uint8_t channel, uint8_t cc_number, uint8_t value)
{
    if (port >= MIDI_PORT_COUNT || !port_ready[port]) return;

    uint8_t msg[3] = {
        (uint8_t)(0xB0U | ((channel - 1U) & 0x0FU)),
        (uint8_t)(cc_number & 0x7FU),
        (uint8_t)(value & 0x7FU),
    };
    HAL_UART_Transmit(&huart[port], msg, sizeof(msg), 10U);
}
