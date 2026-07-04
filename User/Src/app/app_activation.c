#include "app/app_activation.h"

#include "app_event.h"
#include "app/app_requests.h"
#include "app/app_state.h"
#include "app/app_ui.h"
#include "app/app_ui_events.h"
#include "app/app_preset_latency_diag.h"
#include "presets.h"
#include "stm32f4xx_hal.h"

#define APP_ACTIVATION_ENC2_UI_REFRESH_COALESCE_MS 300U
#define APP_ACTIVATION_ENC2_UI_REFRESH_IDLE_FLUSH_MS 80U

static void AppActivation_HandleBankStepEvent(int8_t delta, uint8_t step_mode);
static void AppActivation_HandlePresetActivateEvent(uint8_t preset_index, uint8_t source);
static void AppActivation_HandlePresetActivateRandomEvent(void);
static void AppActivation_HandlePresetActivateMuteEvent(void);
static uint8_t AppActivation_IsRandomOverlayActive(void);

static uint32_t app_activation_last_enc2_ui_refresh_tick = 0U;
static uint32_t app_activation_last_enc2_step_tick = 0U;
static uint8_t app_activation_enc2_ui_refresh_deferred = 0U;

static uint8_t AppActivation_IsRandomOverlayActive(void)
{
    return Presets_IsRandomPreset(AppState_GetActivePreset());
}

/* Applies preset and bank-step activation events from the central queue. */
uint8_t AppActivation_HandleEvent(const AppEvent_t *event)
{
    if (event == 0)
        return 0U;

    switch (event->type)
    {
    case APP_EVENT_TYPE_BANK_STEP:
        AppActivation_HandleBankStepEvent((int8_t)event->value, event->source);
        return 1U;

    case APP_EVENT_TYPE_PRESET_ACTIVATE:
        AppActivation_HandlePresetActivateEvent((uint8_t)event->value, event->source);
        return 1U;

    case APP_EVENT_TYPE_PRESET_ACTIVATE_RANDOM:
        AppActivation_HandlePresetActivateRandomEvent();
        return 1U;

    case APP_EVENT_TYPE_PRESET_ACTIVATE_MUTE:
        AppActivation_HandlePresetActivateMuteEvent();
        return 1U;

    default:
        return 0U;
    }
}

/* Flushes a deferred ENC2-driven UI refresh after input settles. */
void AppActivation_ServiceDeferredUiRefresh(uint32_t now)
{
    if (!app_activation_enc2_ui_refresh_deferred)
        return;

    if ((now - app_activation_last_enc2_step_tick) < APP_ACTIVATION_ENC2_UI_REFRESH_IDLE_FLUSH_MS)
        return;

    app_activation_enc2_ui_refresh_deferred = 0U;
    app_activation_last_enc2_ui_refresh_tick = now;
    AppUi_RequestLiveContentRefresh();
}

/* Steps the current bank and queues the matching preset activation. */
static void AppActivation_HandleBankStepEvent(int8_t delta, uint8_t step_mode)
{
    int16_t next_bank;
    uint8_t preset_slot = 0U;
    uint8_t current_bank = AppState_GetCurrentBank();
    uint8_t active_preset_index = AppState_GetActivePresetIndex();

    if (delta == 0)
        return;

    if (step_mode == APP_EVENT_BANK_STEP_MODE_ACTIVE_SLOT)
        preset_slot = (uint8_t)(active_preset_index % PRESETS_PER_BANK);

    next_bank = (int16_t)current_bank + (int16_t)delta;
    while (next_bank < 0)
        next_bank += (int16_t)PRESET_BANK_COUNT;

    while (next_bank >= (int16_t)PRESET_BANK_COUNT)
        next_bank -= (int16_t)PRESET_BANK_COUNT;

    AppState_SelectBank((uint8_t)next_bank);
    App_QueuePresetActivateEvent((uint8_t)((uint8_t)next_bank * PRESETS_PER_BANK + preset_slot));
}

/* Activates a specific preset index if it differs from the current selection. */
static void AppActivation_HandlePresetActivateEvent(uint8_t preset_index, uint8_t source)
{
    const Preset_t *preset;
    uint8_t was_random_overlay;
    uint8_t is_random_overlay;
    uint32_t now;

    if (preset_index >= Presets_Count())
        return;

    preset = Presets_Get(preset_index);
    if (AppState_IsActivePreset(preset))
        return;

    was_random_overlay = AppActivation_IsRandomOverlayActive();

    AppPresetLatencyDiag_OnActivationStart();
    AppUiEvents_PreparePresetActivation(0U);
    App_ActivatePreset(preset_index);
    AppPresetLatencyDiag_OnActivationApplied();

    is_random_overlay = AppActivation_IsRandomOverlayActive();

    if (!is_random_overlay)
        (void)AppUi_RandomSaveCancel();

    if (source == APP_EVENT_SOURCE_ENC2)
    {
        now = HAL_GetTick();
        app_activation_last_enc2_step_tick = now;

        if ((now - app_activation_last_enc2_ui_refresh_tick) >= APP_ACTIVATION_ENC2_UI_REFRESH_COALESCE_MS)
        {
            app_activation_last_enc2_ui_refresh_tick = now;
            app_activation_enc2_ui_refresh_deferred = 0U;
            AppUi_RequestLiveContentRefresh();
        }
        else
        {
            app_activation_enc2_ui_refresh_deferred = 1U;
        }
    }
    else if (was_random_overlay != is_random_overlay)
    {
        AppUi_RequestActiveDisplayRefresh();
    }
    else
    {
        AppUi_RequestLiveContentRefresh();
    }
}

/* Activates the random overlay preset and refreshes the live display. */
static void AppActivation_HandlePresetActivateRandomEvent(void)
{
    uint8_t was_random_overlay = AppActivation_IsRandomOverlayActive();

    AppPresetLatencyDiag_OnActivationStart();
    AppUiEvents_PreparePresetActivation(1U);
    Presets_ActivateRandom();
    AppPresetLatencyDiag_OnActivationApplied();

    if (!was_random_overlay)
        AppUi_RequestActiveDisplayRefresh();
    else
        AppUi_RequestLiveContentRefresh();
}

/* Activates the mute overlay preset and refreshes the live display. */
static void AppActivation_HandlePresetActivateMuteEvent(void)
{
    uint8_t was_random_overlay = AppActivation_IsRandomOverlayActive();

    AppPresetLatencyDiag_OnActivationStart();
    AppUiEvents_PreparePresetActivation(1U);
    Presets_ActivateMute();
    AppPresetLatencyDiag_OnActivationApplied();

    if (was_random_overlay)
        (void)AppUi_RandomSaveCancel();

    if (was_random_overlay)
        AppUi_RequestActiveDisplayRefresh();
    else
        AppUi_RequestLiveContentRefresh();
}
