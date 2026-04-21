#include "button_functions.h"
#include "main.h"
#include "presets.h"
#include "display_functions.h"
#include "stm32f4xx_hal.h"

static uint8_t preset_button_state[4] = {1U, 1U, 1U, 1U};

static const uint16_t preset_button_pins[4] = {
    PRESET_BTN1_Pin,
    PRESET_BTN2_Pin,
    PRESET_BTN3_Pin,
    PRESET_BTN4_Pin
};
static GPIO_TypeDef* const preset_button_ports[4] = {
    PRESET_BTN_GPIO_Port,
    PRESET_BTN_GPIO_Port,
    PRESET_BTN_GPIO_Port,
    PRESET_BTN_GPIO_Port
};

void activatePreset(uint8_t idx) {
    App_ActivatePreset(idx);
}

void Button_CheckAndHandle(void) {
    for (uint8_t i = 0U; i < 4U; ++i) {
        uint8_t is_pressed = (HAL_GPIO_ReadPin(preset_button_ports[i], preset_button_pins[i]) == GPIO_PIN_RESET) ? 1U : 0U;

        if (is_pressed && preset_button_state[i] == 0U) {
            activatePreset(i);
        }

        preset_button_state[i] = is_pressed;
    }
}
