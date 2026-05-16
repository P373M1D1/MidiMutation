#include "bpm_functions.h"
#include "app_event.h"
#include "button_functions.h"
#include "led_functions.h"
#include "main.h"
#include "presets.h"
#include "display_functions.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>

extern volatile uint16_t  g_bpm;
extern const Preset_t    *active_preset;

#define FOOTSWITCH_COUNT 11U            /* total number of EXTI-driven footswitch inputs */
#define RANDOM_BUTTON_INDEX 8U          /* preset-button slot used for the random preset action */
#define SPECIAL_FUNCTION_BUTTON_INDEX 9U /* preset-button slot used to toggle the special-functions overlay */
#define MUTE_BUTTON_INDEX 10U           /* preset-button slot used for mute / TAP+MUTE bank-up combo */
#define FOOTSWITCH_DEBOUNCE_MS 20U      /* ignore edges that arrive too soon after the previous edge on the same switch */
#define BANK_COMBO_WINDOW_MS 400U       /* tap+mute presses inside this window are treated as bank navigation */
#define MUTE_BUTTON_DEBUG_ENABLED 1U    /* temporary UART diagnostics for mute press/release/combo timing */

/* `preset_button_state` is the foreground view of whether a button is still
 * logically down. For the falling-edge buttons we re-arm this state only once
 * the main loop sees the GPIO released again, which keeps switch bounce from
 * toggling the action twice. */
static uint8_t preset_button_state[FOOTSWITCH_COUNT] = {0U};
static uint32_t preset_button_event_tick[FOOTSWITCH_COUNT] = {0U};
static uint8_t special_functions_active = 0U;
static uint8_t mute_activation_pending = 0U;

