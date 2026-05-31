#include "app/app_startup_clock.h"

#define APP_STARTUP_SYSTEM_CLOCK_PLL_N 384U
#define APP_STARTUP_SYSTEM_CLOCK_PLL_P RCC_PLLP_DIV4
#define APP_STARTUP_SYSTEM_CLOCK_PLL_Q 8U
#define APP_STARTUP_SYSTEM_CLOCK_PLL_R 2U
#define APP_STARTUP_SYSTEM_CLOCK_HSI_PLL_M 16U
#define APP_STARTUP_SYSTEM_CLOCK_HSE_PLL_M 8U

static HAL_StatusTypeDef AppStartupClock_ApplyClockTree(uint32_t sysclk_source);
static HAL_StatusTypeDef AppStartupClock_ConfigureHsiPll(void);
static HAL_StatusTypeDef AppStartupClock_ConfigureHsePll(void);

uint8_t AppStartupClock_HardwareUsesHse(void)
{
    if ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
        return 0U;

    if ((RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC) != RCC_PLLCFGR_PLLSRC_HSE)
        return 0U;

    return ((RCC->CR & RCC_CR_HSERDY) != 0U) ? 1U : 0U;
}

HAL_StatusTypeDef AppStartupClock_PromoteToHse(void)
{
    HAL_StatusTypeDef status;

    if (AppStartupClock_HardwareUsesHse())
        return HAL_OK;

    status = AppStartupClock_ApplyClockTree(RCC_SYSCLKSOURCE_HSI);
    if (status != HAL_OK)
        return status;

    status = AppStartupClock_ConfigureHsePll();
    if (status != HAL_OK)
    {
        (void)AppStartupClock_RestoreHsiPll();
        return status;
    }

    status = AppStartupClock_ApplyClockTree(RCC_SYSCLKSOURCE_PLLCLK);
    if (status != HAL_OK)
    {
        (void)AppStartupClock_ApplyClockTree(RCC_SYSCLKSOURCE_HSI);
        (void)AppStartupClock_RestoreHsiPll();
        return status;
    }

    return HAL_OK;
}

HAL_StatusTypeDef AppStartupClock_RestoreHsiPll(void)
{
    HAL_StatusTypeDef status;

    status = AppStartupClock_ConfigureHsiPll();
    if (status != HAL_OK)
        return status;

    return AppStartupClock_ApplyClockTree(RCC_SYSCLKSOURCE_PLLCLK);
}

static HAL_StatusTypeDef AppStartupClock_ApplyClockTree(uint32_t sysclk_source)
{
    RCC_ClkInitTypeDef clock_init = {0};

    clock_init.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                         | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clock_init.SYSCLKSource = sysclk_source;
    clock_init.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock_init.APB1CLKDivider = RCC_HCLK_DIV2;
    clock_init.APB2CLKDivider = RCC_HCLK_DIV1;

    return HAL_RCC_ClockConfig(&clock_init, FLASH_LATENCY_3);
}

static HAL_StatusTypeDef AppStartupClock_ConfigureHsiPll(void)
{
    RCC_OscInitTypeDef osc_init = {0};

    osc_init.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc_init.HSIState = RCC_HSI_ON;
    osc_init.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc_init.PLL.PLLState = RCC_PLL_ON;
    osc_init.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    osc_init.PLL.PLLM = APP_STARTUP_SYSTEM_CLOCK_HSI_PLL_M;
    osc_init.PLL.PLLN = APP_STARTUP_SYSTEM_CLOCK_PLL_N;
    osc_init.PLL.PLLP = APP_STARTUP_SYSTEM_CLOCK_PLL_P;
    osc_init.PLL.PLLQ = APP_STARTUP_SYSTEM_CLOCK_PLL_Q;
    osc_init.PLL.PLLR = APP_STARTUP_SYSTEM_CLOCK_PLL_R;

    return HAL_RCC_OscConfig(&osc_init);
}

static HAL_StatusTypeDef AppStartupClock_ConfigureHsePll(void)
{
    RCC_OscInitTypeDef osc_init = {0};

    osc_init.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc_init.HSEState = RCC_HSE_BYPASS;
    osc_init.PLL.PLLState = RCC_PLL_ON;
    osc_init.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc_init.PLL.PLLM = APP_STARTUP_SYSTEM_CLOCK_HSE_PLL_M;
    osc_init.PLL.PLLN = APP_STARTUP_SYSTEM_CLOCK_PLL_N;
    osc_init.PLL.PLLP = APP_STARTUP_SYSTEM_CLOCK_PLL_P;
    osc_init.PLL.PLLQ = APP_STARTUP_SYSTEM_CLOCK_PLL_Q;
    osc_init.PLL.PLLR = APP_STARTUP_SYSTEM_CLOCK_PLL_R;

    return HAL_RCC_OscConfig(&osc_init);
}