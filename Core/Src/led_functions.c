#include "led_functions.h"
#include "display_functions.h"
#include "main.h"          /* board LED and custom LED GPIO definitions */
#include "stm32f4xx_hal.h"
#include "midi/clock_engine.h"

#define LED_PULSE_MS  50U  /* pulse width for all LED blinks */
#define LED_PULSE_US ((uint32_t)LED_PULSE_MS * 1000UL)
#define LED_TIMING_COMPARE_GUARD_US 20UL //
#define LED_BEAT_DUPLICATE_GUARD_US 80000UL
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
static volatile uint32_t beat_pulse_last_due_us = 0U;
static volatile uint8_t led_timing_compare_pending = 0U;
static volatile uint8_t led_all_outputs_suppressed = 0U;
static volatile uint8_t led_beat_output_suppressed = 0U;
static uint16_t active_button_led_pin = 0U; /* one active selection LED across preset/random/mute */
static uint8_t special_function_led_active = 0U; /* sticky state for button 10 mode */

static void LED_UpdateUiSuppressionState(void);
static uint8_t LED_AllOutputsAreSuppressed(void);
static uint8_t LED_TransientPulseIsAllowed(void);
static uint8_t LED_BeatPulseIsAllowed(void);
static uint8_t LED_PulseDeadlineIsActive(uint32_t off_tick, uint32_t now);
static void LED_ClearAllPhysicalOutputs(void);
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

/**
 * Initializes the LED GPIO outputs used by the firmware.
 */
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

/**
 * Clears all preset/button-monitor indicator LEDs at once.
 */
void LED_ClearButtonMonitorIndicators(void)
{
    HAL_GPIO_WritePin(GPIOF, BUTTON_MONITOR_LED_PINS_MASK, GPIO_PIN_RESET);
}

static void LED_UpdateUiSuppressionState(void)
{
    uint8_t menu_active = Display_MenuIsActive();

    led_all_outputs_suppressed = menu_active ? 1U : 0U;
    led_beat_output_suppressed = menu_active ? 1U : 0U;
}

static uint8_t LED_AllOutputsAreSuppressed(void)
{
    return led_all_outputs_suppressed;
}

static uint8_t LED_TransientPulseIsAllowed(void)
{
    return LED_AllOutputsAreSuppressed() ? 0U : 1U;
}

