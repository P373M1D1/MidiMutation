#include "display_functions.h"
#include "display/display_internal.h"
#include "display/display_value_helpers.h"
#include "runtime_config.h"
#include "st7796.h"
#include "stm32f4xx_hal.h"

uint16_t Display_GetBrightnessFromUiValue(uint8_t ui_value)
{
    uint32_t brightness_span = (uint32_t)RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MAX - (uint32_t)RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MIN;
    uint32_t ui_scale = (uint32_t)RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_UI_MAX;
    uint32_t curve_denominator = ui_scale * ui_scale * ui_scale;
    uint16_t mapped_brightness = RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MIN;

    if (ui_value >= RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_UI_MAX)
        return RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MAX;

    /* Keep the cubic taper, but force each UI step to land on a strictly
     * higher raw DAC value so round-tripping through the inverse mapping does
     * not collapse adjacent low-end UI values onto the same stored value. */
    for (uint32_t step = 1U; step <= (uint32_t)ui_value; ++step)
    {
        uint32_t curved_numerator = step * step * step;
        uint32_t ideal_brightness = (uint32_t)RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MIN
                                  + (((curved_numerator * brightness_span)
                                    + (curve_denominator / 2UL))
                                   / curve_denominator);

        if (ideal_brightness <= (uint32_t)mapped_brightness)
            ideal_brightness = (uint32_t)mapped_brightness + 1U;
        if (ideal_brightness > (uint32_t)RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MAX)
            ideal_brightness = (uint32_t)RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MAX;

        mapped_brightness = (uint16_t)ideal_brightness;
    }

    return mapped_brightness;
}

uint8_t Display_GetGlobalBrightnessUiValue(uint16_t brightness)
{
    uint8_t low = 0U;
    uint8_t high = RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_UI_MAX;
    uint8_t candidate;
    uint16_t candidate_brightness;
    uint8_t previous;
    uint16_t previous_brightness;

    if (brightness <= RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MIN)
        return 0U;
    if (brightness >= RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MAX)
        return RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_UI_MAX;

    while (low < high)
    {
        uint8_t mid = (uint8_t)(low + ((high - low) / 2U));
        uint16_t mapped = Display_GetBrightnessFromUiValue(mid);

        if (mapped < brightness)
            low = (uint8_t)(mid + 1U);
        else
            high = mid;
    }

    candidate = low;
    if (candidate == 0U)
        return 0U;

    candidate_brightness = Display_GetBrightnessFromUiValue(candidate);
    previous = (uint8_t)(candidate - 1U);
    previous_brightness = Display_GetBrightnessFromUiValue(previous);

    return (((uint32_t)candidate_brightness - (uint32_t)brightness)
            < ((uint32_t)brightness - (uint32_t)previous_brightness))
        ? candidate
        : previous;
}

void Display_ApplyConfiguredBacklightBrightnessNow(void)
{
    if (Display_ScreensaverIsActive())
        return;

    DAC->DHR12R1 = Display_BacklightGetConfiguredBrightness();
}

void Display_ForceFullDisplayRedraw(void)
{
    ST7796_FillScreen(Display_GetBackgroundColour());
    display_state.menu_draw_state_valid = 0U;
    display_state.menu_last_drawn_page = DISPLAY_MENU_PAGE_ROOT;
    display_state.main_layout_dirty = 1U;
    display_state.bpm_display_valid = 0U;

    if (display_state.menu_mode_active)
        Display_MenuRefresh();
}