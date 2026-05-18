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
 * logically down. For the falling-edge buttons we re-arm this state only once
 * the main loop sees the GPIO released again, which keeps switch bounce from
 * toggling the action twice. */
static uint8_t preset_button_state[FOOTSWITCH_COUNT] = {0U};
static uint8_t Button_IsTapHeld(void);

static uint8_t Button_ProcessPresetEvent(uint8_t index, uint8_t is_pressed, uint32_t now)
{
    /* Mute release is committed from Button_ProcessPendingEvents() after the
     * combo window logic runs; do not clear the latched pressed state here. */
    if ((index == MUTE_BUTTON_INDEX) && (is_pressed == 0U))
        return 0U;

    if (is_pressed == preset_button_state[index]) {
        return 0U;
    }

    preset_button_state[index] = is_pressed;

    if (is_pressed == 0U)
        return 0U;

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

uint8_t Button_IsMuteHeld(void)
{
    return AppFootswitchInput_ReadPressed(MUTE_BUTTON_INDEX);
}

uint8_t Button_ProcessInterruptEvent(uint8_t index, uint8_t is_pressed, uint32_t now)
{
    if (index >= FOOTSWITCH_COUNT)
        return 0U;

    (void)now;
    if (is_pressed && AppButtonMonitor_HandleFootswitchPress(index))
        return 0U;

    return Button_ProcessPresetEvent(index, is_pressed, now);
}

void Button_ProcessPendingEvents(void) {
    uint32_t now = HAL_GetTick();

    /* Mute keeps both EXTI edges, but a very fast tap can still lose the
     * release edge to debounce. Re-arm from polled GPIO so mute never stays
     * logically stuck pressed between taps. */
    if (preset_button_state[MUTE_BUTTON_INDEX]
        && (AppFootswitchInput_ReadPressed(MUTE_BUTTON_INDEX) == 0U)) {
        preset_button_state[MUTE_BUTTON_INDEX] = 0U;

        AppButtonMonitor_LogMuteState("poll-up", now, preset_button_state[MUTE_BUTTON_INDEX]);

        if (AppButtonCombo_HandleMuteRelease(now)) {
            AppButtonMonitor_LogMuteState("commit-up", now, preset_button_state[MUTE_BUTTON_INDEX]);
        }
    }

    /* If mute was not consumed by the TAP+MUTE bank-up combo inside the combo
     * window, commit it here as a normal mute press. */
    if (AppButtonCombo_Service(now)) {
        AppButtonMonitor_LogMuteState("commit-400", now, preset_button_state[MUTE_BUTTON_INDEX]);
    }

    /* Falling-edge buttons stay logically pressed until the main loop sees
     * the pin released again. */
    for (uint8_t i = 0U; i < FOOTSWITCH_COUNT; ++i) {
        if (preset_button_state[i]
            && (AppFootswitchInput_ReadPressed(i) == 0U)) {
            preset_button_state[i] = 0U;
        }
    }
}
