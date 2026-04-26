#include "bpm_functions.h"
#include "button_functions.h"
#include "main.h"
#include "presets.h"
#include "display_functions.h"
#include "stm32f4xx_hal.h"

extern volatile uint16_t  g_bpm;
extern const Preset_t    *active_preset;

#define FOOTSWITCH_COUNT 11U
#define RANDOM_BUTTON_INDEX 8U
#define SPECIAL_FUNCTION_BUTTON_INDEX 9U
#define MUTE_BUTTON_INDEX 10U
#define FOOTSWITCH_DEBOUNCE_MS 20U
#define BANK_COMBO_WINDOW_MS 400U
#define BUTTON_EVENT_QUEUE_DEPTH 4U

static uint8_t preset_button_state[FOOTSWITCH_COUNT] = {0U};
static volatile uint8_t preset_button_event_queue[FOOTSWITCH_COUNT][BUTTON_EVENT_QUEUE_DEPTH] = {{0U}};
static volatile uint8_t preset_button_event_read_index[FOOTSWITCH_COUNT] = {0U};
static volatile uint8_t preset_button_event_write_index[FOOTSWITCH_COUNT] = {0U};
static volatile uint8_t preset_button_event_count[FOOTSWITCH_COUNT] = {0U};
static volatile uint8_t preset_button_action_pending[FOOTSWITCH_COUNT] = {0U};
static uint32_t preset_button_event_tick[FOOTSWITCH_COUNT] = {0U};
static uint8_t special_functions_active = 0U;
static uint8_t mute_activation_pending = 0U;
static volatile uint8_t tap_button_action_pending = 0U;

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

static uint8_t Button_ReadPresetPressed(uint8_t index)
{
    return (HAL_GPIO_ReadPin(PRESET_BTN_GPIO_Port, preset_button_pins[index]) == GPIO_PIN_RESET) ? 1U : 0U;
}

static int8_t Button_TryResolveIndex(uint16_t gpio_pin)
{
    for (uint8_t i = 0U; i < FOOTSWITCH_COUNT; ++i) {
        if (preset_button_pins[i] == gpio_pin) {
            return (int8_t)i;
        }
    }

    return -1;
}

static void Button_ProcessPresetEvent(uint8_t index, uint8_t is_pressed, uint32_t now)
{
    if (is_pressed == preset_button_state[index]) {
        if (is_pressed) {
            preset_button_action_pending[index] = 0U;
        }
        return;
    }

    preset_button_state[index] = is_pressed;

    if (is_pressed) {
        Display_ScreensaverDismiss();
        Display_ScreensaverActivity();

        if (index == RANDOM_BUTTON_INDEX) {
            activateRandom();
        } else if (index == SPECIAL_FUNCTION_BUTTON_INDEX) {
            if (special_functions_active == 0U) {
                special_functions_active = 1U;
                activateSpecialFunctions();
            } else {
                special_functions_active = 0U;
                deactivateSpecialFunctions();
            }
        } else if (index == MUTE_BUTTON_INDEX) {
            if (Button_HandleMutePress(now)) {
                Button_CancelTapBankCombo();
                Display_DrawMainScreen(active_preset ? active_preset : Presets_Get(current_bank * PRESETS_PER_BANK), g_bpm);
            }
        } else {
            App_ActivatePreset(current_bank * PRESETS_PER_BANK + index);
        }

        preset_button_action_pending[index] = 0U;
    } else if ((index == MUTE_BUTTON_INDEX) && mute_activation_pending) {
        mute_activation_pending = 0U;
        activateMute();
    }
}

uint8_t Button_ActionPending(uint8_t index)
{
    if (index >= FOOTSWITCH_COUNT) {
        return 0U;
    }

    return preset_button_action_pending[index];
}

uint8_t Button_TapActionPending(void)
{
    return tap_button_action_pending;
}

void Button_SetTapActionPending(uint8_t is_pending)
{
    tap_button_action_pending = is_pending ? 1U : 0U;
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
}


// --- Bank switching state ---
volatile uint8_t current_bank = 0U;
static uint32_t button_last_tap_tick = 0U;
static uint32_t button_last_mute_tick = 0U;

