#ifndef LED_FUNCTIONS_H
#define LED_FUNCTIONS_H /* include guard for LED helper declarations */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Configure the custom LED output GPIOs owned by the LED subsystem.
 *         This covers the preset indicator bank plus the dedicated tap and
 *         MIDI-in activity LEDs.
 */
void LED_InitBoardOutputs(void);

/**
 * @brief  Turn on the beat LED (LD1, green) for 50 ms.
 *         Call from the TIM6 beat ISR.
 */
void LED_BeatPulse(void);
void LED_BeatPulseAtUs(uint32_t start_us);

/**
 * @brief  Turn on the Flash-write LED (LD2, blue) for 50 ms.
 *         Call immediately after RuntimeState_Flash_Save().
 */
void LED_FlashPulse(void);

/**
 * @brief  Turn on the MIDI-clock LED (LD3, red) for 50 ms.
 *         Call on each received MIDI quarter note.
 */
void LED_MidiClockPulse(void);
void LED_MidiClockPulseAtUs(uint32_t start_us);

/**
 * @brief  Turn on the dedicated tap-press feedback LED for 50 ms.
 *         Call whenever the tap footswitch press is accepted by the ISR path.
 */
void LED_TapPressPulse(void);

/**
 * @brief  Turn on the dedicated MIDI-in activity LED for 50 ms.
 *         Call on MIDI transport start.
 */
void LED_MidiInPulse(void);

/**
 * @brief  Show the active preset on the board LEDs.
 * @param  preset_slot_in_bank 0-based preset slot inside the current bank.
 *         Slot-to-LED mapping cycles across LED1..LED3.
 */
void LED_SetPresetIndicator(uint8_t preset_slot_in_bank);

/**
 * @brief  Show the active non-preset footswitch indicator LED.
 * @param  button_index 0-based footswitch index (0..10).
 */
void LED_SetActiveButtonIndicator(uint8_t button_index);

/**
 * @brief  Toggle the special-function status LED (button 10 / LED10).
 * @param  is_active 1 to illuminate, 0 to turn off.
 */
void LED_SetSpecialFunctionIndicator(uint8_t is_active);

/**
 * @brief  Clear all raw footswitch indicator LEDs used by the wiring monitor.
 */
void LED_ClearButtonMonitorIndicators(void);

/**
 * @brief  Light the one raw footswitch LED associated with the given button.
 * @param  button_index 0-based footswitch index.
 */
void LED_ShowButtonMonitorIndicator(uint8_t button_index);

/**
 * @brief  Return a short label describing the physical LED pin used by the
 *         button/LED monitor for the given footswitch index.
 * @param  button_index 0-based footswitch index.
 */
const char *LED_GetButtonMonitorLabel(uint8_t button_index);

/**
 * @brief  Service LED timeout expirations from a timebase/IRQ context.
 * @param  now  Current HAL tick value in milliseconds.
 */
void LED_TickUpdate(uint32_t now);
void LED_HandleTimingCounterIrq(void);

/**
 * @brief  Return whether any transient visible feedback pulse is still active.
 *         This ignores the steady preset/special-function indicators.
 */
uint8_t LED_IsPulseActive(void);

/**
 * @brief  Poll both LED timers and turn off when their periods expire.
 *         Call once per main-loop iteration.
 */
void LED_Update(void);

#ifdef __cplusplus
}
#endif

#endif /* LED_FUNCTIONS_H */