static void LED_ApplyButtonIndicatorState(void)
{
    uint16_t pin_mask = 0U;

    HAL_GPIO_WritePin(GPIOF, BUTTON_MONITOR_LED_PINS_MASK, GPIO_PIN_RESET);

    if (LED_AllOutputsAreSuppressed())
        return;

    if (active_button_led_pin != 0U)
    {
        pin_mask = active_button_led_pin;
    }

    if (special_function_led_active)
    {
        pin_mask |= PRESET_LED9_Pin;
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

static void LED_ClearAllPhysicalOutputs(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    LED_DisarmBeatPulseCompare();
    beat_off_tick = 0U;
    flash_off_tick = 0U;
    tap_press_off_tick = 0U;
    midi_in_off_tick = 0U;
    beat_pulse_last_due_us = 0U;
    led_timing_compare_pending = 0U;
    if (primask == 0U)
        __enable_irq();

    HAL_GPIO_WritePin(GPIOF, BUTTON_MONITOR_LED_PINS_MASK, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TAP_FEEDBACK_LED_GPIO_Port, TAP_FEEDBACK_LED_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MIDI_IN_LED_GPIO_Port, MIDI_IN_LED_Pin, GPIO_PIN_RESET);
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
    return led_beat_output_suppressed ? 0U : 1U;
}

/**
 * Handles the TIM2 compare interrupt that drives beat pulse edges.
 */
void LED_ServiceDeferredTimingWork(void)
{
    uint32_t now_us;

    if (led_timing_compare_pending == 0U)
    {
        return;
    }

    led_timing_compare_pending = 0U;

    if (!LED_BeatPulseIsAllowed())
    {
        LED_DisarmBeatPulseCompare();
        return;
    }

    if (!beat_pulse_compare_active)
    {
        /* Pulse width must be measured from the actual ON edge. Anchors can
         * legitimately arrive in the past under load, so deriving OFF from the
         * anchor shortens/lengthens visible pulse width. */
        now_us = LED_TimingNowUs();
        HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_SET);
        beat_pulse_compare_on_us = now_us;
        beat_pulse_compare_off_us = now_us + LED_PULSE_US;
        beat_pulse_compare_active = 1U;
        LED_ArmBeatPulseCompare(beat_pulse_compare_off_us);
        return;
    }

    LED_DisarmBeatPulseCompare();
}

void LED_HandleTimingCounterIrq(void)
{
    LED_FlagTimingCounterIrq();
    LED_ServiceDeferredTimingWork();
}

__attribute__((section(".RamFunc")))
void LED_FlagTimingCounterIrq(void)
{
    if (((TIM2->SR & TIM_SR_CC3IF) == 0U)
     || ((TIM2->DIER & TIM_DIER_CC3IE) == 0U))
    {
        return;
    }

    TIM2->SR = ~TIM_SR_CC3IF;
    led_timing_compare_pending = 1U;
}

/**
 * Returns true while any transient LED pulse or beat compare is still active.
 */
uint8_t LED_IsPulseActive(void)
{
    uint32_t now;
    uint8_t beat_pulse_active = 0U;

    if (LED_AllOutputsAreSuppressed())
        return 0U;

    now = HAL_GetTick();
    if (LED_BeatPulseIsAllowed())
        beat_pulse_active = (uint8_t)(beat_pulse_compare_active || (beat_pulse_compare_on_us != 0U));

    return (uint8_t)(beat_pulse_active
                   || LED_PulseDeadlineIsActive(flash_off_tick, now)
                   || LED_PulseDeadlineIsActive(tap_press_off_tick, now)
                   || LED_PulseDeadlineIsActive(midi_in_off_tick, now));
}

/**
 * Starts a beat pulse using the current timing counter value as the anchor.
 */
void LED_BeatPulse(void)
{
    LED_BeatPulseAtUs(LED_TimingNowUs());
}

/**
 * Starts a beat pulse from an explicit microsecond timestamp.
 */
void LED_BeatPulseAtUs(uint32_t start_us)
{
    uint32_t primask;
    uint32_t now_us;
    uint32_t due_us;
    uint32_t elapsed_since_last_due_us;

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
    now_us = LED_TimingNowUs();

    /* Beat LED timing follows the transport anchor directly so INT and EXT
     * modes share one observable timing truth. */
    due_us = LED_TimeReachedUs(now_us, start_us) ? now_us : start_us;

    /* Observer-only duplicate suppression: drop pulses that arrive too soon
     * to be musical quarter notes, which avoids perceived "too fast" LED
     * cadence when upstream emits occasional extra beat cues. */
    if (beat_pulse_last_due_us != 0U)
    {
        elapsed_since_last_due_us = due_us - beat_pulse_last_due_us;
        if (elapsed_since_last_due_us < LED_BEAT_DUPLICATE_GUARD_US)
        {
            if (primask == 0U)
                __enable_irq();
            return;
        }
    }

    beat_pulse_compare_active = 0U;
    beat_pulse_compare_on_us = due_us;
    beat_pulse_compare_off_us = 0U;
    beat_pulse_last_due_us = due_us;
    HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_RESET);
    LED_ArmBeatPulseCompare(due_us);
    if (primask == 0U)
        __enable_irq();
}

/**
 * Emits the short flash-write indicator pulse on the board LED.
 */
void LED_FlashPulse(void)
{
    if (!LED_TransientPulseIsAllowed())
    {
        flash_off_tick = 0U;
        HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
        return;
    }

    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
    flash_off_tick = HAL_GetTick() + LED_PULSE_MS;
}

/**
 * Emits a MIDI clock pulse using the current timing counter value.
 */
void LED_MidiClockPulse(void)
{
    LED_MidiClockPulseAtUs(LED_TimingNowUs());
}

/**
 * Emits a MIDI clock pulse from an explicit timing anchor.
 */
void LED_MidiClockPulseAtUs(uint32_t start_us)
{
    LED_BeatPulseAtUs(start_us);
}

/**
 * Emits the short tap-feedback pulse on the dedicated tap LED.
 */
