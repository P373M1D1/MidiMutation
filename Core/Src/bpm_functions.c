#include "bpm_functions.h"
#include "app/app_state.h"
#include "app_event.h"
#include "display_functions.h"
#include "led_functions.h"
#include "midi_functions.h"
#include "presets.h"
#include "stm32f4xx_hal.h"

#define FLASH_STATE_MAGIC_V1 0x50424D31UL /* 'PBM1' */
#define FLASH_STATE_MAGIC_V2 0x50424D32UL /* 'PBM2' */

typedef struct {
    uint32_t magic;
    uint32_t bpm;
    uint32_t preset_idx;
    uint32_t bank_idx;
} FlashState_t;

static uint8_t bpm_value_is_valid(uint32_t bpm)
{
    return (bpm >= BPM_MIN && bpm <= BPM_MAX) ? 1U : 0U;
}

static uint8_t flash_state_read(FlashState_t *state)
{
    const volatile FlashState_t *stored_state = (const volatile FlashState_t *)BPM_FLASH_ADDR;

    state->magic      = stored_state->magic;
    state->bpm        = stored_state->bpm;
    state->preset_idx = stored_state->preset_idx;
    state->bank_idx   = stored_state->bank_idx;

    if (state->magic == FLASH_STATE_MAGIC_V2)
    {
        return (bpm_value_is_valid(state->bpm)
             && state->preset_idx < PRESET_COUNT
             && state->bank_idx < PRESET_BANK_COUNT) ? 1U : 0U;
    }

    if (state->magic == FLASH_STATE_MAGIC_V1)
    {
        if (bpm_value_is_valid(state->bpm) && state->preset_idx < PRESET_COUNT) {
            state->bank_idx = state->preset_idx / PRESETS_PER_BANK;
            return 1U;
        }

        return 0U;
    }

    /* Backward compatibility with the previous single-word BPM format. */
    if (bpm_value_is_valid(state->magic))
    {
        state->bpm        = state->magic;
        state->preset_idx = PRESET_DEFAULT;
        state->bank_idx   = PRESET_DEFAULT / PRESETS_PER_BANK;
        return 1U;
    }

    return 0U;
}

/*
 * RuntimeState_Flash_Save  –  placed in .RamFunc so it executes from SRAM.
 *
 * STM32F4 has a single-bank Flash: while any erase or program is in
 * progress ALL Flash reads stall, including instruction fetches.
 * Running from RAM lets the CPU keep executing throughout.
 *
 * Interrupts are left ENABLED.  SysTick_Handler and HAL_IncTick also run
 * from RAM (stm32f4xx_it.c) so the tick counter stays accurate.  Other ISRs
 * whose handlers are in Flash will stall until the current operation
 * completes, but that stall is bounded (~200 µs per word program; the sector
 * erase is the long pole) and safe — the hardware serialises flash accesses.
 *
 * Direct register access is used deliberately — calling HAL functions
 * would require fetching their code from Flash, defeating the purpose.
 */
__attribute__((noinline, section(".RamFunc")))
/**
 * Stores the runtime BPM, preset, and bank selection to flash.
 */
void RuntimeState_Flash_Save(uint16_t bpm, uint8_t preset_idx, uint8_t bank_idx)
{
#if !BPM_FLASH_WRITES_ENABLED
    (void)bpm;
    (void)preset_idx;
    (void)bank_idx;
    return;
#else
    volatile FlashState_t *stored_state = (volatile FlashState_t *)BPM_FLASH_ADDR;

    /* Unlock Flash control register */
    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;

    /* Clear any sticky error flags */
    FLASH->SR = FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;

    /* Erase sector 11  (PSIZE=10b → 32-bit width, suits 2.7–3.6 V supply) */
    FLASH->CR = FLASH_CR_SER
              | (BPM_FLASH_SECTOR << FLASH_CR_SNB_Pos)
              | FLASH_CR_PSIZE_1   /* bit 1 of PSIZE field → 32-bit */
              | FLASH_CR_STRT;
    while (FLASH->SR & FLASH_SR_BSY) {}

    /* Program state words */
    FLASH->CR = FLASH_CR_PG | FLASH_CR_PSIZE_1;
    stored_state->magic = FLASH_STATE_MAGIC_V2;
    __DSB();                         /* ensure write reaches Flash controller */
    while (FLASH->SR & FLASH_SR_BSY) {}

    stored_state->bpm = (uint32_t)bpm;
    __DSB();
    while (FLASH->SR & FLASH_SR_BSY) {}

    stored_state->preset_idx = (uint32_t)preset_idx;
    __DSB();
    while (FLASH->SR & FLASH_SR_BSY) {}

    stored_state->bank_idx = (uint32_t)bank_idx;
    __DSB();
    while (FLASH->SR & FLASH_SR_BSY) {}

    FLASH->CR &= ~FLASH_CR_PG;

    /* Lock */
    FLASH->CR |= FLASH_CR_LOCK;
#endif
}

/**
 * Loads the persisted BPM value from flash.
 */
uint16_t BPM_Flash_Load(void)
{
    FlashState_t state;
    if (flash_state_read(&state))
        return (uint16_t)state.bpm;
    return BPM_DEFAULT;  /* blank Flash reads 0xFFFFFFFF */
}

/**
 * Loads the persisted preset index from flash.
 */
uint8_t BPM_Flash_LoadPresetIndex(void)
{
    FlashState_t state;
    if (flash_state_read(&state))
        return (uint8_t)state.preset_idx;
    return PRESET_DEFAULT;
}

/**
 * Loads the persisted bank index from flash.
 */
uint8_t BPM_Flash_LoadBankIndex(void)
{
    FlashState_t state;
    if (flash_state_read(&state))
        return (uint8_t)state.bank_idx;
    return (PRESET_DEFAULT / PRESETS_PER_BANK);
}

/**
 * Returns true when the persisted flash state is valid.
 */
uint8_t BPM_Flash_IsValid(void)
{
    FlashState_t state;
    return flash_state_read(&state);
}

/* -------------------------------------------------------------------------- */

#if BPM_FLASH_WRITES_ENABLED
static uint8_t BPM_QueueRuntimeStateSaveRequest(void)
{
    AppEvent_t event;

    event.type = APP_EVENT_TYPE_SAVE_REQUEST;
    event.source = APP_EVENT_SAVE_KIND_RUNTIME_STATE;
    event.value = 0;
    event.tick = HAL_GetTick();
    return AppEvent_Push(&event);
}
#endif

/* -------------------------------------------------------------------------- */

/**
 * Schedules a deferred runtime-state save once the deadline expires.
 */
void BPM_Service(void)
{
    uint32_t save_tick = AppState_GetRuntimeStateSaveTick();

    if (save_tick && HAL_GetTick() >= save_tick)
    {
#if BPM_FLASH_WRITES_ENABLED
        if (BPM_QueueRuntimeStateSaveRequest())
            AppState_ClearRuntimeStateSaveSchedule();
#else
        AppState_ClearRuntimeStateSaveSchedule();
#endif
    }
}
