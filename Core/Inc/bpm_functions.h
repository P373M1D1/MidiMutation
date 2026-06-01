#ifndef BPM_FUNCTIONS_H
#define BPM_FUNCTIONS_H /* include guard for BPM/persistence declarations */

#include <stdint.h>

#define BPM_FLASH_ADDR    0x080E0000UL  /* flash address where persisted BPM/bank/preset state starts */
#define BPM_FLASH_SECTOR  FLASH_SECTOR_11 /* STM32 flash sector used for persisted BPM/bank/preset state */
#define BPM_SAVE_DELAY_MS 2000U         /* save 2 s after last tap       */
#define BPM_FLASH_WRITES_ENABLED 0U     /* disable runtime flash persistence of BPM/bank/preset */
#define BPM_MIN           20U           /* lowest accepted BPM value for internal or restored tempo */
#define BPM_MAX           240U          /* highest accepted BPM value for internal or restored tempo */
#define BPM_DEFAULT       120U          /* used when Flash is blank      */
#define PRESET_DEFAULT    0U            /* preset index used when no valid preset was restored from flash */

typedef enum {
	RUNTIME_STATE_FLASH_FAIL_NONE = 0,
	RUNTIME_STATE_FLASH_FAIL_TIMEOUT,
	RUNTIME_STATE_FLASH_FAIL_HW_ERROR,
	RUNTIME_STATE_FLASH_FAIL_VERIFY_MISMATCH,
	RUNTIME_STATE_FLASH_FAIL_INVALID_PHASE,
} RuntimeStateFlashFailReason_t;

typedef struct {
	uint8_t busy;
	uint8_t last_success;
	uint8_t phase;
	RuntimeStateFlashFailReason_t last_fail_reason;
	uint32_t phase_age_ms;
	uint32_t timeout_failures;
	uint32_t verify_failures;
} RuntimeStateFlashDiagnostics_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Request asynchronous persistence of BPM + preset index + bank index.
 * @param  bpm          BPM value to persist (expected range BPM_MIN..BPM_MAX).
 * @param  preset_idx   Preset index to persist.
 * @param  bank_idx     Bank index to persist.
 */
void    RuntimeState_Flash_Save(uint16_t bpm, uint8_t preset_idx, uint8_t bank_idx);

/**
 * @brief  Advance one step of the deferred flash-save state machine.
 */
void RuntimeState_Flash_SaveService(void);

/**
 * @brief  Return true while a deferred runtime-state flash save is in flight.
 */
uint8_t RuntimeState_Flash_SaveIsBusy(void);

/**
 * @brief  Return 1 when the most recently completed save finished successfully.
 */
uint8_t RuntimeState_Flash_SaveDidSucceed(void);

/**
 * @brief  Snapshot runtime-state flash-save diagnostics.
 */
void RuntimeState_Flash_GetDiagnostics(RuntimeStateFlashDiagnostics_t *diagnostics);

/**
 * @brief  Read BPM from Flash sector 11.
 * @return Stored value if valid (BPM_MIN..BPM_MAX), BPM_DEFAULT otherwise.
 */
uint16_t BPM_Flash_Load(void);

/**
 * @brief  Read preset index from Flash sector 11.
 * @return Stored index if valid, PRESET_DEFAULT otherwise.
 */
uint8_t  BPM_Flash_LoadPresetIndex(void);

/**
 * @brief  Read bank index from Flash sector 11.
 * @return Stored bank index if valid, or the bank implied by PRESET_DEFAULT.
 */
uint8_t  BPM_Flash_LoadBankIndex(void);

/**
 * @brief  Check whether Flash sector 11 contains a valid BPM value.
 * @return 1 if a valid value is stored, 0 if blank or out of range.
 */
uint8_t  BPM_Flash_IsValid(void);

/**
 * @brief  Process BPM dirty-state and deferred runtime-state save scheduling.
 *         Call from the main while(1) loop.
 */
void BPM_Service(void);

#ifdef __cplusplus
}
#endif

#endif /* BPM_FUNCTIONS_H */
