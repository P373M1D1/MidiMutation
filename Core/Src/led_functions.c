#include "led_functions.h"
#include "display_functions.h"
#include "main.h"          /* LD1_Pin / LD1_GPIO_Port, LD2_Pin / LD2_GPIO_Port */
#include "stm32f4xx_hal.h"

#define LED_PULSE_MS  50U  /* pulse width for all LED blinks */

/* Internal tick targets – 0 means LED is already off */
static volatile uint32_t beat_off_tick  = 0U;  /* LD1 green – tap tempo beat */
static volatile uint32_t flash_off_tick = 0U;  /* LD2 blue  – Flash write    */
static volatile uint32_t midi_in_off_tick = 0U; /* PF15      – MIDI in start  */

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

static void LED_ClearPresetIndicators(void)
{
    HAL_GPIO_WritePin(GPIOF,
                      PRESET_LED1_Pin | PRESET_LED2_Pin | PRESET_LED3_Pin | PRESET_LED4_Pin |
                      PRESET_LED5_Pin | PRESET_LED6_Pin | PRESET_LED7_Pin | PRESET_LED8_Pin,
                      GPIO_PIN_RESET);
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

void LED_MidiInPulse(void)
{
    HAL_GPIO_WritePin(MIDI_IN_LED_GPIO_Port, MIDI_IN_LED_Pin, GPIO_PIN_SET);
    midi_in_off_tick = HAL_GetTick() + LED_PULSE_MS;
}

void LED_SetPresetIndicator(uint8_t preset_slot_in_bank)
{
    uint8_t slot = (uint8_t)(preset_slot_in_bank % 8U);

    LED_ClearPresetIndicators();
    HAL_GPIO_WritePin(GPIOF, preset_led_pins[slot], GPIO_PIN_SET);
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
