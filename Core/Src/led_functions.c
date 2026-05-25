#include "led_functions.h"
#include "display_functions.h"
#include "main.h"          /* LD1_Pin / LD1_GPIO_Port, LD2_Pin / LD2_GPIO_Port */
#include "stm32f4xx_hal.h"

#define LED_PULSE_MS  50U  /* pulse width for all LED blinks */
#define LED_PULSE_US ((uint32_t)LED_PULSE_MS * 1000UL)
#define LED_TIMING_COMPARE_GUARD_US 20UL //
#define LED_PRESET_EDIT_DIM_PERIOD_MS 10U /* dim PWM-like cycle length in edit mode; increase for slower flicker, decrease for smoother/faster gating */
#define LED_PRESET_EDIT_DIM_ON_MS 1U /* dim brightness control in edit mode; lower = dimmer, higher = brighter (duty = ON / PERIOD, here 2/10 = 20%) */
#define BUTTON_MONITOR_LED_PINS_MASK (PRESET_LED1_Pin | PRESET_LED2_Pin | PRESET_LED3_Pin | PRESET_LED4_Pin \
                                    | PRESET_LED5_Pin | PRESET_LED6_Pin | PRESET_LED7_Pin | PRESET_LED8_Pin \
                                    | PRESET_LED9_Pin | PRESET_LED10_Pin | PRESET_LED11_Pin)

/* Internal tick targets – 0 means LED is already off */
static volatile uint32_t beat_off_tick  = 0U;  /* LD1 green – tap tempo beat */
static volatile uint32_t flash_off_tick = 0U;  /* LD2 blue  – Flash write    */
static volatile uint32_t tap_press_off_tick = 0U; /* PF10     – tap press      */
static volatile uint32_t midi_in_off_tick = 0U; /* PF15      – MIDI in start  */
static volatile uint8_t beat_pulse_compare_active = 0U;
static volatile uint32_t beat_pulse_compare_on_us = 0U;
static volatile uint32_t beat_pulse_compare_off_us = 0U;
static uint16_t active_button_led_pin = 0U; /* one active selection LED across preset/random/mute */
static uint8_t special_function_led_active = 0U; /* sticky state for button 10 mode */

static uint8_t LED_BeatPulseIsAllowed(void);
static uint8_t LED_InEditMode(void);
static uint8_t LED_UsePresetEditDimming(void);
static uint8_t LED_PresetEditDimPulseIsOn(uint32_t now_ms);
static uint8_t LED_PulseDeadlineIsActive(uint32_t off_tick, uint32_t now);
__attribute__((section(".RamFunc")))
static uint32_t LED_TimingNowUs(void);
__attribute__((section(".RamFunc")))
static uint8_t LED_TimeReachedUs(uint32_t now_us, uint32_t due_us);
__attribute__((section(".RamFunc")))
static void LED_ArmBeatPulseCompare(uint32_t due_us);
__attribute__((section(".RamFunc")))
static void LED_DisarmBeatPulseCompare(void);

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

void LED_InitBoardOutputs(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    HAL_GPIO_WritePin(GPIOF,
                      BUTTON_MONITOR_LED_PINS_MASK,
                      GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TAP_FEEDBACK_LED_GPIO_Port,
                      TAP_FEEDBACK_LED_Pin,
                      GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MIDI_IN_LED_GPIO_Port,
                      MIDI_IN_LED_Pin,
                      GPIO_PIN_RESET);

    gpio_init.Pin = MIDI_IN_LED_Pin;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(MIDI_IN_LED_GPIO_Port, &gpio_init);

    gpio_init.Pin = BUTTON_MONITOR_LED_PINS_MASK;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOF, &gpio_init);

    gpio_init.Pin = TAP_FEEDBACK_LED_Pin;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(TAP_FEEDBACK_LED_GPIO_Port, &gpio_init);
}

void LED_ClearButtonMonitorIndicators(void)
{
    HAL_GPIO_WritePin(GPIOF, BUTTON_MONITOR_LED_PINS_MASK, GPIO_PIN_RESET);
}

static uint8_t LED_InEditMode(void)
{
    return (Display_PresetEditIsActive() || Display_MenuIsActive()) ? 1U : 0U;
}

static uint8_t LED_UsePresetEditDimming(void)
{
    return LED_InEditMode();
}

