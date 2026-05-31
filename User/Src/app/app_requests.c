#include "app/app_requests.h"

#include "app_event.h"
#include "stm32f4xx_hal.h"

static uint8_t app_screensaver_wake_event_pending = 0U;
static uint8_t app_screensaver_activity_event_pending = 0U;
static uint8_t app_ui_tick_100ms_event_pending = 0U;
static uint8_t app_preset_activate_event_pending = 0U;
static uint8_t app_redraw_main_screen_event_pending = 0U;
static uint8_t app_save_request_pending_mask = 0U;
static uint8_t app_pending_preset_activate_index = 0U;

/* Maps each save kind to its pending-mask bit. */
static uint8_t AppRequest_SaveMaskForKind(uint8_t save_kind)
{
    switch (save_kind)
    {
    case APP_EVENT_SAVE_KIND_RUNTIME_CONFIG:
        return 0x01U;

    case APP_EVENT_SAVE_KIND_PRESETS:
        return 0x02U;

    case APP_EVENT_SAVE_KIND_RUNTIME_STATE:
        return 0x04U;

    default:
        return 0U;
    }
}

/* Queues an encoder press event for deferred handling. */
void App_QueueEncoderPressEvent(uint8_t press_mask, uint32_t tick)
{
    AppEvent_t event;

    if (press_mask == 0U)
        return;

    event.type = APP_EVENT_TYPE_ENCODER_PRESS;
    event.source = APP_EVENT_SOURCE_NONE;
    event.value = (int16_t)press_mask;
    event.tick = tick;
    (void)AppEvent_Push(&event);
}

/* Queues an encoder turn event for deferred handling. */
void App_QueueEncoderTurnEvent(uint8_t encoder_source, int8_t delta, uint32_t tick)
{
    AppEvent_t event;

    if (delta == 0)
        return;

    event.type = APP_EVENT_TYPE_ENCODER_TURN;
    event.source = encoder_source;
    event.value = (int16_t)delta;
    event.tick = tick;
    (void)AppEvent_Push(&event);
}

/* Queues a bank-step request using the current tick. */
void App_QueueBankStepEvent(int8_t delta, uint8_t step_mode)
{
    AppEvent_t event;

    if (delta == 0)
        return;

    event.type = APP_EVENT_TYPE_BANK_STEP;
    event.source = step_mode;
    event.value = (int16_t)delta;
    event.tick = HAL_GetTick();
    (void)AppEvent_Push(&event);
}

/* Queues a preset activation while coalescing repeated requests. */
void App_QueuePresetActivateEvent(uint8_t preset_index)
{
    AppEvent_t event;

    app_pending_preset_activate_index = preset_index;
    if (app_preset_activate_event_pending)
        return;

    event.type = APP_EVENT_TYPE_PRESET_ACTIVATE;
    event.source = APP_EVENT_SOURCE_NONE;
    event.value = 0;
    event.tick = HAL_GetTick();
    if (AppEvent_Push(&event))
        app_preset_activate_event_pending = 1U;
}

/* Returns the coalesced preset activation index to the activation handler. */
uint8_t App_TakePendingPresetActivate(uint8_t *preset_index)
{
    if (!app_preset_activate_event_pending)
        return 0U;

    app_preset_activate_event_pending = 0U;
    if (preset_index)
        *preset_index = app_pending_preset_activate_index;

    return 1U;
}

/* Queues a one-shot screensaver wake request. */
void App_QueueScreensaverWakeEvent(void)
{
    AppEvent_t event;

    if (app_screensaver_wake_event_pending)
        return;

    event.type = APP_EVENT_TYPE_SCREENSAVER_WAKE;
    event.source = APP_EVENT_SOURCE_NONE;
    event.value = 0;
    event.tick = HAL_GetTick();
    if (AppEvent_Push(&event))
        app_screensaver_wake_event_pending = 1U;
}

/* Queues a screensaver activity pulse while suppressing duplicates. */
void App_QueueScreensaverActivityEvent(void)
{
    AppEvent_t event;

    if (app_screensaver_wake_event_pending || app_screensaver_activity_event_pending)
        return;

    event.type = APP_EVENT_TYPE_SCREENSAVER_ACTIVITY;
    event.source = APP_EVENT_SOURCE_NONE;
    event.value = 0;
    event.tick = HAL_GetTick();
    if (AppEvent_Push(&event))
        app_screensaver_activity_event_pending = 1U;
}

/* Clears the pending screensaver wake/activity flags after dispatch. */
void App_AcknowledgeScreensaverWakeEvent(void)
{
    app_screensaver_wake_event_pending = 0U;
    app_screensaver_activity_event_pending = 0U;
}

/* Clears the pending screensaver activity flag after dispatch. */
void App_AcknowledgeScreensaverActivityEvent(void)
{
    app_screensaver_activity_event_pending = 0U;
}

/* Queues the coalesced 100 ms UI tick. */
void App_QueueUiTick100MsEvent(void)
{
    AppEvent_t event;

    if (app_ui_tick_100ms_event_pending)
        return;

    event.type = APP_EVENT_TYPE_UI_TICK_100MS;
    event.source = APP_EVENT_SOURCE_NONE;
    event.value = 0;
    event.tick = HAL_GetTick();
    if (AppEvent_Push(&event))
        app_ui_tick_100ms_event_pending = 1U;
}

/* Clears the 100 ms UI tick pending flag after dispatch. */
void App_AcknowledgeUiTick100MsEvent(void)
{
    app_ui_tick_100ms_event_pending = 0U;
}

/* Queues a one-shot main-screen redraw request. */
void App_QueueRedrawMainScreenEvent(void)
{
    AppEvent_t event;

    if (app_redraw_main_screen_event_pending)
        return;

    event.type = APP_EVENT_TYPE_REDRAW_MAIN_SCREEN;
    event.source = APP_EVENT_SOURCE_NONE;
    event.value = 0;
    event.tick = HAL_GetTick();
    if (AppEvent_Push(&event))
        app_redraw_main_screen_event_pending = 1U;
}

/* Clears the pending main-screen redraw flag after dispatch. */
void App_AcknowledgeRedrawMainScreenEvent(void)
{
    app_redraw_main_screen_event_pending = 0U;
}

/* Queues a save request and coalesces duplicates by save kind. */
void App_QueueSaveRequestEvent(uint8_t save_kind)
{
    AppEvent_t event;
    uint8_t pending_mask;

    if (save_kind == 0U)
        return;

    pending_mask = AppRequest_SaveMaskForKind(save_kind);
    if (pending_mask == 0U || (app_save_request_pending_mask & pending_mask) != 0U)
        return;

    event.type = APP_EVENT_TYPE_SAVE_REQUEST;
    event.source = save_kind;
    event.value = 0;
    event.tick = HAL_GetTick();
    if (AppEvent_Push(&event))
        app_save_request_pending_mask |= pending_mask;
}

/* Clears the pending save request bit for one save kind. */
void App_AcknowledgeSaveRequestEvent(uint8_t save_kind)
{
    app_save_request_pending_mask &= (uint8_t)~AppRequest_SaveMaskForKind(save_kind);
}