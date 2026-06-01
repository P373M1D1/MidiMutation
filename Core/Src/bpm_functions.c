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

typedef enum {
    RUNTIME_STATE_FLASH_PHASE_IDLE = 0,
    RUNTIME_STATE_FLASH_PHASE_PREPARE,
    RUNTIME_STATE_FLASH_PHASE_UNLOCK,
    RUNTIME_STATE_FLASH_PHASE_ERASE_START,
    RUNTIME_STATE_FLASH_PHASE_ERASE_WAIT,
    RUNTIME_STATE_FLASH_PHASE_PROGRAM_MAGIC,
    RUNTIME_STATE_FLASH_PHASE_PROGRAM_MAGIC_WAIT,
    RUNTIME_STATE_FLASH_PHASE_PROGRAM_BPM,
    RUNTIME_STATE_FLASH_PHASE_PROGRAM_BPM_WAIT,
    RUNTIME_STATE_FLASH_PHASE_PROGRAM_PRESET,
    RUNTIME_STATE_FLASH_PHASE_PROGRAM_PRESET_WAIT,
    RUNTIME_STATE_FLASH_PHASE_PROGRAM_BANK,
    RUNTIME_STATE_FLASH_PHASE_PROGRAM_BANK_WAIT,
    RUNTIME_STATE_FLASH_PHASE_LOCK,
    RUNTIME_STATE_FLASH_PHASE_VERIFY,
} RuntimeStateFlashPhase_t;

typedef struct {
    volatile RuntimeStateFlashPhase_t phase;
    volatile uint8_t busy;
    volatile uint8_t last_success;
    volatile RuntimeStateFlashFailReason_t last_fail_reason;
    volatile uint32_t phase_enter_tick_ms;
    volatile uint32_t timeout_failures;
    volatile uint32_t verify_failures;
    FlashState_t snapshot;
} RuntimeStateFlashState_t;

#define RUNTIME_STATE_FLASH_PHASE_TIMEOUT_ERASE_MS 1000U
#define RUNTIME_STATE_FLASH_PHASE_TIMEOUT_PROGRAM_MS 100U
#define RUNTIME_STATE_FLASH_PHASE_TIMEOUT_VERIFY_MS 20U

static RuntimeStateFlashState_t runtime_state_flash = {
    RUNTIME_STATE_FLASH_PHASE_IDLE,
    0U,
    1U,
    RUNTIME_STATE_FLASH_FAIL_NONE,
    0U,
    0U,
    0U,
    {0U, 0U, 0U, 0U}
};

#if BPM_FLASH_WRITES_ENABLED
static void RuntimeState_Flash_SetPhase(RuntimeStateFlashPhase_t phase);
static uint8_t RuntimeState_Flash_PhaseTimedOut(uint32_t timeout_ms);
static void RuntimeState_Flash_MarkFailed(RuntimeStateFlashFailReason_t reason);
static void RuntimeState_Flash_MarkComplete(void);

static void RuntimeState_Flash_SetPhase(RuntimeStateFlashPhase_t phase)
{
    runtime_state_flash.phase = phase;
    runtime_state_flash.phase_enter_tick_ms = HAL_GetTick();
}

static uint8_t RuntimeState_Flash_PhaseTimedOut(uint32_t timeout_ms)
{
    return ((HAL_GetTick() - runtime_state_flash.phase_enter_tick_ms) >= timeout_ms) ? 1U : 0U;
}

static void RuntimeState_Flash_MarkFailed(RuntimeStateFlashFailReason_t reason)
{
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->CR |= FLASH_CR_LOCK;
    runtime_state_flash.busy = 0U;
    runtime_state_flash.last_success = 0U;
    runtime_state_flash.last_fail_reason = reason;
    runtime_state_flash.phase = RUNTIME_STATE_FLASH_PHASE_IDLE;
    runtime_state_flash.phase_enter_tick_ms = HAL_GetTick();

    if (reason == RUNTIME_STATE_FLASH_FAIL_TIMEOUT && runtime_state_flash.timeout_failures < UINT32_MAX)
        runtime_state_flash.timeout_failures++;
    if (reason == RUNTIME_STATE_FLASH_FAIL_VERIFY_MISMATCH && runtime_state_flash.verify_failures < UINT32_MAX)
        runtime_state_flash.verify_failures++;
}

static void RuntimeState_Flash_MarkComplete(void)
{
    runtime_state_flash.busy = 0U;
    runtime_state_flash.last_success = 1U;
    runtime_state_flash.last_fail_reason = RUNTIME_STATE_FLASH_FAIL_NONE;
    runtime_state_flash.phase = RUNTIME_STATE_FLASH_PHASE_IDLE;
    runtime_state_flash.phase_enter_tick_ms = HAL_GetTick();
}
#endif

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
 * RuntimeState_Flash_Save uses a deferred phase machine so each service pass
 * executes bounded work. This avoids a monolithic erase/program transaction in
 * one foreground call and keeps ownership of save cadence in AppSaveService.
 */
