#ifndef LED_FUNCTIONS_H
#define LED_FUNCTIONS_H

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
 *         Call immediately after BPM_Flash_Save().
 */
void LED_FlashPulse(void);

/**
 * @brief  Poll both LED timers and turn off when their periods expire.
 *         Call once per main-loop iteration.
 */
void LED_Update(void);

#ifdef __cplusplus
}
#endif

#endif /* LED_FUNCTIONS_H */
