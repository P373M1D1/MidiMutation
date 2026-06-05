# NUCLEO-F413ZH — Pin Assignment Reference

Board: STM32 Nucleo-144 with STM32F413ZH (LQFP144)
Reference: https://os.mbed.com/platforms/ST-Nucleo-F413ZH/

| Nucleo Pin  | Function                                              | Connected To                  |
|-------------|-------------------------------------------------------|-------------------------------|
| **Buttons & LEDs**                                                                            |
| PC13        | User button input (unused — tap tempo moved to PG15)  | Blue pushbutton (on-board)    |
| PB0         | LD1 — green LED output                                | Green LED (on-board) / Beat indicator |
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
| **SPI1 — ST7796 Display + BUILTIN SD (TFT slot)**                                           |
| PA4         | Backlight Pin for fade                               | ST7796 LED                     |
| PA5         | SPI1_SCK  (AF5) — CN7 / Arduino D13                  | ST7796 SCK                    |
| PA6         | SPI1_MISO (AF5) — CN7 / Arduino D12                  | ST7796 MISO                   |
| PA7         | SPI1_MOSI (AF5) — CN7 / Arduino D11                  | ST7796 MOSI                   |
| PD14        | ST7796_CS  — chip select (output) — Morpho CN11       | ST7796 CS                     |
| PD15        | ST7796_DC  — data/command select (output) — Morpho CN11 | ST7796 DC / RS              |
| PF12        | ST7796_RST — reset (output) — Morpho CN11             | ST7796 RST                    |
| PG2         | BUILTIN_SD_CS — chip select (output)                 | BUILTIN SD CS (TFT slot)      |
| **SPI3 — USER SD Card (new dedicated SD bus)**                                             |
| PC10        | SPI3_SCK  (AF6) — Morpho CN11                          | USER SD SCK                  |
| PC11        | SPI3_MISO (AF6) — Morpho CN11                          | USER SD MISO                 |
| PC12        | SPI3_MOSI (AF6) — Morpho CN11                          | USER SD MOSI                 |
| PG1         | USER_SD_CS — chip select (output)                      | USER SD CS                   |
| **MIDI UART Ports**                                                                           |
| PD0         | UART4_RX (AF11) — Morpho CN11                         | MIDI In 2 monitor input |
| PD1         | UART4_TX (AF11) — Morpho CN11                         | MIDI Out 1 — smart output (preset MIDI + internal/external clock) |
| PC12        | Repurposed from UART5_TX spare route                  | Now used by USER SD SPI3 MOSI |
| PD5         | USART2_TX (AF7) — Morpho CN11                         | MIDI Thru — soft-thru copy of MIDI In |
| PD6         | USART2_RX (AF7) — Morpho CN11                         | MIDI In (opto-isolated input) |
| **Rotary Encoders — practical 3-controller plan**                                             |
| PG11        | GPIO input pull-up, sampled from TIM7 IRQ            | Encoder 1 — A channel (main-info scroll in LIVE, row/cursor navigation in MENU and preset edit) |
| PG12        | GPIO input pull-up, sampled from TIM7 IRQ            | Encoder 1 — B channel (main-info scroll in LIVE, row/cursor navigation in MENU and preset edit) |
| PB4         | GPIO input pull-up, sampled from TIM7 IRQ — Morpho CN10 | Encoder 2 — A channel (preset select within current bank in LIVE) |
| PB5         | GPIO input pull-up, sampled from TIM7 IRQ — Morpho CN10 | Encoder 2 — B channel (preset select within current bank in LIVE) |
| PD12        | GPIO input pull-up, sampled from TIM7 IRQ — Morpho CN11 | Encoder 3 — CLK (A) (tempo in LIVE, value edit in MENU and preset edit) |
| PD13        | GPIO input pull-up, sampled from TIM7 IRQ — Morpho CN11 | Encoder 3 — DT (B) (tempo in LIVE, value edit in MENU and preset edit) |
| **Encoder push buttons**                                                                       |
| PG14        | GPIO input pull-up, EXTI14 (EXTI15_10_IRQn)          | Encoder 1 switch (enter preset edit in LIVE; activate/select in MENU) |
| PD4         | GPIO input pull-up, EXTI4 (EXTI4_IRQn)               | Encoder 2 switch (next bank in LIVE; HOME in MENU; resend/confirm action in preset edit) |
| PD3         | GPIO input pull-up, EXTI3 (EXTI3_IRQn)               | Encoder 3 switch (enter MENU from LIVE; BACK/exit in MENU and preset edit) |
| PD1         | Reassigned to UART4_TX (AF11)                        | Not available for an encoder switch |
| **Pushbutton LEDs — GPIO output**                                                             |
| PF0         | Button 1  LED output                                  | Pushbutton 1 LED              |
| PF1         | Button 2  LED output                                  | Pushbutton 2 LED              |
| PF2         | Button 4  LED output                                  | Pushbutton 4 LED              |  note i fucked up the soldering and confused 4 and 3
| PF3         | Button 3  LED output                                  | Pushbutton 3 LED              |  note i fucked up the soldering and confused 4 and 3
| PF4         | Button 5  LED output                                  | Pushbutton 5 LED              |
| PF5         | Button 6  LED output                                  | Pushbutton 6 LED              |
| PF6         | Button 7  LED output                                  | Pushbutton 7 LED              |
| PF7         | Button 8  LED output                                  | Pushbutton 8 LED              |
| PF8         | Button 9  LED output                                  | Pushbutton 9 LED              |
| PF9         | Button 10 LED output                                  | Pushbutton 10 LED             |
| PF10        | Tap footswitch visual feedback output                 | Tap footswitch press LED      |
| PF11        | Button 11 LED output                                  | Pushbutton 11 LED             |
| PF13        | Button 13 LED output                                  | Pushbutton 13 LED             |
| **Encoder button LEDs — GPIO output**                                                         |
| PF15        | MIDI In activity LED output                           | MIDI In indicator LED         |
| PG0         | MIDI Out activity LED output                          | MIDI Out indicator LED        |
| **Metronome PWM Output**                                                                         |
| PB8         | TIM4_CH3 PWM output (AF2), manually configured in firmware | Metronome click output    |
| **Tap Tempo Input**                                                                           |
| PG15        | Tap tempo footswitch — INPUT_PULLUP, EXTI15 (EXTI15_10_IRQn) | Footswitch to GND      |
| **Pushbutton Inputs (GPIO_INPUT_PULLUP + EXTI interrupt)**                                    |
| PE0         | Button 1  — INPUT_PULLUP, EXTI0  (EXTI0_IRQn)         | Pushbutton 1                  | Preset 1
| PE1         | Button 2  — INPUT_PULLUP, EXTI1  (EXTI1_IRQn)         | Pushbutton 2                  | Preset 2
| PE2         | Button 3  — INPUT_PULLUP, EXTI2  (EXTI2_IRQn)         | Pushbutton 3                  | Preset 3
| PE11        | Button 4  — INPUT_PULLUP, EXTI11 (EXTI15_10_IRQn)     | Pushbutton 4                  | Preset 4
| PE12        | Button 5  — INPUT_PULLUP, EXTI12 (EXTI15_10_IRQn)     | Pushbutton 5                  | Preset 5
| PE5         | Button 6  — INPUT_PULLUP, EXTI5  (EXTI9_5_IRQn)       | Pushbutton 6                  | Preset 6
| PE6         | Button 7  — INPUT_PULLUP, EXTI6  (EXTI9_5_IRQn)       | Pushbutton 7                  | Preset 7
| PE7         | Button 8  — INPUT_PULLUP, EXTI7  (EXTI9_5_IRQn)       | Pushbutton 8                  | Preset 8
| PE8         | Button 9  — INPUT_PULLUP, EXTI8  (EXTI9_5_IRQn)       | Pushbutton 9                  | Random Preset
s| PE9         | Button 10 — INPUT_PULLUP, EXTI9  (EXTI9_5_IRQn)       | Pushbutton 10                 | Special Function
| PE10        | Button 11 — INPUT_PULLUP, EXTI10 (EXTI15_10_IRQn) *   | Pushbutton 11                 | Mute / Bypas

