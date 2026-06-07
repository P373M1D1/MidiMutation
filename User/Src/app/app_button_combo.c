#include "app/app_button_combo.h"

#include "app_event.h"
#include "app/app_requests.h"
#include "app/app_state.h"
#include "presets.h"

#define APP_BUTTON_COMBO_WINDOW_MS 400U

static uint32_t app_button_combo_last_tap_tick = 0U;
static uint32_t app_button_combo_last_mute_tick = 0U;
static uint8_t app_button_combo_mute_activation_pending = 0U;
static uint8_t app_button_combo_mute_activation_deferred = 0U;

static void AppButtonCombo_Clear(void);
static void AppButtonCombo_RequestMuteActivateEvent(uint32_t now);
static uint8_t AppButtonCombo_TryQueueMuteActivateEvent(uint32_t now);
static uint8_t AppButtonCombo_CanBankStepFromMuteState(void);

uint8_t AppButtonCombo_HandleTapPress(uint32_t now, uint8_t mute_held)
{
    if (AppButtonCombo_CanBankStepFromMuteState()
        && (mute_held || ((now - app_button_combo_last_mute_tick) < APP_BUTTON_COMBO_WINDOW_MS)))
    {
        App_QueueBankStepEvent(-1, APP_EVENT_BANK_STEP_MODE_FIRST_PRESET);
        AppButtonCombo_Clear();
        return 1U;
    }

    app_button_combo_last_tap_tick = now;
    return 0U;
}

uint8_t AppButtonCombo_HandleMutePress(uint32_t now, uint8_t tap_held)
{
    app_button_combo_last_mute_tick = now;

    if (tap_held || ((now - app_button_combo_last_tap_tick) < APP_BUTTON_COMBO_WINDOW_MS))
    {
        App_QueueBankStepEvent(1, APP_EVENT_BANK_STEP_MODE_FIRST_PRESET);
        AppButtonCombo_Clear();
        return 1U;
    }

    app_button_combo_mute_activation_pending = 1U;
    return 0U;
}

uint8_t AppButtonCombo_HandleMuteRelease(uint32_t now)
{
    if (!app_button_combo_mute_activation_pending)
        return 0U;

    app_button_combo_mute_activation_pending = 0U;
    AppButtonCombo_RequestMuteActivateEvent(now);
    return 1U;
}

uint8_t AppButtonCombo_Service(uint32_t now)
{
    if (app_button_combo_mute_activation_deferred)
    {
        (void)AppButtonCombo_TryQueueMuteActivateEvent(now);
        return 0U;
    }

    if (!app_button_combo_mute_activation_pending)
        return 0U;

    if ((now - app_button_combo_last_mute_tick) < APP_BUTTON_COMBO_WINDOW_MS)
        return 0U;

    app_button_combo_mute_activation_pending = 0U;
    AppButtonCombo_RequestMuteActivateEvent(now);
    return 1U;
}

uint8_t AppButtonCombo_IsMuteActivationPending(void)
{
    return (uint8_t)(app_button_combo_mute_activation_pending || app_button_combo_mute_activation_deferred);
}

static void AppButtonCombo_Clear(void)
{
    app_button_combo_last_tap_tick = 0U;
    app_button_combo_last_mute_tick = 0U;
    app_button_combo_mute_activation_pending = 0U;
    app_button_combo_mute_activation_deferred = 0U;
}

static void AppButtonCombo_RequestMuteActivateEvent(uint32_t now)
{
    if (!AppButtonCombo_TryQueueMuteActivateEvent(now))
        app_button_combo_mute_activation_deferred = 1U;
}

static uint8_t AppButtonCombo_TryQueueMuteActivateEvent(uint32_t now)
{
    AppEvent_t event;

    event.type = APP_EVENT_TYPE_PRESET_ACTIVATE_MUTE;
    event.source = APP_EVENT_SOURCE_NONE;
    event.value = 0;
    event.tick = now;
    if (!AppEvent_Push(&event))
        return 0U;

    app_button_combo_mute_activation_deferred = 0U;
    return 1U;
}

static uint8_t AppButtonCombo_CanBankStepFromMuteState(void)
{
    return Presets_IsGlobalMutePreset(AppState_GetActivePreset()) ? 0U : 1U;
}
