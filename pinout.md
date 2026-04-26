# NUCLEO-F413ZH — Pin Assignment Reference

Board: STM32 Nucleo-144 with STM32F413ZH (LQFP144)
Reference: https://os.mbed.com/platforms/ST-Nucleo-F413ZH/

| Nucleo Pin  | Function                                              | Connected To                  |
|-------------|-------------------------------------------------------|-------------------------------|
| **Buttons & LEDs**                                                                            |
| PC13        | User button input (unused — tap tempo moved to PG15)  | Blue pushbutton (on-board)    |
| PB0         | LD1 — green LED output                                | Green LED (on-board)          |
| PB7         | LD2 — blue LED output                                 | Blue LED (on-board)           |
| PB14        | LD3 — red LED output                                  | Red LED (on-board)            |
| **Debugger / ST-LINK**                                                                        |
| PH0         | HSE bypass clock input (MCO)                          | ST-LINK clock output          |
| PD8         | USART3_TX — VCP transmit (AF7)                        | ST-LINK RX                    |
| PD9         | USART3_RX — VCP receive (AF7)                         | ST-LINK TX                    |
| PA13        | TMS — SWD data                                        | ST-LINK debugger              |
| PA14        | TCK — SWD clock                                       | ST-LINK debugger              |
| PB3         | SWO — SWD trace output                                | ST-LINK debugger              |
| **USB FS**                                                                                    |
| PA8         | USB_SOF                                               | USB connector                 |
| PA9         | USB_VBUS                                              | USB connector                 |
| PA10        | USB_ID                                                | USB connector                 |
| PA11        | USB_DM                                                | USB connector                 |
| PA12        | USB_DP                                                | USB connector                 |
| PG6         | USB power switch enable (output)                      | USB power switch IC           |
| PG7         | USB overcurrent sense (input)                         | USB power switch IC           |
| **SPI1 — ST7796 Display**                                                                     |
| PA5         | SPI1_SCK  (AF5) — CN7 / Arduino D13                  | ST7796 SCK                    |
| PA6         | SPI1_MISO (AF5) — CN7 / Arduino D12                  | ST7796 MISO                   |
| PA7         | SPI1_MOSI (AF5) — CN7 / Arduino D11                  | ST7796 MOSI                   |
| PD14        | ST7796_CS  — chip select (output) — Morpho CN11       | ST7796 CS                     |
| PD15        | ST7796_DC  — data/command select (output) — Morpho CN11 | ST7796 DC                   |
| PF12        | ST7796_RST — reset (output) — Morpho CN11             | ST7796 RST                    |
| **MIDI UART Ports**                                                                           |
| PD1         | UART4_TX (AF11) — Morpho CN11                         | MIDI Out 1 — smart output (preset MIDI + internal/external clock) |
| PC12        | UART5_TX (AF8) — Morpho CN11                          | Spare second MIDI out (currently unused in firmware) |
| PD5         | USART2_TX (AF7) — Morpho CN11                         | MIDI Thru — soft-thru copy of MIDI In |
| PD6         | USART2_RX (AF7) — Morpho CN11                         | MIDI In (opto-isolated input) |
| **Rotary Encoders — hardware timer encoder mode (no EXTI used)**                              |
| PA0         | TIM2_CH1 (AF1) — Morpho CN10                          | Encoder 1 — A channel         |
| PA1         | TIM2_CH2 (AF1) — Morpho CN10                          | Encoder 1 — B channel         |
| PB4         | TIM3_CH1 (AF2) — Morpho CN10                          | Encoder 2 — A channel         |
| PB5         | TIM3_CH2 (AF2) — Morpho CN10                          | Encoder 2 — B channel         |
| PD12        | TIM4_CH1 (AF2) — Morpho CN11                          | Encoder 3 — A channel         |
| PD13        | TIM4_CH2 (AF2) — Morpho CN11                          | Encoder 3 — B channel         |
| PC6         | TIM8_CH1 (AF3) — Morpho CN10                          | Encoder 4 — A channel         |
| PC7         | TIM8_CH2 (AF3) — Morpho CN10                          | Encoder 4 — B channel         |
| **Encoder push buttons — INPUT_PULLUP, polled † **                                            |
| PD0         | Encoder 1 push button — INPUT_PULLUP, polled          | Encoder 1 switch              |
| PD1         | Reassigned to UART4_TX (AF11)                         | MIDI Out 1 — smart output     |
| PD3         | Encoder 3 push button — INPUT_PULLUP, polled          | Encoder 3 switch              |
| PD4         | Encoder 4 push button — INPUT_PULLUP, polled          | Encoder 4 switch              |
| **Pushbutton LEDs — GPIO output**                                                             |
| PF0         | Button 1  LED output                                  | Pushbutton 1 LED              |
| PF1         | Button 2  LED output                                  | Pushbutton 2 LED              |
| PF2         | Button 3  LED output                                  | Pushbutton 3 LED              |
| PF3         | Button 4  LED output                                  | Pushbutton 4 LED              |
| PF4         | Button 5  LED output                                  | Pushbutton 5 LED              |
| PF5         | Button 6  LED output                                  | Pushbutton 6 LED              |
| PF6         | Button 7  LED output                                  | Pushbutton 7 LED              |
| PF7         | Button 8  LED output                                  | Pushbutton 8 LED              |
| PF8         | Button 9  LED output                                  | Pushbutton 9 LED              |
| PF9         | Button 10 LED output                                  | Pushbutton 10 LED             |
| PF10        | Button 11 LED output                                  | Pushbutton 11 LED             |
| PF11        | Button 12 LED output                                  | Pushbutton 12 LED             |
| PF13        | Button 13 LED output                                  | Pushbutton 13 LED             |
| **Encoder button LEDs — GPIO output**                                                         |
| PF14        | Tap Tempo LED output                                  | Tap Tempo indicator LED       |
| PF15        | MIDI In activity LED output                           | MIDI In indicator LED         |
| PG0         | MIDI Out activity LED output                          | MIDI Out indicator LED        |
| PG1         | Spare LED output                                      | Spare indicator LED           |
| **Tap Tempo Input**                                                                           |
| PG15        | Tap tempo footswitch — INPUT_PULLUP, EXTI15 (EXTI15_10_IRQn) | Footswitch to GND      |
| **Pushbutton Inputs (GPIO_INPUT_PULLUP + EXTI interrupt)**                                    |
| PE0         | Button 1  — INPUT_PULLUP, EXTI0  (EXTI0_IRQn)         | Pushbutton 1                  | Preset 1
| PE1         | Button 2  — INPUT_PULLUP, EXTI1  (EXTI1_IRQn)         | Pushbutton 2                  | Preset 2
| PE2         | Button 3  — INPUT_PULLUP, EXTI2  (EXTI2_IRQn)         | Pushbutton 3                  | Preset 3
| PE3         | Button 4  — INPUT_PULLUP, EXTI3  (EXTI3_IRQn)         | Pushbutton 4                  | Preset 4
| PE4         | Button 5  — INPUT_PULLUP, EXTI4  (EXTI4_IRQn)         | Pushbutton 5                  | Preset 5
| PE5         | Button 6  — INPUT_PULLUP, EXTI5  (EXTI9_5_IRQn)       | Pushbutton 6                  | Preset 6
| PE6         | Button 7  — INPUT_PULLUP, EXTI6  (EXTI9_5_IRQn)       | Pushbutton 7                  | Preset 7
| PE7         | Button 8  — INPUT_PULLUP, EXTI7  (EXTI9_5_IRQn)       | Pushbutton 8                  | Preset 8
| PE8         | Button 9  — INPUT_PULLUP, EXTI8  (EXTI9_5_IRQn)       | Pushbutton 9                  | Random Preset
| PE9         | Button 10 — INPUT_PULLUP, EXTI9  (EXTI9_5_IRQn)       | Pushbutton 10                 | Special Function
| PE10        | Button 11 — INPUT_PULLUP, EXTI10 (EXTI15_10_IRQn) *   | Pushbutton 11                 | Mute / Bypass
| PE11        | Button 12 — INPUT_PULLUP, EXTI11 (EXTI15_10_IRQn) *   | Pushbutton 12                 |
| PE12        | Button 13 — INPUT_PULLUP, EXTI12 (EXTI15_10_IRQn) *   | Pushbutton 13                 |

