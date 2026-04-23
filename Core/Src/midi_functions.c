#include "midi_functions.h"
#include "led_functions.h"
#include "main.h"

/* ── midi_functions.c ────────────────────────────────────────────────────────
 *
 * Multi-port MIDI TX driver.
 *
 * Physical ports (initialised in main.cpp, USER CODE BEGIN 2):
 *   Port 0 – UART4, TX = PC10, AF8  → Empress Echosystem (TRS-A jack)
 *   Port 1 – UART5, TX = PC12, AF8  → Empress Reverb     (DIN-5 jack)
 *   Ports 2–7 are blank slots, available for future devices.
 *
 * To add a new device:
 *   1. Call MIDI_InitPort(n, UARTx, GPIOx, PIN, AF) in main.cpp startup.
 *   2. Add a row to the device_table in midi_devices.c.
 *   3. The preset_table in presets.c can then reference slot n.
 *
 * MIDI standard: 31 250 baud, 8 data bits, no parity, 1 stop bit (8-N-1).
 *
 * Clock maths (from main.cpp SystemClock_Config):
 *   HSE  8 MHz (ST-Link oscillator, bypass mode)
 *   PLL: M=8, N=384, P=4  →  SYSCLK = (8 × 384) / (8 × 4) = 96 MHz
 *   APB1 = SYSCLK / 2 = 48 MHz  (UART4 and UART5 both live on APB1)
 *   UART baud divider = 48 000 000 / 31 250 = 1536  (exact integer, zero jitter)
 *
 * Each port has its own UART handle; only the TX pin is configured because
 * MIDI is a one-way (send-only) protocol in this application.
 * ─────────────────────────────────────────────────────────────────────────── */

/* One HAL handle per port; indexed by the port number passed to MIDI_InitPort */
static UART_HandleTypeDef huart[MIDI_PORT_COUNT];
static UART_HandleTypeDef midi_input_uart;

/* Prevents Send functions from running before Init has completed for a port */
static uint8_t            port_ready[MIDI_PORT_COUNT];
static uint8_t            midi_clock_pulse_count = 0U;
static uint32_t           midi_clock_quarter_start_ms = 0U;
static volatile uint8_t   midi_transport_running = 0U;
static volatile MidiTransportEvent_t midi_transport_event = MIDI_TRANSPORT_EVENT_NONE;
static void               midi_clock_reset_sync(void);

/* ── Clock helpers ───────────────────────────────────────────────────────────
 * These two functions are kept here rather than relying on MX_GPIO_Init /
 * MX_UARTx_Init in main.cpp so that MIDI ports can be initialised
 * independently at any point in startup without ordering constraints.
 * ─────────────────────────────────────────────────────────────────────────── */

/* Enable the RCC clock for the given UART peripheral. */
static void uart_clk_enable(USART_TypeDef *uart)
{
    if      (uart == USART1)  { __HAL_RCC_USART1_CLK_ENABLE(); }
    else if (uart == USART2)  { __HAL_RCC_USART2_CLK_ENABLE(); }
    else if (uart == USART3)  { __HAL_RCC_USART3_CLK_ENABLE(); }
    else if (uart == UART4)   { __HAL_RCC_UART4_CLK_ENABLE();  }  /* PC10 – Echosystem */
    else if (uart == UART5)   { __HAL_RCC_UART5_CLK_ENABLE();  }  /* PC12 – Reverb     */
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
    else if (gp == GPIOC) { __HAL_RCC_GPIOC_CLK_ENABLE(); }  /* MIDI TX pins PC10, PC12 */
    else if (gp == GPIOD) { __HAL_RCC_GPIOD_CLK_ENABLE(); }
    else if (gp == GPIOE) { __HAL_RCC_GPIOE_CLK_ENABLE(); }
    else if (gp == GPIOF) { __HAL_RCC_GPIOF_CLK_ENABLE(); }
    else if (gp == GPIOG) { __HAL_RCC_GPIOG_CLK_ENABLE(); }
#if defined(GPIOH)
    else if (gp == GPIOH) { __HAL_RCC_GPIOH_CLK_ENABLE(); }
#endif
}