void LED_TapPressPulse(void)
{
    if (!LED_TransientPulseIsAllowed())
    {
        tap_press_off_tick = 0U;
        HAL_GPIO_WritePin(TAP_FEEDBACK_LED_GPIO_Port, TAP_FEEDBACK_LED_Pin, GPIO_PIN_RESET);
        return;
    }

    HAL_GPIO_WritePin(TAP_FEEDBACK_LED_GPIO_Port, TAP_FEEDBACK_LED_Pin, GPIO_PIN_SET);
    tap_press_off_tick = HAL_GetTick() + LED_PULSE_MS;
}

/**
 * Emits the short MIDI-input activity pulse on the dedicated MIDI LED.
 */
void LED_MidiInPulse(void)
{
    if (!LED_TransientPulseIsAllowed())
    {
        midi_in_off_tick = 0U;
        HAL_GPIO_WritePin(MIDI_IN_LED_GPIO_Port, MIDI_IN_LED_Pin, GPIO_PIN_RESET);
        return;
    }

    HAL_GPIO_WritePin(MIDI_IN_LED_GPIO_Port, MIDI_IN_LED_Pin, GPIO_PIN_SET);
    midi_in_off_tick = HAL_GetTick() + LED_PULSE_MS;
}

/**
 * Selects the active preset indicator LED for the current bank slot.
 */
void LED_SetPresetIndicator(uint8_t preset_slot_in_bank)
{
    uint8_t slot = (uint8_t)(preset_slot_in_bank % 8U);

    active_button_led_pin = preset_led_pins[slot];
    LED_ApplyButtonIndicatorState();
}

/**
 * Selects a specific button-monitor LED as the active indicator.
 */
void LED_SetActiveButtonIndicator(uint8_t button_index)
{
    if (button_index >= (sizeof(button_monitor_led_pins) / sizeof(button_monitor_led_pins[0])))
        return;

    active_button_led_pin = button_monitor_led_pins[button_index];
    LED_ApplyButtonIndicatorState();
}

/**
 * Enables or disables the special-function indicator LED.
 */
void LED_SetSpecialFunctionIndicator(uint8_t is_active)
{
    special_function_led_active = is_active ? 1U : 0U;
    LED_ApplyButtonIndicatorState();
}

/**
 * Shows one button-monitor indicator LED for hardware diagnostics.
 */
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

/**
 * Returns the human-readable label for a button-monitor indicator LED.
 */
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

/**
 * Called from the foreground 10 ms service cadence.
 * Applies clock-governance beat-pulse gating derived from ClockEngine snapshot.
 * Runs in non-ISR context: safe to call ClockEngine_GetBehaviorProfile().
 * If the current clock state does not allow beat pulses (e.g. DETECTING), any
 * pending compare is disarmed so stale beat compares from the previous state
 * cannot fire after a governance transition.
 */
/**
 * Called from the foreground 10 ms service cadence.
 * Applies clock-governance beat-pulse gating using a single ClockEngineTick_t
 * so that state, profile, and beat data are all read from the same coherent
 * snapshot. Avoids any divergence from calling GetState/GetBehaviorProfile
 * separately on different snapshots.
 */
static void LED_ApplyBeatPulseGovernance(void)
{
    ClockEngineTick_t tick = ClockEngine_GetTick();

    if (!tick.profile.allow_led_beat_pulse)
    {
        uint32_t primask = __get_PRIMASK();

        __disable_irq();
        LED_DisarmBeatPulseCompare();
        if (primask == 0U)
            __enable_irq();
    }
}

void LED_Update(void)
{
    uint32_t now = HAL_GetTick();

    LED_UpdateUiSuppressionState();

    if (LED_AllOutputsAreSuppressed())
    {
        LED_ClearAllPhysicalOutputs();
        return;
    }

    LED_ApplyBeatPulseGovernance();

    if (!LED_BeatPulseIsAllowed())
    {
        uint32_t primask = __get_PRIMASK();

        __disable_irq();
        LED_DisarmBeatPulseCompare();
        beat_off_tick = 0U;
        beat_pulse_last_due_us = 0U;
        led_timing_compare_pending = 0U;
        if (primask == 0U)
            __enable_irq();
    }

    LED_ServiceDeferredTimingWork();

    LED_UpdateExpiredOutputs(now);

    /* Re-apply button indicator state in case mode changed */
    LED_ApplyButtonIndicatorState();
}