> **Alternate functions sacrificed on PE0–PE10 (not needed for this project):**
> PE0/PE1 → UART8 RX/TX  |  PE7/PE8 → UART7 RX/TX  |  PE3 → UART10 RX
> PE2/PE4/PE5/PE6 → SPI4 SCK/NSS/MISO/MOSI

> **Metronome PWM note:** the live manual route is `PB8 -> TIM4_CH3` in `User/Src/app/app_board_init.c`. Do not reuse `PB8` unless the metronome output is moved.

> **Conflict note:** `PE9` may look tempting as `TIM1_CH1`, but in this repo it is already the Special Function footswitch input (`PRESET_BTN10_Pin`). Do not repurpose `PE9` for the metronome.

> **CubeMX note:** the metronome PWM pin is currently brought up manually in `User/Src/app/app_board_init.c` rather than through the `.ioc` file.

> **Note on STLK pin naming:** `STLK_TX_Pin` (PD8) and `STLK_RX_Pin` (PD9) are labelled from the
> ST-LINK's perspective. On the MCU, PD8 = USART3_TX and PD9 = USART3_RX. Wiring is correct.

> **Note on UART4_TX pin selection:** On the STM32F413ZH package used here, `PD1` is a valid
> `UART4_TX` route and uses `AF11`. `PC10` is a `USART3_TX` pin on this package, not a `UART4_TX`
> pin. The firmware therefore routes smart MIDI out on [Core/Src/main.cpp](Core/Src/main.cpp#L64)
> through `PD1`, while `USART3` remains on `PD8`/`PD9` for the ST-LINK VCP.

> **\* EXTI15_10_IRQn shared ISR (current firmware):** PE10/PE11/PE12 (Buttons 11/4/5), PG14 (Encoder 1 switch), and PG15 (Tap) are serviced in the shared handler.

> **EXTI9_5_IRQn shared ISR:** PE5–PE9 share `EXTI9_5_IRQn`. Same rule — check each pending flag.

> **EXTI4 / EXTI3 ownership:** PD4 now carries Encoder 2 switch on `EXTI4_IRQn`, and PD3 carries Encoder 3 switch on `EXTI3_IRQn`.

> **Encoder push buttons status:** All three encoder switches are configured with pull-ups and routed through interrupts. Their actions are now mode-dependent in firmware: ENC1 enters/activates, ENC2 is bank/HOME/confirm in the general UI but becomes hold-preview in the USER theme editor and clear in MIDI MONITOR, and ENC3 is MENU/BACK or EXIT depending on whether the unit is in LIVE, MENU, preset edit, or MIDI MONITOR.

> **Encoder input mode:** All three encoder A/B pairs are decoded from the shared `TIM7` interrupt sampler using the same transition-accumulator quadrature approach. The numbered preset buttons still use direct GPIO EXTI interrupts; encoder switch presses are edge-latched on EXTI and debounced/queued from the shared sampler path.

---

## Clock Configuration

| Parameter             | Value                            |
|-----------------------|----------------------------------|
| Source                | HSE bypass — 8 MHz from ST-LINK  |
| PLL M / N / P / Q     | 8 / 384 / 4 / 8                  |
| SYSCLK                | 96 MHz                           |
| APB1 timer clock      | 96 MHz (TIM6 — BPM tick)         |

## Timer Overview

These timers are used internally by the firmware. They do not currently consume any external timer-channel pins for PWM, capture, or compare output.

| Timer | Clocking | IRQ | Current use |
|-------|----------|-----|-------------|
| `TIM2` | 96 MHz APB1 timer clock prescaled to 1 MHz (`1 us` per count) | None | Free-running 32-bit microsecond counter used by MIDI clock receive/measure logic in [Core/Src/midi_functions.c](Core/Src/midi_functions.c). This replaces coarse `HAL_GetTick()` timing for external MIDI clock interval measurement and timeout tracking. |
| `TIM4` | 96 MHz APB1 timer clock prescaled to 1 MHz (`1 us` per count) | None | Metronome PWM output on `PB8/TIM4_CH3`. The metronome backend retunes frequency and duty per click and the foreground loop stops each burst after a short click window. |
| `TIM6` | 96 MHz APB1 timer clock prescaled to 100 kHz (`10 us` per count) | `TIM6_DAC_IRQn` | Internal MIDI clock output scheduler. `ARR` is set for one MIDI clock pulse at `24 PPQN`, the ISR emits outgoing MIDI clock bytes, and the beat LED is pulsed once per quarter note. External sync can retune this timer by rewriting `ARR`/`CNT`, but the smart MIDI clock output still runs from the `TIM6` path. |
| `TIM7` | 96 MHz APB1 timer clock prescaled to 1 MHz, period set for `2000 Hz` sampling | `TIM7_IRQn` | Shared encoder sampler. The IRQ polls all three encoder A/B pairs plus the debounced encoder switches, then queues foreground actions for live mode, menu navigation, preset edit, and tempo changes. |

### Notes

- `TIM2` is started as a base timer only; no timer interrupt is enabled for it.
- `TIM6` and `TIM7` are the two active firmware timer interrupts.
- The encoder pushbuttons themselves still use GPIO EXTI lines; `TIM7` is only the shared sampling timebase for the rotary A/B signals and switch debounce.
- `PB8 -> TIM4_CH3` is now reserved for the manual metronome PWM backend; `PE9 -> TIM1_CH1` remains unavailable because `PE9` is already the Special Function footswitch input.


TRS wiring and pin recommendation

Use this default wiring for an expression pedal jack:

Tip: connect to PA0 configured as ADC1_IN0
PA0 is currently free in your .ioc and not assigned in board defines.
Relevant refs:
ChatTest.ioc
main.h
Ring: connect to 3.3V rail
Do not drive ring from a GPIO pin.
Sleeve: connect to GND
Recommended front-end on tip line:

1k series resistor between jack tip and PA0
100nF from PA0 to GND (close to MCU pin)
Optional 100k from PA0 to GND so unplugged jack does not float
Notes:

If pedal direction feels reversed, use Expr Invert in GLOBAL menu.
If your pedal is TRS-wired opposite to this convention, invert will usually fix feel without rewiring.
How to use the new learn flow

Move pedal to heel.
Go to Expr Learn Min and press ENC3.
Move pedal to toe.
Go to Expr Learn Max and press ENC3.
Set Expr Invert if needed.
If you want, next I can add a tiny status text on those learn rows showing the captured raw value so you get immediate feedback after each ENC3 press.