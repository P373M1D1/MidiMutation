#ifndef LED_FUNCTIONS_H
#define LED_FUNCTIONS_H /* include guard for LED helper declarations */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Turn on the beat LED (LD1, green) for 50 ms.
 *         Call from the TIM6 beat ISR.
 */
void LED_BeatPulse(void);

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

/**
 * @brief  Turn on the dedicated MIDI-in activity LED for 50 ms.
 *         Call on MIDI transport start.
 */
void LED_MidiInPulse(void);

/**
 * @brief  Service LED timeout expirations from a timebase/IRQ context.
 * @param  now  Current HAL tick value in milliseconds.
 */
void LED_TickUpdate(uint32_t now);

/**
 * @brief  Poll both LED timers and turn off when their periods expire.
 *         Call once per main-loop iteration.
 */
void LED_Update(void);

#ifdef __cplusplus
}
#endif

#endif /* LED_FUNCTIONS_H */
