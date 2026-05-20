#include "app/app_activation.h"

#include "app_event.h"
#include "app/app_requests.h"
#include "app/app_state.h"
#include "app/app_ui.h"
#include "app/app_ui_events.h"
#include "presets.h"

static void AppActivation_HandleBankStepEvent(int8_t delta, uint8_t step_mode);
static void AppActivation_HandlePresetActivateEvent(uint8_t preset_index);
static void AppActivation_HandlePresetActivateRandomEvent(void);
static void AppActivation_HandlePresetActivateMuteEvent(void);

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
        AppActivation_HandlePresetActivateEvent((uint8_t)event->value);
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

static void AppActivation_HandlePresetActivateEvent(uint8_t preset_index)
{
    const Preset_t *preset;

    (void)App_TakePendingPresetActivate(&preset_index);

    if (preset_index >= Presets_Count())
        return;

    preset = Presets_Get(preset_index);
    if (AppState_IsActivePreset(preset))
        return;

    AppUiEvents_PreparePresetActivation(0U);
    App_ActivatePreset(preset_index);
    AppUi_RequestLiveContentRefresh();
}

static void AppActivation_HandlePresetActivateRandomEvent(void)
{
    AppUiEvents_PreparePresetActivation(1U);
    Presets_ActivateRandom();
    AppUi_RequestLiveContentRefresh();
}

static void AppActivation_HandlePresetActivateMuteEvent(void)
{
    AppUiEvents_PreparePresetActivation(1U);
    Presets_ActivateMute();
    AppUi_RequestLiveContentRefresh();
}