static uint8_t Button_StepBankDown(void)
{
    if (current_bank == 0U) {
        current_bank = PRESET_BANK_COUNT - 1U;
    } else {
        current_bank--;
    }

    App_ActivatePreset(current_bank * PRESETS_PER_BANK);

    return 1U;
}

static uint8_t Button_StepBankUp(void)
{
    current_bank = (current_bank + 1U) % PRESET_BANK_COUNT;
    App_ActivatePreset(current_bank * PRESETS_PER_BANK);
    return 1U;
}

uint8_t Button_HandleTapPress(uint32_t now)
{
    if (Button_IsMuteHeld() || ((now - button_last_mute_tick) < BANK_COMBO_WINDOW_MS)) {
        Button_StepBankDown();
        return 1U;
    }

    button_last_tap_tick = now;
    return 0U;
}

uint8_t Button_HandleMutePress(uint32_t now)
{
    button_last_mute_tick = now;

    if (Button_IsTapHeld() || ((now - button_last_tap_tick) < BANK_COMBO_WINDOW_MS)) {
        Button_StepBankUp();
        return 1U;
    }

    mute_activation_pending = 1U;
    return 0U;
}

void Button_CancelTapBankCombo(void)
{
    button_last_tap_tick = 0U;
    button_last_mute_tick = 0U;
    mute_activation_pending = 0U;
}

void Button_HandleInterrupt(uint16_t gpio_pin)
{
    int8_t index = Button_TryResolveIndex(gpio_pin);
    uint8_t queue_index;
    uint32_t now;
    uint8_t is_pressed;

    if (index < 0) {
        return;
    }

    now = HAL_GetTick();
    if ((now - preset_button_event_tick[(uint8_t)index]) < FOOTSWITCH_DEBOUNCE_MS) {
        return;
    }

    if ((uint8_t)index == MUTE_BUTTON_INDEX) {
        is_pressed = Button_ReadPresetPressed((uint8_t)index);
    } else {
        is_pressed = 1U;
    }

    preset_button_event_tick[(uint8_t)index] = now;

    if (preset_button_event_count[(uint8_t)index] < BUTTON_EVENT_QUEUE_DEPTH) {
        queue_index = preset_button_event_write_index[(uint8_t)index];
        preset_button_event_queue[(uint8_t)index][queue_index] = is_pressed;
        preset_button_event_write_index[(uint8_t)index] = (uint8_t)((queue_index + 1U) % BUTTON_EVENT_QUEUE_DEPTH);
        preset_button_event_count[(uint8_t)index]++;
    }

    if (is_pressed) {
        preset_button_action_pending[(uint8_t)index] = 1U;
    }

    if ((uint8_t)index == MUTE_BUTTON_INDEX && is_pressed) {
        button_last_mute_tick = now;
    }
}

void Button_ProcessPendingEvents(void) {
    uint32_t now = HAL_GetTick();

    if (mute_activation_pending && ((now - button_last_mute_tick) >= BANK_COMBO_WINDOW_MS)) {
        mute_activation_pending = 0U;
        activateMute();
    }

    for (uint8_t i = 0U; i < FOOTSWITCH_COUNT; ++i) {
        if ((i != MUTE_BUTTON_INDEX)
            && preset_button_state[i]
            && (Button_ReadPresetPressed(i) == 0U)) {
            preset_button_state[i] = 0U;
        }
    }

    for (uint8_t i = 0U; i < FOOTSWITCH_COUNT; ++i) {
        uint32_t primask;
        uint8_t queued_events;

        for (;;) {
            uint8_t is_pressed;
            uint8_t read_index;

            primask = __get_PRIMASK();
            __disable_irq();
            queued_events = preset_button_event_count[i];
            if (queued_events != 0U) {
                read_index = preset_button_event_read_index[i];
                is_pressed = preset_button_event_queue[i][read_index];
                preset_button_event_read_index[i] = (uint8_t)((read_index + 1U) % BUTTON_EVENT_QUEUE_DEPTH);
                preset_button_event_count[i] = (uint8_t)(queued_events - 1U);
            }
            if (primask == 0U) {
                __enable_irq();
            }

            if (queued_events == 0U) {
                break;
            }

            Button_ProcessPresetEvent(i, is_pressed, now);
        }
    }
}
