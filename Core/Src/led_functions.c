#include "led_functions.h"
#include "display_functions.h"
#include "main.h"          /* LD1_Pin / LD1_GPIO_Port, LD2_Pin / LD2_GPIO_Port */
#include "stm32f4xx_hal.h"

#define LED_PULSE_MS  50U  /* pulse width for all LED blinks */
#define BUTTON_MONITOR_LED_PINS_MASK (PRESET_LED1_Pin | PRESET_LED2_Pin | PRESET_LED3_Pin | PRESET_LED4_Pin \
                                    | PRESET_LED5_Pin | PRESET_LED6_Pin | PRESET_LED7_Pin | PRESET_LED8_Pin \
                                    | PRESET_LED9_Pin | PRESET_LED10_Pin | PRESET_LED11_Pin)

/* Internal tick targets – 0 means LED is already off */
static volatile uint32_t beat_off_tick  = 0U;  /* LD1 green – tap tempo beat */
static volatile uint32_t flash_off_tick = 0U;  /* LD2 blue  – Flash write    */
static volatile uint32_t tap_press_off_tick = 0U; /* PF10     – tap press      */
static volatile uint32_t midi_in_off_tick = 0U; /* PF15      – MIDI in start  */
static uint16_t active_button_led_pin = 0U; /* one active selection LED across preset/random/mute */
static uint8_t special_function_led_active = 0U; /* sticky state for button 10 mode */

static const uint16_t preset_led_pins[8] = {
    PRESET_LED1_Pin,
    PRESET_LED2_Pin,
    PRESET_LED4_Pin, /* wiring compensation: preset 3 drives the pin silked as preset 4 */
    PRESET_LED3_Pin, /* wiring compensation: preset 4 drives the pin silked as preset 3 */
    PRESET_LED5_Pin,
    PRESET_LED6_Pin,
    PRESET_LED7_Pin,
    PRESET_LED8_Pin
};

static const uint16_t button_monitor_led_pins[11] = {
    PRESET_LED1_Pin,
    PRESET_LED2_Pin,
    PRESET_LED3_Pin,
    PRESET_LED4_Pin,
    PRESET_LED5_Pin,
    PRESET_LED6_Pin,
    PRESET_LED7_Pin,
    PRESET_LED8_Pin,
    PRESET_LED11_Pin, /* wiring compensation: random button drives the LED silked as random */
    PRESET_LED9_Pin,  /* wiring compensation: special-function button drives the LED silked as special */
    PRESET_LED10_Pin, /* wiring compensation: mute button drives the LED silked as mute */
};

static const char * const button_monitor_led_labels[11] = {
    "PF0/LED1",
    "PF1/LED2",
    "PF3/LED3",
    "PF2/LED4",
    "PF4/LED5",
    "PF5/LED6",
    "PF6/LED7",
    "PF7/LED8",
    "PF11/LED11",
    "PF8/LED9",
    "PF9/LED10",
};

void LED_ClearButtonMonitorIndicators(void)
{
    HAL_GPIO_WritePin(GPIOF, BUTTON_MONITOR_LED_PINS_MASK, GPIO_PIN_RESET);
}

static void LED_ApplyButtonIndicatorState(void)
{
    uint16_t pin_mask = active_button_led_pin;

    if (special_function_led_active)
        pin_mask |= PRESET_LED9_Pin;

    HAL_GPIO_WritePin(GPIOF, BUTTON_MONITOR_LED_PINS_MASK, GPIO_PIN_RESET);
    if (pin_mask != 0U)
        HAL_GPIO_WritePin(GPIOF, pin_mask, GPIO_PIN_SET);
}

/* -------------------------------------------------------------------------- */

static void LED_UpdateExpiredOutputs(uint32_t now)
{
    if (beat_off_tick && now >= beat_off_tick)
    {
        beat_off_tick = 0U;
        HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_RESET);
    }

    if (flash_off_tick && now >= flash_off_tick)
    {
        flash_off_tick = 0U;
        HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
    }

    if (tap_press_off_tick && now >= tap_press_off_tick)
    {
        tap_press_off_tick = 0U;
        HAL_GPIO_WritePin(TAP_FEEDBACK_LED_GPIO_Port, TAP_FEEDBACK_LED_Pin, GPIO_PIN_RESET);
    }

    if (midi_in_off_tick && now >= midi_in_off_tick)
    {
        midi_in_off_tick = 0U;
        HAL_GPIO_WritePin(MIDI_IN_LED_GPIO_Port, MIDI_IN_LED_Pin, GPIO_PIN_RESET);
    }

}

static uint8_t LED_BeatPulseIsAllowed(void)
{
    return (Display_MenuIsActive() || Display_PresetEditIsActive()) ? 0U : 1U;
}

void LED_BeatPulse(void)
{
    if (!LED_BeatPulseIsAllowed())
    {
        beat_off_tick = 0U;
        HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_RESET);
        return;
    }

    HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_SET);
    beat_off_tick = HAL_GetTick() + LED_PULSE_MS;
}

void LED_FlashPulse(void)
{
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
    flash_off_tick = HAL_GetTick() + LED_PULSE_MS;
}

void LED_MidiClockPulse(void)
{
    LED_BeatPulse();
}

void LED_TapPressPulse(void)
{
    HAL_GPIO_WritePin(TAP_FEEDBACK_LED_GPIO_Port, TAP_FEEDBACK_LED_Pin, GPIO_PIN_SET);
    tap_press_off_tick = HAL_GetTick() + LED_PULSE_MS;
}

void LED_MidiInPulse(void)
{
    HAL_GPIO_WritePin(MIDI_IN_LED_GPIO_Port, MIDI_IN_LED_Pin, GPIO_PIN_SET);
    midi_in_off_tick = HAL_GetTick() + LED_PULSE_MS;
}

void LED_SetPresetIndicator(uint8_t preset_slot_in_bank)
{
    uint8_t slot = (uint8_t)(preset_slot_in_bank % 8U);

    active_button_led_pin = preset_led_pins[slot];
    LED_ApplyButtonIndicatorState();
}

void LED_SetActiveButtonIndicator(uint8_t button_index)
{
    if (button_index >= (sizeof(button_monitor_led_pins) / sizeof(button_monitor_led_pins[0])))
        return;

    active_button_led_pin = button_monitor_led_pins[button_index];
    LED_ApplyButtonIndicatorState();
}

void LED_SetSpecialFunctionIndicator(uint8_t is_active)
{
    special_function_led_active = is_active ? 1U : 0U;
    LED_ApplyButtonIndicatorState();
}

void LED_ShowButtonMonitorIndicator(uint8_t button_index)
{
    if (button_index >= (sizeof(button_monitor_led_pins) / sizeof(button_monitor_led_pins[0])))
    {
        LED_ClearButtonMonitorIndicators();
        return;
    }

    LED_ClearButtonMonitorIndicators();
    HAL_GPIO_WritePin(GPIOF, button_monitor_led_pins[button_index], GPIO_PIN_SET);
}

const char *LED_GetButtonMonitorLabel(uint8_t button_index)
{
    if (button_index >= (sizeof(button_monitor_led_labels) / sizeof(button_monitor_led_labels[0])))
        return "none";

    return button_monitor_led_labels[button_index];
}

void LED_TickUpdate(uint32_t now)
{
    LED_UpdateExpiredOutputs(now);
}

void LED_Update(void)
{
    uint32_t now = HAL_GetTick();

    if (!LED_BeatPulseIsAllowed())
    {
        beat_off_tick = 0U;
        HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_RESET);
    }

    LED_UpdateExpiredOutputs(now);
}