#if MUTE_BUTTON_DEBUG_ENABLED
static void Button_DebugMuteState(const char *tag, uint32_t now)
{
    uint8_t tap_held = (HAL_GPIO_ReadPin(TAP_GPIO_Port, TAP_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
    uint8_t mute_held = (HAL_GPIO_ReadPin(PRESET_BTN_GPIO_Port, PRESET_BTN11_Pin) == GPIO_PIN_RESET) ? 1U : 0U;

    printf("MUTEDBG %-12s t=%lu pressed=%u pending=%u tapHeld=%u muteHeld=%u\r\n",
           tag,
           (unsigned long)now,
           (unsigned)preset_button_state[MUTE_BUTTON_INDEX],
           (unsigned)mute_activation_pending,
           (unsigned)tap_held,
           (unsigned)mute_held);
}
#endif

static const uint16_t preset_button_pins[FOOTSWITCH_COUNT] = {
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

#if BUTTON_LED_MONITOR_ENABLED
static const char * const button_monitor_input_labels[FOOTSWITCH_COUNT] = {
    "PE0",
    "PE1",
    "PE2",
    "PE11",
    "PE12",
    "PE5",
    "PE6",
    "PE7",
    "PE8",
    "PE9",
    "PE10",
};

static void Button_MonitorReportPress(uint8_t index)
{
    if (index >= FOOTSWITCH_COUNT)
        return;

    LED_ShowButtonMonitorIndicator(index);
    printf("BTNMON button=%u input=%s led=%s\r\n",
           (unsigned)(index + 1U),
           button_monitor_input_labels[index],
           LED_GetButtonMonitorLabel(index));
}
#endif

void Button_MonitorReportTapPress(void)
{
#if BUTTON_LED_MONITOR_ENABLED
    LED_TapPressPulse();
    printf("BTNMON tap input=PG15 led=PF10(TAP_PRESS)\r\n");
#endif
}

static uint8_t Button_ReadPresetPressed(uint8_t index)
{
    return (HAL_GPIO_ReadPin(PRESET_BTN_GPIO_Port, preset_button_pins[index]) == GPIO_PIN_RESET) ? 1U : 0U;
}

static void Button_QueueScreensaverWakeEvent(uint32_t now)
{
    AppEvent_t event;

    event.type = APP_EVENT_TYPE_SCREENSAVER_WAKE;
    event.source = APP_EVENT_SOURCE_NONE;
    event.value = 0;
    event.tick = now;
    (void)AppEvent_Push(&event);
}

static int8_t Button_TryResolveIndex(uint16_t gpio_pin)
{
    if (gpio_pin == USER_Btn_Pin) {
        return (int8_t)RANDOM_BUTTON_INDEX;
    }

    for (uint8_t i = 0U; i < FOOTSWITCH_COUNT; ++i) {
        if (preset_button_pins[i] == gpio_pin) {
            return (int8_t)i;
        }
    }

    return -1;
}

static void Button_ProcessPresetEvent(uint8_t index, uint8_t is_pressed, uint32_t now)
{
    AppEvent_t event;

    if (is_pressed == preset_button_state[index]) {
        return;
    }

    preset_button_state[index] = is_pressed;

    if (is_pressed) {
        Button_QueueScreensaverWakeEvent(now);

        if (index == RANDOM_BUTTON_INDEX) {
            event.type = APP_EVENT_TYPE_PRESET_ACTIVATE_RANDOM;
            event.source = APP_EVENT_SOURCE_NONE;
            event.value = 0;
            event.tick = now;
            (void)AppEvent_Push(&event);
        } else if (index == SPECIAL_FUNCTION_BUTTON_INDEX) {
            /* The button module owns the mode bit; presets.c only redraws the
             * active screen so the right-side status text follows that state. */
            if (special_functions_active == 0U) {
                special_functions_active = 1U;
            } else {
                special_functions_active = 0U;
            }
            LED_SetSpecialFunctionIndicator(special_functions_active);

            event.type = APP_EVENT_TYPE_REDRAW_ACTIVE_DISPLAY;
            event.source = APP_EVENT_SOURCE_NONE;
            event.value = 0;
            event.tick = now;
            (void)AppEvent_Push(&event);
        } else if (index == MUTE_BUTTON_INDEX) {
            /* Mute is two-stage: a quick tap may combine with TAP for bank up,
             * otherwise the actual mute overlay is armed and committed later. */
            if (Button_HandleMutePress(now)) {
                Button_CancelTapBankCombo();
            }
        } else {
            event.type = APP_EVENT_TYPE_PRESET_ACTIVATE;
            event.source = APP_EVENT_SOURCE_NONE;
            event.value = (int16_t)(current_bank * PRESETS_PER_BANK + index);
            event.tick = now;
            (void)AppEvent_Push(&event);
        }
    } else if ((index == MUTE_BUTTON_INDEX) && mute_activation_pending) {
        mute_activation_pending = 0U;
        event.type = APP_EVENT_TYPE_PRESET_ACTIVATE_MUTE;
        event.source = APP_EVENT_SOURCE_NONE;
        event.value = 0;
        event.tick = now;
        (void)AppEvent_Push(&event);
    }
}

uint8_t Button_SpecialFunctionsActive(void)
{
    return special_functions_active;
}

uint8_t Button_IsTapHeld(void)
{
    return (HAL_GPIO_ReadPin(TAP_GPIO_Port, TAP_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

uint8_t Button_IsMuteHeld(void)
{
    return Button_ReadPresetPressed(MUTE_BUTTON_INDEX);
}

void Button_ResetSpecialFunctions(void)
{
    special_functions_active = 0U;
    LED_SetSpecialFunctionIndicator(0U);
}


/* Tap and mute share a short combo window so pressing them together can step
 * banks without stealing normal tap-tempo or mute behavior. */
static uint32_t button_last_tap_tick = 0U;
static uint32_t button_last_mute_tick = 0U;

static uint8_t Button_QueueBankStepEvent(int8_t delta, uint32_t now)
{
    AppEvent_t event;

    if (delta == 0)
        return 0U;

    event.type = APP_EVENT_TYPE_BANK_STEP;
    event.source = APP_EVENT_BANK_STEP_MODE_FIRST_PRESET;
    event.value = (int16_t)delta;
    event.tick = now;
    (void)AppEvent_Push(&event);

    return 1U;
}

static uint8_t Button_StepBankDown(uint32_t now)
{
    return Button_QueueBankStepEvent(-1, now);
}

static uint8_t Button_StepBankUp(uint32_t now)
{
    return Button_QueueBankStepEvent(1, now);
}

void Button_MonitorInit(void)
{
#if BUTTON_LED_MONITOR_ENABLED
    LED_ClearButtonMonitorIndicators();
    printf("\r\nButton/LED monitor ready on USART3 @ 115200\r\n");
    printf("Press a footswitch: firmware will print the detected button number and light its mapped LED.\r\n");
    printf("Tap footswitch on PG15 is also reported in monitor output.\r\n");
    printf("Normal preset/button actions are bypassed while BUTTON_LED_MONITOR_ENABLED=1.\r\n");
#endif
}

uint8_t Button_HandleTapPress(uint32_t now)
{
    if (Button_IsMuteHeld() || ((now - button_last_mute_tick) < BANK_COMBO_WINDOW_MS)) {
        Button_StepBankDown(now);
        return 1U;
    }

    button_last_tap_tick = now;
    return 0U;
}

uint8_t Button_HandleMutePress(uint32_t now)
{
    button_last_mute_tick = now;

#if MUTE_BUTTON_DEBUG_ENABLED
    Button_DebugMuteState("press", now);
#endif

    if (Button_IsTapHeld() || ((now - button_last_tap_tick) < BANK_COMBO_WINDOW_MS)) {
        Button_StepBankUp(now);
#if MUTE_BUTTON_DEBUG_ENABLED
        Button_DebugMuteState("combo-up", now);
#endif
        return 1U;
    }

    mute_activation_pending = 1U;
#if MUTE_BUTTON_DEBUG_ENABLED
    Button_DebugMuteState("armed", now);
#endif
    return 0U;
}

void Button_CancelTapBankCombo(void)
{
    button_last_tap_tick = 0U;
    button_last_mute_tick = 0U;
    mute_activation_pending = 0U;
}

void Button_ProcessInterruptEvent(uint8_t index, uint8_t is_pressed, uint32_t now)
{
    if (index >= FOOTSWITCH_COUNT)
        return;

#if BUTTON_LED_MONITOR_ENABLED
    (void)now;
    if (is_pressed)
        Button_MonitorReportPress(index);
    return;
#endif

    Button_ProcessPresetEvent(index, is_pressed, now);
}

void Button_HandleInterrupt(uint16_t gpio_pin)
{
    int8_t index = Button_TryResolveIndex(gpio_pin);
    uint32_t now;
    uint8_t is_pressed;
    AppEvent_t event;

    if (index < 0) {
        return;
    }

    now = HAL_GetTick();
    if ((now - preset_button_event_tick[(uint8_t)index]) < FOOTSWITCH_DEBOUNCE_MS) {
#if MUTE_BUTTON_DEBUG_ENABLED
        if ((uint8_t)index == MUTE_BUTTON_INDEX)
            Button_DebugMuteState("debounce", now);
#endif
        return;
    }

    /* Non-mute preset buttons are wired as falling-edge EXTI only, so their
     * interrupt always means "press". Mute keeps both edges because release
     * timing matters for the delayed mute action. */
    if ((uint8_t)index == MUTE_BUTTON_INDEX) {
        is_pressed = Button_ReadPresetPressed((uint8_t)index);
#if MUTE_BUTTON_DEBUG_ENABLED
        Button_DebugMuteState(is_pressed ? "edge-down" : "edge-up", now);
#endif
    } else {
        is_pressed = 1U;
    }

    preset_button_event_tick[(uint8_t)index] = now;

    event.type = APP_EVENT_TYPE_FOOTSWITCH_EDGE;
    event.source = APP_EVENT_SOURCE_FOOTSWITCH((uint8_t)index);
    event.value = (int16_t)is_pressed;
    event.tick = now;
    (void)AppEvent_Push(&event);
}

void Button_ProcessPendingEvents(void) {
    uint32_t now = HAL_GetTick();
    AppEvent_t event;

    /* Mute keeps both EXTI edges, but a very fast tap can still lose the
     * release edge to debounce. Re-arm from polled GPIO so mute never stays
     * logically stuck pressed between taps. */
    if (preset_button_state[MUTE_BUTTON_INDEX]
        && (Button_ReadPresetPressed(MUTE_BUTTON_INDEX) == 0U)) {
        preset_button_state[MUTE_BUTTON_INDEX] = 0U;

#if MUTE_BUTTON_DEBUG_ENABLED
        Button_DebugMuteState("poll-up", now);
#endif

        if (mute_activation_pending) {
            mute_activation_pending = 0U;
            event.type = APP_EVENT_TYPE_PRESET_ACTIVATE_MUTE;
            event.source = APP_EVENT_SOURCE_NONE;
            event.value = 0;
            event.tick = now;
            (void)AppEvent_Push(&event);
#if MUTE_BUTTON_DEBUG_ENABLED
            Button_DebugMuteState("commit-up", now);
#endif
        }
    }

    /* If mute was not consumed by the TAP+MUTE bank-up combo inside the combo
     * window, commit it here as a normal mute press. */
    if (mute_activation_pending && ((now - button_last_mute_tick) >= BANK_COMBO_WINDOW_MS)) {
        mute_activation_pending = 0U;
        event.type = APP_EVENT_TYPE_PRESET_ACTIVATE_MUTE;
        event.source = APP_EVENT_SOURCE_NONE;
        event.value = 0;
        event.tick = now;
        (void)AppEvent_Push(&event);
#if MUTE_BUTTON_DEBUG_ENABLED
        Button_DebugMuteState("commit-400", now);
#endif
    }

    /* Falling-edge buttons stay logically pressed until the main loop sees
     * the pin released again. */
    for (uint8_t i = 0U; i < FOOTSWITCH_COUNT; ++i) {
        if (preset_button_state[i]
            && (Button_ReadPresetPressed(i) == 0U)) {
            preset_button_state[i] = 0U;
        }
    }
}