> **Alternate functions sacrificed on PE0–PE12 (not needed for this project):**
> PE0/PE1 → UART8 RX/TX  |  PE7/PE8 → UART7 RX/TX  |  PE3 → UART10 RX
> PE2/PE4/PE5/PE6 → SPI4 SCK/NSS/MISO/MOSI  |  PE9/PE11 → TIM1 CH1/CH2 PWM

> **Note on STLK pin naming:** `STLK_TX_Pin` (PD8) and `STLK_RX_Pin` (PD9) are labelled from the
> ST-LINK's perspective. On the MCU, PD8 = USART3_TX and PD9 = USART3_RX. Wiring is correct.

> **Note on UART4_TX pin selection:** On the STM32F413ZH package used here, `PD1` is a valid
> `UART4_TX` route and uses `AF11`. `PC10` is a `USART3_TX` pin on this package, not a `UART4_TX`
> pin. The firmware therefore routes smart MIDI out on [Core/Src/main.cpp](Core/Src/main.cpp#L64)
> through `PD1`, while `USART3` remains on `PD8`/`PD9` for the ST-LINK VCP.

> **\* EXTI15_10_IRQn shared ISR:** PE10, PE11, PE12, and PG15 share `EXTI15_10_IRQn`. The ISR checks `__HAL_GPIO_EXTI_GET_IT()` for each line to identify the source.

> **EXTI9_5_IRQn shared ISR:** PE5–PE9 share `EXTI9_5_IRQn`. Same rule — check each pending flag.

> **† Encoder push buttons polled:** EXTI lines 0–13 are fully consumed by PE0–PE12. EXTI14 remains free. EXTI15 is used by PG15 tap tempo. Encoder buttons are polled in SysTick (1 ms) with software
> debounce — fully sufficient for push-button response times.

> **Encoder timer mode:** TIM2/TIM3/TIM4/TIM8 configured in encoder interface mode. The timer
> counts up/down on quadrature edges automatically — no CPU intervention per step, no EXTI needed.
> Read TIMx->CNT to get position. PF12 (TIM5_CH3 AF2) is reserved for ST7796_RST — not a conflict
> as TIM5 is not used for encoders.

---

## Clock Configuration

| Parameter             | Value                            |
|-----------------------|----------------------------------|
| Source                | HSE bypass — 8 MHz from ST-LINK  |
| PLL M / N / P / Q     | 8 / 384 / 4 / 8                  |
| SYSCLK                | 96 MHz                           |
| APB1 timer clock      | 96 MHz (TIM6 — BPM tick)         |
