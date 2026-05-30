#include "app/app_button_monitor.h"
#include "app/app_button_combo.h"
#include "app/app_footswitch_input.h"
#include "app_event.h"
#include "button_functions.h"
#include "main.h"
#include "stm32f4xx_hal.h"

#define FOOTSWITCH_COUNT 11U            /* total number of EXTI-driven footswitch inputs */
#define MUTE_BUTTON_INDEX 10U           /* preset-button slot used for mute / TAP+MUTE bank-up combo */

/* `preset_button_state` is the foreground view of whether a button is still
 * logically down. Press and release events update this directly; the combo
 * timer only remains as a fallback for the mute-activation window. */
static uint8_t preset_button_state[FOOTSWITCH_COUNT] = {0U};
static uint8_t Button_IsTapHeld(void);

static uint8_t Button_ProcessPresetEvent(uint8_t index, uint8_t is_pressed, uint32_t now)
{
    if (is_pressed == preset_button_state[index]) {
        return 0U;
    }

    preset_button_state[index] = is_pressed;

    if (is_pressed == 0U)
    {
        if (index == MUTE_BUTTON_INDEX && AppButtonCombo_HandleMuteRelease(now))
        {
            AppButtonMonitor_LogMuteState("commit-up", now, preset_button_state[MUTE_BUTTON_INDEX]);
        }
        return 0U;
    }

    if (index == MUTE_BUTTON_INDEX) {
        /* Mute is two-stage: a quick tap may combine with TAP for bank up,
         * otherwise the actual mute overlay is armed and committed later. */
        AppButtonMonitor_LogMuteState("press", now, preset_button_state[MUTE_BUTTON_INDEX]);
        if (AppButtonCombo_HandleMutePress(now, Button_IsTapHeld())) {
            AppButtonMonitor_LogMuteState("combo-up", now, preset_button_state[MUTE_BUTTON_INDEX]);
        } else if (AppButtonCombo_IsMuteActivationPending()) {
            AppButtonMonitor_LogMuteState("armed", now, preset_button_state[MUTE_BUTTON_INDEX]);
        }
        return 0U;
    }

    return 1U;
}

static uint8_t Button_IsTapHeld(void)
{
    return (HAL_GPIO_ReadPin(TAP_GPIO_Port, TAP_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

/**
 * Returns the current logical pressed state of the mute footswitch.
 */
uint8_t Button_IsMuteHeld(void)
{
    return AppFootswitchInput_ReadPressed(MUTE_BUTTON_INDEX);
}

/**
 * Applies one interrupt-time button edge and routes it to the shell logic.
 */
uint8_t Button_ProcessInterruptEvent(uint8_t index, uint8_t is_pressed, uint32_t now)
{
    if (index >= FOOTSWITCH_COUNT)
        return 0U;

    (void)now;
    if (is_pressed && AppButtonMonitor_HandleFootswitchPress(index))
        return 0U;

    return Button_ProcessPresetEvent(index, is_pressed, now);
}

/**
 * Services the mute-combo timeout and re-arms non-mute preset buttons.
 */
void Button_ProcessPendingEvents(void)
{
    uint32_t now = HAL_GetTick();

    /* If mute was not consumed by the TAP+MUTE bank-up combo inside the combo
     * window, commit it here as a normal mute press. */
    if (AppButtonCombo_Service(now)) {
        AppButtonMonitor_LogMuteState("commit-400", now, preset_button_state[MUTE_BUTTON_INDEX]);
    }

    /* Non-mute footswitches are press-only EXTI inputs, so they still need a
     * foreground release scan to re-arm the next press after the GPIO rises. */
    for (uint8_t index = 0U; index < FOOTSWITCH_COUNT; ++index)
    {
        if ((index == MUTE_BUTTON_INDEX) || !preset_button_state[index])
            continue;

        if (AppFootswitchInput_ReadPressed(index) == 0U)
            preset_button_state[index] = 0U;
    }
}