void RuntimeState_Flash_Save(uint16_t bpm, uint8_t preset_idx, uint8_t bank_idx)
{
    if (runtime_state_flash.busy)
        return;

    runtime_state_flash.snapshot.magic = FLASH_STATE_MAGIC_V2;
    runtime_state_flash.snapshot.bpm = (uint32_t)bpm;
    runtime_state_flash.snapshot.preset_idx = (uint32_t)preset_idx;
    runtime_state_flash.snapshot.bank_idx = (uint32_t)bank_idx;
    runtime_state_flash.last_success = 0U;
    runtime_state_flash.last_fail_reason = RUNTIME_STATE_FLASH_FAIL_NONE;
    runtime_state_flash.phase_enter_tick_ms = HAL_GetTick();

#if !BPM_FLASH_WRITES_ENABLED
    runtime_state_flash.busy = 0U;
    runtime_state_flash.last_success = 1U;
    runtime_state_flash.phase = RUNTIME_STATE_FLASH_PHASE_IDLE;
    return;
#else
    runtime_state_flash.busy = 1U;
    RuntimeState_Flash_SetPhase(RUNTIME_STATE_FLASH_PHASE_PREPARE);
#endif
}

__attribute__((section(".RamFunc")))
void RuntimeState_Flash_SaveService(void)
{
#if !BPM_FLASH_WRITES_ENABLED
    return;
#else
    volatile FlashState_t *stored_state = (volatile FlashState_t *)BPM_FLASH_ADDR;
    uint32_t error_flags;

    if (!runtime_state_flash.busy)
        return;

    error_flags = FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;

    switch (runtime_state_flash.phase)
    {
    case RUNTIME_STATE_FLASH_PHASE_PREPARE:
        RuntimeState_Flash_SetPhase(RUNTIME_STATE_FLASH_PHASE_UNLOCK);
        return;

    case RUNTIME_STATE_FLASH_PHASE_UNLOCK:
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
        FLASH->SR = error_flags;
        RuntimeState_Flash_SetPhase(RUNTIME_STATE_FLASH_PHASE_ERASE_START);
        return;

    case RUNTIME_STATE_FLASH_PHASE_ERASE_START:
        FLASH->CR = FLASH_CR_SER
                  | (BPM_FLASH_SECTOR << FLASH_CR_SNB_Pos)
                  | FLASH_CR_PSIZE_1
                  | FLASH_CR_STRT;
        RuntimeState_Flash_SetPhase(RUNTIME_STATE_FLASH_PHASE_ERASE_WAIT);
        return;

    case RUNTIME_STATE_FLASH_PHASE_ERASE_WAIT:
        if (RuntimeState_Flash_PhaseTimedOut(RUNTIME_STATE_FLASH_PHASE_TIMEOUT_ERASE_MS))
        {
            RuntimeState_Flash_MarkFailed(RUNTIME_STATE_FLASH_FAIL_TIMEOUT);
            return;
        }
        if (FLASH->SR & FLASH_SR_BSY)
            return;
        if (FLASH->SR & error_flags)
        {
            RuntimeState_Flash_MarkFailed(RUNTIME_STATE_FLASH_FAIL_HW_ERROR);
            return;
        }
        FLASH->CR = FLASH_CR_PG | FLASH_CR_PSIZE_1;
        RuntimeState_Flash_SetPhase(RUNTIME_STATE_FLASH_PHASE_PROGRAM_MAGIC);
        return;

    case RUNTIME_STATE_FLASH_PHASE_PROGRAM_MAGIC:
        stored_state->magic = runtime_state_flash.snapshot.magic;
        __DSB();
        RuntimeState_Flash_SetPhase(RUNTIME_STATE_FLASH_PHASE_PROGRAM_MAGIC_WAIT);
        return;

    case RUNTIME_STATE_FLASH_PHASE_PROGRAM_MAGIC_WAIT:
        if (RuntimeState_Flash_PhaseTimedOut(RUNTIME_STATE_FLASH_PHASE_TIMEOUT_PROGRAM_MS))
        {
            RuntimeState_Flash_MarkFailed(RUNTIME_STATE_FLASH_FAIL_TIMEOUT);
            return;
        }
        if (FLASH->SR & FLASH_SR_BSY)
            return;
        if (FLASH->SR & error_flags)
        {
            RuntimeState_Flash_MarkFailed(RUNTIME_STATE_FLASH_FAIL_HW_ERROR);
            return;
        }
        RuntimeState_Flash_SetPhase(RUNTIME_STATE_FLASH_PHASE_PROGRAM_BPM);
        return;

    case RUNTIME_STATE_FLASH_PHASE_PROGRAM_BPM:
        stored_state->bpm = runtime_state_flash.snapshot.bpm;
        __DSB();
        RuntimeState_Flash_SetPhase(RUNTIME_STATE_FLASH_PHASE_PROGRAM_BPM_WAIT);
        return;

    case RUNTIME_STATE_FLASH_PHASE_PROGRAM_BPM_WAIT:
        if (RuntimeState_Flash_PhaseTimedOut(RUNTIME_STATE_FLASH_PHASE_TIMEOUT_PROGRAM_MS))
        {
            RuntimeState_Flash_MarkFailed(RUNTIME_STATE_FLASH_FAIL_TIMEOUT);
            return;
        }
        if (FLASH->SR & FLASH_SR_BSY)
            return;
        if (FLASH->SR & error_flags)
        {
            RuntimeState_Flash_MarkFailed(RUNTIME_STATE_FLASH_FAIL_HW_ERROR);
            return;
        }
        RuntimeState_Flash_SetPhase(RUNTIME_STATE_FLASH_PHASE_PROGRAM_PRESET);
        return;

    case RUNTIME_STATE_FLASH_PHASE_PROGRAM_PRESET:
        stored_state->preset_idx = runtime_state_flash.snapshot.preset_idx;
        __DSB();
        RuntimeState_Flash_SetPhase(RUNTIME_STATE_FLASH_PHASE_PROGRAM_PRESET_WAIT);
        return;

    case RUNTIME_STATE_FLASH_PHASE_PROGRAM_PRESET_WAIT:
        if (RuntimeState_Flash_PhaseTimedOut(RUNTIME_STATE_FLASH_PHASE_TIMEOUT_PROGRAM_MS))
        {
            RuntimeState_Flash_MarkFailed(RUNTIME_STATE_FLASH_FAIL_TIMEOUT);
            return;
        }
        if (FLASH->SR & FLASH_SR_BSY)
            return;
        if (FLASH->SR & error_flags)
        {
            RuntimeState_Flash_MarkFailed(RUNTIME_STATE_FLASH_FAIL_HW_ERROR);
            return;
        }
        RuntimeState_Flash_SetPhase(RUNTIME_STATE_FLASH_PHASE_PROGRAM_BANK);
        return;

    case RUNTIME_STATE_FLASH_PHASE_PROGRAM_BANK:
        stored_state->bank_idx = runtime_state_flash.snapshot.bank_idx;
        __DSB();
        RuntimeState_Flash_SetPhase(RUNTIME_STATE_FLASH_PHASE_PROGRAM_BANK_WAIT);
        return;

    case RUNTIME_STATE_FLASH_PHASE_PROGRAM_BANK_WAIT:
        if (RuntimeState_Flash_PhaseTimedOut(RUNTIME_STATE_FLASH_PHASE_TIMEOUT_PROGRAM_MS))
        {
            RuntimeState_Flash_MarkFailed(RUNTIME_STATE_FLASH_FAIL_TIMEOUT);
            return;
        }
        if (FLASH->SR & FLASH_SR_BSY)
            return;
        if (FLASH->SR & error_flags)
        {
            RuntimeState_Flash_MarkFailed(RUNTIME_STATE_FLASH_FAIL_HW_ERROR);
            return;
        }
        RuntimeState_Flash_SetPhase(RUNTIME_STATE_FLASH_PHASE_LOCK);
        return;

    case RUNTIME_STATE_FLASH_PHASE_LOCK:
        FLASH->CR &= ~FLASH_CR_PG;
        FLASH->CR |= FLASH_CR_LOCK;
        RuntimeState_Flash_SetPhase(RUNTIME_STATE_FLASH_PHASE_VERIFY);
        return;

    case RUNTIME_STATE_FLASH_PHASE_VERIFY:
        if (RuntimeState_Flash_PhaseTimedOut(RUNTIME_STATE_FLASH_PHASE_TIMEOUT_VERIFY_MS))
        {
            RuntimeState_Flash_MarkFailed(RUNTIME_STATE_FLASH_FAIL_TIMEOUT);
            return;
        }
        if (stored_state->magic != runtime_state_flash.snapshot.magic
         || stored_state->bpm != runtime_state_flash.snapshot.bpm
         || stored_state->preset_idx != runtime_state_flash.snapshot.preset_idx
         || stored_state->bank_idx != runtime_state_flash.snapshot.bank_idx)
        {
            RuntimeState_Flash_MarkFailed(RUNTIME_STATE_FLASH_FAIL_VERIFY_MISMATCH);
            return;
        }
        RuntimeState_Flash_MarkComplete();
        return;

    case RUNTIME_STATE_FLASH_PHASE_IDLE:
    default:
        RuntimeState_Flash_MarkFailed(RUNTIME_STATE_FLASH_FAIL_INVALID_PHASE);
        return;
    }
#endif
}

uint8_t RuntimeState_Flash_SaveIsBusy(void)
{
    return runtime_state_flash.busy;
}

uint8_t RuntimeState_Flash_SaveDidSucceed(void)
{
    return runtime_state_flash.last_success;
}

void RuntimeState_Flash_GetDiagnostics(RuntimeStateFlashDiagnostics_t *diagnostics)
{
    if (!diagnostics)
        return;

    diagnostics->busy = runtime_state_flash.busy;
    diagnostics->last_success = runtime_state_flash.last_success;
    diagnostics->phase = (uint8_t)runtime_state_flash.phase;
    diagnostics->last_fail_reason = runtime_state_flash.last_fail_reason;
    diagnostics->phase_age_ms = HAL_GetTick() - runtime_state_flash.phase_enter_tick_ms;
    diagnostics->timeout_failures = runtime_state_flash.timeout_failures;
    diagnostics->verify_failures = runtime_state_flash.verify_failures;
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
