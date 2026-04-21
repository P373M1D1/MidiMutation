#ifndef MIDI_FUNCTIONS_H
#define MIDI_FUNCTIONS_H

/*
 * Multi-port MIDI output.  Each device gets its own UART → own optocoupler
 * current loop → no current-sharing problems, fully future-proof.
 *
 * Physical connection per port (5-pin DIN or TRS-A):
 *   UART_TX  → 220 Ω → MIDI OUT pin 5
 *   3.3 V    → 220 Ω → MIDI OUT pin 4
 *   GND               → MIDI OUT pin 2
 *
 * Usage:
 *   MIDI_InitPort(0, UART4, GPIOC, GPIO_PIN_10, GPIO_AF8_UART4);  // Echosystem
 *   MIDI_InitPort(1, UART5, GPIOC, GPIO_PIN_12, GPIO_AF8_UART5);  // Reverb
 *   ...
 *   MIDI_SendCC(0, channel, cc, value);
 */

#include <stdint.h>
#include "presets.h"
#include "midi_devices.h"
#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of independent MIDI output ports. */
#define MIDI_PORT_COUNT  8U

/**
 * @brief  Initialise one MIDI output port.
 *
 * @param  port       Port index (0 … MIDI_PORT_COUNT-1).
 * @param  uart       UART peripheral instance, e.g. UART4.
 * @param  gpio_port  GPIO port for the TX pin, e.g. GPIOC.
 * @param  pin        GPIO pin mask, e.g. GPIO_PIN_10.
 * @param  af         Alternate-function number, e.g. GPIO_AF8_UART4.
 *
 * Call once per port during startup, before any Send function.
 */
void MIDI_InitPort(uint8_t port, USART_TypeDef *uart,
                   GPIO_TypeDef *gpio_port, uint16_t pin, uint8_t af);

/**
 * @brief  Send a Program Change message on the given port.
 * @param  port     Port index initialised with MIDI_InitPort().
 * @param  channel  MIDI channel, 1–16.
 * @param  program  Program number, 0–127.
 */
void MIDI_SendProgramChange(uint8_t port, uint8_t channel, uint8_t program);

/**
 * @brief  Send a Control Change (CC) message on the given port.
 * @param  port       Port index initialised with MIDI_InitPort().
 * @param  channel    MIDI channel, 1–16.
 * @param  cc_number  Controller number, 0–127.
 * @param  value      Controller value, 0–127.
 */
void MIDI_SendCC(uint8_t port, uint8_t channel, uint8_t cc_number, uint8_t value);

/**
 * @brief  Send Program Changes for all devices in a preset.
 *         Skips any device slot where program == 0xFF.
 */
void Midi_LoadPreset(const Preset_t *preset);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_FUNCTIONS_H */