static uint8_t LED_PresetEditDimPulseIsOn(uint32_t now_ms)
{
    /* Dim-level tuning lives in LED_PRESET_EDIT_DIM_PERIOD_MS and
     * LED_PRESET_EDIT_DIM_ON_MS above. Keep ON <= PERIOD. */
    return ((now_ms % LED_PRESET_EDIT_DIM_PERIOD_MS) < LED_PRESET_EDIT_DIM_ON_MS) ? 1U : 0U;
}

static void LED_ApplyButtonIndicatorState(void)
{
    uint16_t pin_mask = 0U;
    uint8_t in_live_mode = LED_InEditMode() ? 0U : 1U;
    uint8_t in_edit_mode = LED_UsePresetEditDimming();
    uint8_t dim_pulse_on = 1U;

    if (in_edit_mode)
    {
        /* To retune edit-mode dim level later, change the two
         * LED_PRESET_EDIT_DIM_* constants near the top of this file. */
        dim_pulse_on = LED_PresetEditDimPulseIsOn(HAL_GetTick());
    }

    HAL_GPIO_WritePin(GPIOF, BUTTON_MONITOR_LED_PINS_MASK, GPIO_PIN_RESET);

    /* Keep the active button indicator available in all modes; in edit modes,
     * gate it with the dim pulse for reduced perceived brightness. */
    if (active_button_led_pin != 0U)
    {
        if (in_edit_mode)
        {
            if (dim_pulse_on)
                pin_mask = active_button_led_pin;
        }
        else
        {
            pin_mask = active_button_led_pin;
        }
    }

    /* Special function indicator stays visible while editing, but dimmed. */
    if (special_function_led_active)
    {
        if (in_live_mode)
        {
            pin_mask |= PRESET_LED9_Pin;
        }
        else if (in_edit_mode && dim_pulse_on)
        {
            pin_mask |= PRESET_LED9_Pin;
        }
    }

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

static uint8_t LED_PulseDeadlineIsActive(uint32_t off_tick, uint32_t now)
{
    return (off_tick != 0U && ((int32_t)(off_tick - now) > 0)) ? 1U : 0U;
}

__attribute__((section(".RamFunc")))
static uint32_t LED_TimingNowUs(void)
{
    return TIM2->CNT;
}

__attribute__((section(".RamFunc")))
static uint8_t LED_TimeReachedUs(uint32_t now_us, uint32_t due_us)
{
    return ((int32_t)(now_us - due_us) >= 0) ? 1U : 0U;
}

__attribute__((section(".RamFunc")))
static void LED_ArmBeatPulseCompare(uint32_t due_us)
{
    uint32_t now_us = LED_TimingNowUs();
    uint32_t earliest_due_us = now_us + LED_TIMING_COMPARE_GUARD_US;

    if (LED_TimeReachedUs(earliest_due_us, due_us))
        due_us = earliest_due_us;

    TIM2->CCR3 = due_us;
    TIM2->SR = ~TIM_SR_CC3IF;
    TIM2->DIER |= TIM_DIER_CC3IE;
}

__attribute__((section(".RamFunc")))
static void LED_DisarmBeatPulseCompare(void)
{
    beat_pulse_compare_active = 0U;
    beat_pulse_compare_on_us = 0U;
    beat_pulse_compare_off_us = 0U;
    TIM2->DIER &= ~TIM_DIER_CC3IE;
    TIM2->SR = ~TIM_SR_CC3IF;
    HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_RESET);
}

static uint8_t LED_BeatPulseIsAllowed(void)
{
    return 1U;
}

void LED_HandleTimingCounterIrq(void)
{
    if (((TIM2->SR & TIM_SR_CC3IF) == 0U)
     || ((TIM2->DIER & TIM_DIER_CC3IE) == 0U))
    {
        return;
    }

    TIM2->SR = ~TIM_SR_CC3IF;

    if (!LED_BeatPulseIsAllowed())
    {
        LED_DisarmBeatPulseCompare();
        return;
    }

    if (!beat_pulse_compare_active)
    {
        HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_SET);
        beat_pulse_compare_active = 1U;
        LED_ArmBeatPulseCompare(beat_pulse_compare_off_us);
        return;
    }

    LED_DisarmBeatPulseCompare();
}

uint8_t LED_IsPulseActive(void)
{
    uint32_t now;
    uint8_t beat_pulse_active;

    if (!LED_BeatPulseIsAllowed())
        return 0U;

    now = HAL_GetTick();
    beat_pulse_active = (uint8_t)(beat_pulse_compare_active || (beat_pulse_compare_on_us != 0U));
    return (uint8_t)(beat_pulse_active
                   || LED_PulseDeadlineIsActive(flash_off_tick, now)
                   || LED_PulseDeadlineIsActive(tap_press_off_tick, now)
                   || LED_PulseDeadlineIsActive(midi_in_off_tick, now));
}

