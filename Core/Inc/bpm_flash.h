#ifndef BPM_FLASH_H
#define BPM_FLASH_H

#include <stdint.h>

#define BPM_FLASH_ADDR    0x080E0000UL  /* first word of sector 11       */
#define BPM_FLASH_SECTOR  FLASH_SECTOR_11
#define BPM_SAVE_DELAY_MS 3000U         /* save 3 s after last tap       */
#define BPM_DEFAULT       120U          /* used when Flash is blank      */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Erase Flash sector 11 and write @p bpm as a 32-bit word.
 * @param  bpm  BPM value to persist (expected range 20–240).
 */
void    BPM_Flash_Save(uint16_t bpm);

/**
 * @brief  Read BPM from Flash sector 11.
 * @return Stored value if valid (20–240), BPM_DEFAULT otherwise.
 */
uint16_t BPM_Flash_Load(void);

/**
 * @brief  Check whether Flash sector 11 contains a valid BPM value.
 * @return 1 if a valid value is stored, 0 if blank or out of range.
 */
uint8_t  BPM_Flash_IsValid(void);

#ifdef __cplusplus
}
#endif

#endif /* BPM_FLASH_H */