/* ── MIDI_InitPort ───────────────────────────────────────────────────────────
 * Configures one TX-only UART at MIDI baud rate (31 250).
 * Call once per port during startup (see main.cpp USER CODE BEGIN 2).
 *
 * The GPIO pin is set to Alternate Function push-pull at low speed –
 * low speed is fine; at 31 250 baud the signal edge rate is very gentle.
 * ─────────────────────────────────────────────────────────────────────────── */
void MIDI_InitPort(uint8_t port, USART_TypeDef *uart,
                   GPIO_TypeDef *gpio_port, uint16_t pin, uint8_t af)
{
    if (port >= MIDI_PORT_COUNT) return;

    gpio_clk_enable(gpio_port);
    uart_clk_enable(uart);

    /* Configure TX pin as alternate function, push-pull output */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = pin;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = af;                    /* e.g. GPIO_AF8_UART4 for PC10 */
    HAL_GPIO_Init(gpio_port, &gpio);

    /* 31 250 baud, 8-N-1, TX only – standard MIDI electrical spec */
    huart[port].Instance          = uart;
    huart[port].Init.BaudRate     = 31250;
    huart[port].Init.WordLength   = UART_WORDLENGTH_8B;
    huart[port].Init.StopBits     = UART_STOPBITS_1;
    huart[port].Init.Parity       = UART_PARITY_NONE;
    huart[port].Init.Mode         = UART_MODE_TX;      /* RX not wired/needed */
    huart[port].Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart[port].Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart[port]);

    port_ready[port] = 1U;
}

void MidiInitInput(void)
{
    midi_input_uart.Instance          = USART2;
    midi_input_uart.Init.BaudRate     = 31250;
    midi_input_uart.Init.WordLength   = UART_WORDLENGTH_8B;
    midi_input_uart.Init.StopBits     = UART_STOPBITS_1;
    midi_input_uart.Init.Parity       = UART_PARITY_NONE;
    midi_input_uart.Init.Mode         = UART_MODE_RX;
    midi_input_uart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    midi_input_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&midi_input_uart) != HAL_OK)
    {
        Error_Handler();
    }

    HAL_NVIC_SetPriority(USART2_IRQn, 2U, 1U);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    __HAL_UART_ENABLE_IT(&midi_input_uart, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&midi_input_uart, UART_IT_ERR);
    midi_clock_reset_sync();
}

/* ── MIDI_SendProgramChange ──────────────────────────────────────────────────
 * Sends a 2-byte Program Change message:
 *   Byte 0:  0xC0 | (channel-1)   — status byte, upper nibble 0xC = Program Change
 *   Byte 1:  program & 0x7F       — program number (7-bit, 0–127)
 *
 * The 10 ms timeout is more than enough: at 31 250 baud two bytes take ~640 µs.
 * ─────────────────────────────────────────────────────────────────────────── */
void MIDI_SendProgramChange(uint8_t port, uint8_t channel, uint8_t program)
{
    if (port >= MIDI_PORT_COUNT || !port_ready[port]) return;

    uint8_t msg[2] = {
        (uint8_t)(0xC0U | ((channel - 1U) & 0x0FU)),  /* channel 1-16 → nibble 0-15 */
        (uint8_t)(program & 0x7FU),                    /* mask to 7-bit MIDI data range */
    };
    HAL_UART_Transmit(&huart[port], msg, sizeof(msg), 10U);
}

/* ── MIDI_SendCC ─────────────────────────────────────────────────────────────
 * Sends a 3-byte Control Change message:
 *   Byte 0:  0xB0 | (channel-1)   — status byte, 0xB = Control Change
 *   Byte 1:  cc_number & 0x7F     — which controller (e.g. 60 = engage/bypass)
 *   Byte 2:  value & 0x7F         — controller value  (e.g. 127 = on, 0 = off)
 *
 * See midi_devices.c for the CC numbers used by each pedal.
 * ─────────────────────────────────────────────────────────────────────────── */
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

