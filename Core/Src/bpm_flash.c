#include "bpm_flash.h"
#include "stm32f4xx_hal.h"

/*
 * BPM_Flash_Save  –  placed in .RamFunc so it executes from SRAM.
 *
 * STM32F4 has a single-bank Flash: while any erase or program is in
 * progress ALL Flash reads stall, including instruction fetches.
 * Running from RAM lets the CPU keep executing; disabling interrupts
 * for the duration prevents ISR code (in Flash) from stalling mid-erase.
 *
 * Direct register access is used deliberately — calling HAL functions
 * would require fetching their code from Flash, defeating the purpose.
 */
__attribute__((noinline, section(".RamFunc")))
void BPM_Flash_Save(uint16_t bpm)
{
    __disable_irq();

    /* Unlock Flash control register */
    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;

    /* Clear any sticky error flags */
    FLASH->SR = FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;

    /* Erase sector 11  (PSIZE=10b → 32-bit width, suits 2.7–3.6 V supply) */
    FLASH->CR = FLASH_CR_SER
              | (11U << FLASH_CR_SNB_Pos)
              | FLASH_CR_PSIZE_1   /* bit 1 of PSIZE field → 32-bit */
              | FLASH_CR_STRT;
    while (FLASH->SR & FLASH_SR_BSY) {}

    /* Program one 32-bit word */
    FLASH->CR = FLASH_CR_PG | FLASH_CR_PSIZE_1;
    *(volatile uint32_t *)BPM_FLASH_ADDR = (uint32_t)bpm;
    __DSB();                         /* ensure write reaches Flash controller */
    while (FLASH->SR & FLASH_SR_BSY) {}
    FLASH->CR &= ~FLASH_CR_PG;

    /* Lock */
    FLASH->CR |= FLASH_CR_LOCK;

    __enable_irq();
}

uint16_t BPM_Flash_Load(void)
{
    uint32_t val = *(volatile uint32_t *)BPM_FLASH_ADDR;
    if (val >= 20U && val <= 240U)
        return (uint16_t)val;
    return BPM_DEFAULT;  /* blank Flash reads 0xFFFFFFFF */
}

uint8_t BPM_Flash_IsValid(void)
{
    uint32_t val = *(volatile uint32_t *)BPM_FLASH_ADDR;
    return (val >= 20U && val <= 240U) ? 1U : 0U;
}
