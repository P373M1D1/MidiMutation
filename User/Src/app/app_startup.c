#include "app/app_startup.h"

#include "fonts.h"
#include "runtime_config.h"
#include "st7796.h"

#define APP_STARTUP_STATUS_TEXT_X 10U
#define APP_STARTUP_STATUS_TEXT_Y 10U
#define APP_STARTUP_STATUS_FONT Font_7x10
#define APP_STARTUP_STATUS_SUBTEXT_Y (APP_STARTUP_STATUS_TEXT_Y + APP_STARTUP_STATUS_FONT.height + 2U)
#define APP_STARTUP_STATUS_FG_COLOUR CHARCOAL
#define APP_STARTUP_STATUS_BG_COLOUR BLACK
#define APP_STARTUP_STATUS_TEXT_BUFFER_SIZE 16U
#define APP_STARTUP_LOADING_BAR_MS_DEFAULT 1000U

#define APP_STARTUP_SYSTEM_CLOCK_PLL_N 384U
#define APP_STARTUP_SYSTEM_CLOCK_PLL_P RCC_PLLP_DIV4
#define APP_STARTUP_SYSTEM_CLOCK_PLL_Q 8U
#define APP_STARTUP_SYSTEM_CLOCK_PLL_R 2U
#define APP_STARTUP_SYSTEM_CLOCK_HSI_PLL_M 16U
#define APP_STARTUP_SYSTEM_CLOCK_HSE_PLL_M 8U
#define APP_STARTUP_SYSTEM_CLOCK_PROMOTION_RETRY_MS 50U

static uint8_t AppStartup_HardwareUsesHse(void);
static HAL_StatusTypeDef AppStartup_ApplyClockTree(uint32_t sysclk_source);
static HAL_StatusTypeDef AppStartup_ConfigureHsiPll(void);
static HAL_StatusTypeDef AppStartup_ConfigureHsePll(void);
static HAL_StatusTypeDef AppStartup_PromoteToHse(void);

static const char *AppStartup_GetClockSourceStatusText(void)
{
    return AppStartup_HardwareUsesHse() ? "clock source: HSE" : "clock source: HSI";
}

static uint8_t AppStartup_HardwareUsesHse(void)
{
    if ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
        return 0U;

    if ((RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC) != RCC_PLLCFGR_PLLSRC_HSE)
        return 0U;

    return ((RCC->CR & RCC_CR_HSERDY) != 0U) ? 1U : 0U;
}

static HAL_StatusTypeDef AppStartup_ApplyClockTree(uint32_t sysclk_source)
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

static HAL_StatusTypeDef AppStartup_ConfigureHsiPll(void)
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

static HAL_StatusTypeDef AppStartup_ConfigureHsePll(void)
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

static HAL_StatusTypeDef AppStartup_PromoteToHse(void)
{
    HAL_StatusTypeDef status;

    if (AppStartup_HardwareUsesHse())
        return HAL_OK;

    status = AppStartup_ApplyClockTree(RCC_SYSCLKSOURCE_HSI);
    if (status != HAL_OK)
        return status;

    status = AppStartup_ConfigureHsePll();
    if (status != HAL_OK)
    {
        (void)AppStartup_RestoreHsiPll();
        return status;
    }

    status = AppStartup_ApplyClockTree(RCC_SYSCLKSOURCE_PLLCLK);
    if (status != HAL_OK)
    {
        (void)AppStartup_ApplyClockTree(RCC_SYSCLKSOURCE_HSI);
        (void)AppStartup_RestoreHsiPll();
        return status;
    }

    return HAL_OK;
}

uint32_t AppStartup_GetLoadingBarDurationMs(void)
{
    const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();

    if (!global)
        return APP_STARTUP_LOADING_BAR_MS_DEFAULT;

    return (uint32_t)global->startup_delay_seconds * 1000UL;
}

void AppStartup_DrawPersistentStoreStatus(void)
{
    char status_text[APP_STARTUP_STATUS_TEXT_BUFFER_SIZE];

    RuntimeConfig_FormatPersistentStoreStatusText(status_text, sizeof(status_text));
    ST7796_WriteString(APP_STARTUP_STATUS_TEXT_X,
                       APP_STARTUP_STATUS_TEXT_Y,
                       status_text,
                       APP_STARTUP_STATUS_FONT,
                       APP_STARTUP_STATUS_FG_COLOUR,
                       APP_STARTUP_STATUS_BG_COLOUR);
}

void AppStartup_DrawClockSource(void)
{
    ST7796_WriteString(APP_STARTUP_STATUS_TEXT_X,
                       APP_STARTUP_STATUS_SUBTEXT_Y,
                       AppStartup_GetClockSourceStatusText(),
                       APP_STARTUP_STATUS_FONT,
                       APP_STARTUP_STATUS_FG_COLOUR,
                       APP_STARTUP_STATUS_BG_COLOUR);
}

void AppStartup_ServiceClockPromotion(void)
{
    static uint32_t last_promotion_attempt_tick = 0U;
    static uint8_t last_drawn_clock_source = 0xFFU;
    uint8_t hardware_uses_hse = AppStartup_HardwareUsesHse();
    uint32_t now = HAL_GetTick();

    if (last_drawn_clock_source != hardware_uses_hse)
    {
        AppStartup_DrawClockSource();
        last_drawn_clock_source = hardware_uses_hse;
    }

    if (hardware_uses_hse)
        return;

    if ((now - last_promotion_attempt_tick) < APP_STARTUP_SYSTEM_CLOCK_PROMOTION_RETRY_MS)
        return;

    last_promotion_attempt_tick = now;
    (void)AppStartup_PromoteToHse();

    hardware_uses_hse = AppStartup_HardwareUsesHse();
    if (last_drawn_clock_source != hardware_uses_hse)
    {
        AppStartup_DrawClockSource();
        last_drawn_clock_source = hardware_uses_hse;
    }
}

void AppStartup_AttemptClockPromotion(void)
{
    (void)AppStartup_PromoteToHse();
}

HAL_StatusTypeDef AppStartup_RestoreHsiPll(void)
{
    HAL_StatusTypeDef status;

    status = AppStartup_ConfigureHsiPll();
    if (status != HAL_OK)
        return status;

    return AppStartup_ApplyClockTree(RCC_SYSCLKSOURCE_PLLCLK);
}