/* ── Midi_LoadPreset ─────────────────────────────────────────────────────────
 * Iterates all device slots in the preset and sends a Program Change to each
 * device whose slot is not skipped (program != 0xFF), then emits any extra
 * per-preset CC messages whose channel/CC fields are populated.
 * Called by App_ActivatePreset() in presets.c whenever a new preset is loaded.
 * ─────────────────────────────────────────────────────────────────────────── */
static uint8_t Midi_TryResolvePortForChannel(uint8_t channel, uint8_t *port)
{
    if (channel == 0U || !port)
        return 0U;

    for (uint8_t i = 0U; i < MidiDevices_Count(); i++)
    {
        const MidiDevice_t *dev = MidiDevices_Get(i);
        if (dev->channel == channel)
        {
            *port = dev->midi_port;
            return 1U;
        }
    }

    return 0U;
}

static void midi_clock_reset_sync(void)
{
    midi_clock_pulse_count = 0U;
    midi_clock_quarter_start_ms = 0U;
}

void MidiReceive(uint8_t byte)
{
    if (byte == 0xFAU)
    {
        midi_transport_running = 1U;
        midi_transport_event = MIDI_TRANSPORT_EVENT_START;
        LED_MidiInPulse();
        midi_clock_reset_sync();
        return;
    }

    if (byte == 0xFBU)
    {
        midi_transport_running = 1U;
        midi_transport_event = MIDI_TRANSPORT_EVENT_CONTINUE;
        midi_clock_reset_sync();
        return;
    }

    if (byte == 0xFCU)
    {
        midi_transport_running = 0U;
        midi_transport_event = MIDI_TRANSPORT_EVENT_STOP;
        midi_clock_reset_sync();
        return;
    }

    if (byte != 0xF8U)
        return;

    uint32_t now = HAL_GetTick();

    if (midi_clock_pulse_count == 0U)
    {
        midi_clock_quarter_start_ms = now;
    }

    midi_clock_pulse_count++;
    if (midi_clock_pulse_count < 24U)
        return;

    midi_clock_pulse_count = 0U;
    if (midi_clock_quarter_start_ms == 0U)
    {
        midi_clock_quarter_start_ms = now;
        return;
    }

    midi_clock_quarter_start_ms = now;
    LED_MidiClockPulse();
}

uint8_t MidiTransportIsRunning(void)
{
    return midi_transport_running;
}

MidiTransportEvent_t MidiTransportConsumeEvent(void)
{
    MidiTransportEvent_t event = midi_transport_event;
    midi_transport_event = MIDI_TRANSPORT_EVENT_NONE;
    return event;
}

void USART2_IRQHandler(void)
{
    uint32_t status = USART2->SR;

    if (status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE))
    {
        uint8_t byte = (uint8_t)USART2->DR;

        if (status & USART_SR_RXNE)
        {
            MidiReceive(byte);
        }
    }
}

void Midi_LoadPreset(const Preset_t *preset)
{
    if (!preset) return;

    for (uint8_t i = 0U; i < PRESET_DEVICE_SLOTS; i++)
    {
        /* 0xFF in the program field means "don't send anything to this device" */
        if (preset->prg[i].program == 0xFFU) continue;

        const MidiDevice_t *dev = MidiDevices_Get(i);  /* look up port, channel etc. */
        MIDI_SendProgramChange(dev->midi_port, dev->channel, preset->prg[i].program);
    }

    for (uint8_t i = 0U; i < PRESET_CC_SLOT_COUNT; i++)
    {
        const PresetCCSlot_t *cc = &preset->cc[i];
        uint8_t midi_port = 0U;

        if (cc->channel == 0U || cc->cc_number == 0xFFU)
            continue;

        if (!Midi_TryResolvePortForChannel(cc->channel, &midi_port))
            continue;

        MIDI_SendCC(midi_port, cc->channel, cc->cc_number, cc->value);
    }
}
