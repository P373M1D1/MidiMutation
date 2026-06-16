#include "display_functions.h"
#include "runtime_config.h"
#include "stm32f4xx_hal.h"

/* DAC backlight control.
 *
 * This module owns the raw PA4/DAC setup plus the blocking fade-in/fade-out
 * ramps used during boot transitions. Keep the fades simple and deterministic
 * here; higher-level policy about when to fade belongs outside this driver. */

#define BL_STEPS      100U
#define BL_SPIN_DELAY 48000U
#define BL_BRIGHTNESS_DEFAULT 2095U

uint16_t Display_BacklightGetConfiguredBrightness(void)
{
    const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();
    uint16_t brightness = BL_BRIGHTNESS_DEFAULT;

    if (global)
        brightness = global->backlight_brightness;

    /* Clamp persisted values so legacy/default/corrupt config never drives the
     * DAC outside the range the UI and hardware path are tuned around. */
    if (brightness < RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MIN)
        brightness = RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MIN;
    else if (brightness > RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MAX)
        brightness = RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MAX;

    return brightness;
}

void Display_BL_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_DAC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin  = GPIO_PIN_4;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    DAC->CR      = DAC_CR_EN1;
    DAC->DHR12R1 = 0U;
}

void Display_BL_FadeIn(void)
{
    uint16_t brightness = Display_BacklightGetConfiguredBrightness();

    /* The fade is intentionally blocking and deterministic; callers use it only
     * for boot transitions where a tiny stall is acceptable. */
    for (uint32_t step = 0U; step <= BL_STEPS; step++)
    {
        DAC->DHR12R1 = (step * brightness) / BL_STEPS;
        for (volatile uint32_t d = 0U; d < BL_SPIN_DELAY; d++) {}
    }
}

void Display_BL_FadeOut(void)
{
    uint16_t brightness = Display_BacklightGetConfiguredBrightness();

    /* Count down explicitly to zero so the panel never retains a dim residual
     * level after a caller requests a full fade-out. */
    for (uint32_t step = BL_STEPS; ; step--)
    {
        DAC->DHR12R1 = (step * brightness) / BL_STEPS;
        for (volatile uint32_t d = 0U; d < BL_SPIN_DELAY; d++) {}
        if (step == 0U)
            break;
    }
}