void LED_BeatPulse(void)
{
    LED_BeatPulseAtUs(LED_TimingNowUs());
}

void LED_BeatPulseAtUs(uint32_t start_us)
{
    uint32_t primask;

    if (!LED_BeatPulseIsAllowed())
    {
        primask = __get_PRIMASK();
        __disable_irq();
        LED_DisarmBeatPulseCompare();
        if (primask == 0U)
            __enable_irq();
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    beat_pulse_compare_active = 0U;
    beat_pulse_compare_on_us = start_us;
    beat_pulse_compare_off_us = start_us + LED_PULSE_US;
    HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_RESET);
    LED_ArmBeatPulseCompare(start_us);
    if (primask == 0U)
        __enable_irq();
}

void LED_FlashPulse(void)
{
    if (!LED_BeatPulseIsAllowed())
    {
        flash_off_tick = 0U;
        HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
        return;
    }

    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
    flash_off_tick = HAL_GetTick() + LED_PULSE_MS;
}

void LED_MidiClockPulse(void)
{
    LED_MidiClockPulseAtUs(LED_TimingNowUs());
}

void LED_MidiClockPulseAtUs(uint32_t start_us)
{
    LED_BeatPulseAtUs(start_us);
}

void LED_TapPressPulse(void)
{
    if (!LED_BeatPulseIsAllowed())
    {
        tap_press_off_tick = 0U;
        HAL_GPIO_WritePin(TAP_FEEDBACK_LED_GPIO_Port, TAP_FEEDBACK_LED_Pin, GPIO_PIN_RESET);
        return;
    }

    HAL_GPIO_WritePin(TAP_FEEDBACK_LED_GPIO_Port, TAP_FEEDBACK_LED_Pin, GPIO_PIN_SET);
    tap_press_off_tick = HAL_GetTick() + LED_PULSE_MS;
}

void LED_MidiInPulse(void)
{
    if (!LED_BeatPulseIsAllowed())
    {
        midi_in_off_tick = 0U;
        HAL_GPIO_WritePin(MIDI_IN_LED_GPIO_Port, MIDI_IN_LED_Pin, GPIO_PIN_RESET);
        return;
    }

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
    uint8_t in_edit_mode = LED_UsePresetEditDimming();
    uint8_t dim_pulse_on = in_edit_mode ? LED_PresetEditDimPulseIsOn(now) : 1U;
    uint8_t beat_pulse_active = (uint8_t)(beat_pulse_compare_active || (beat_pulse_compare_on_us != 0U));

    if (!LED_BeatPulseIsAllowed())
    {
        uint32_t primask = __get_PRIMASK();

        __disable_irq();
        LED_DisarmBeatPulseCompare();
        if (primask == 0U)
            __enable_irq();
        flash_off_tick = 0U;
        tap_press_off_tick = 0U;
        midi_in_off_tick = 0U;
        HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(TAP_FEEDBACK_LED_GPIO_Port, TAP_FEEDBACK_LED_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MIDI_IN_LED_GPIO_Port, MIDI_IN_LED_Pin, GPIO_PIN_RESET);
    }

    LED_UpdateExpiredOutputs(now);

    /* Re-apply button indicator state in case mode changed */
    LED_ApplyButtonIndicatorState();

    /* In edit mode, pulse-driven LEDs are duty-cycled so all visible feedback
     * appears dimmer, including tap and transport activity LEDs. */
    if (in_edit_mode)
    {
        if (beat_pulse_active)
            HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, dim_pulse_on ? GPIO_PIN_SET : GPIO_PIN_RESET);

        if (flash_off_tick != 0U)
            HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, dim_pulse_on ? GPIO_PIN_SET : GPIO_PIN_RESET);

        if (tap_press_off_tick != 0U)
            HAL_GPIO_WritePin(TAP_FEEDBACK_LED_GPIO_Port,
                              TAP_FEEDBACK_LED_Pin,
                              dim_pulse_on ? GPIO_PIN_SET : GPIO_PIN_RESET);

        if (midi_in_off_tick != 0U)
            HAL_GPIO_WritePin(MIDI_IN_LED_GPIO_Port,
                              MIDI_IN_LED_Pin,
                              dim_pulse_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}
