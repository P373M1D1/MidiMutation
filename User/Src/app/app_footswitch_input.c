#include "app/app_footswitch_input.h"

#include "main.h"

#include "app_event.h"
#include "stm32f4xx_hal.h"

#define APP_FOOTSWITCH_INPUT_COUNT 11U
#define APP_FOOTSWITCH_INPUT_MUTE_INDEX 10U
#define APP_FOOTSWITCH_INPUT_RANDOM_INDEX 8U
#define APP_FOOTSWITCH_INPUT_DEBOUNCE_MS 20U

static const uint16_t app_footswitch_input_pins[APP_FOOTSWITCH_INPUT_COUNT] = {
    PRESET_BTN1_Pin,
    PRESET_BTN2_Pin,
    PRESET_BTN3_Pin,
    PRESET_BTN4_Pin,
    PRESET_BTN5_Pin,
    PRESET_BTN6_Pin,
    PRESET_BTN7_Pin,
    PRESET_BTN8_Pin,
    PRESET_BTN9_Pin,
    PRESET_BTN10_Pin,
    PRESET_BTN11_Pin
};

static uint32_t app_footswitch_input_event_tick[APP_FOOTSWITCH_INPUT_COUNT] = {0U};
static uint8_t app_footswitch_input_press_latched[APP_FOOTSWITCH_INPUT_COUNT] = {0U};

static int8_t AppFootswitchInput_TryResolveIndex(uint16_t gpio_pin);

/* Converts a footswitch GPIO edge into a queued button event. */
void AppFootswitchInput_HandleGpioExti(uint16_t gpio_pin)
{
    int8_t index = AppFootswitchInput_TryResolveIndex(gpio_pin);
    uint32_t now;
    uint8_t is_pressed;
    AppEvent_t event;

    if (index < 0)
        return;

    now = HAL_GetTick();
    if ((now - app_footswitch_input_event_tick[(uint8_t)index]) < APP_FOOTSWITCH_INPUT_DEBOUNCE_MS)
        return;

    is_pressed = AppFootswitchInput_ReadPressed((uint8_t)index);

    if (is_pressed)
    {
        if ((uint8_t)index != APP_FOOTSWITCH_INPUT_MUTE_INDEX && app_footswitch_input_press_latched[(uint8_t)index])
            return;

        app_footswitch_input_press_latched[(uint8_t)index] = 1U;
    }
    else if ((uint8_t)index != APP_FOOTSWITCH_INPUT_MUTE_INDEX && !app_footswitch_input_press_latched[(uint8_t)index])
    {
        return;
    }
    else
    {
        app_footswitch_input_press_latched[(uint8_t)index] = 0U;
    }

    app_footswitch_input_event_tick[(uint8_t)index] = now;

    event.type = is_pressed ? APP_EVENT_TYPE_BUTTON_DOWN : APP_EVENT_TYPE_BUTTON_UP;
    event.source = APP_EVENT_SOURCE_FOOTSWITCH((uint8_t)index);
    event.value = (int16_t)is_pressed;
    event.tick = now;
    (void)AppEvent_Push(&event);
}

/* Re-arms latched footswitch presses once the GPIO is released. */
void AppFootswitchInput_ProcessPending(void)
{
    for (uint8_t index = 0U; index < APP_FOOTSWITCH_INPUT_COUNT; ++index)
    {
        if ((index == APP_FOOTSWITCH_INPUT_MUTE_INDEX)
         || !app_footswitch_input_press_latched[index])
        {
            continue;
        }

        if (AppFootswitchInput_ReadPressed(index) == 0U)
            app_footswitch_input_press_latched[index] = 0U;
    }
}

/* Reads the current logical pressed state for one footswitch. */
uint8_t AppFootswitchInput_ReadPressed(uint8_t index)
{
    if (index >= APP_FOOTSWITCH_INPUT_COUNT)
        return 0U;

    return (HAL_GPIO_ReadPin(PRESET_BTN_GPIO_Port, app_footswitch_input_pins[index]) == GPIO_PIN_RESET) ? 1U : 0U;
}

static int8_t AppFootswitchInput_TryResolveIndex(uint16_t gpio_pin)
{
    if (gpio_pin == USER_Btn_Pin)
        return (int8_t)APP_FOOTSWITCH_INPUT_RANDOM_INDEX;

    for (uint8_t index = 0U; index < APP_FOOTSWITCH_INPUT_COUNT; ++index)
    {
        if (app_footswitch_input_pins[index] == gpio_pin)
            return (int8_t)index;
    }

    return -1;
}