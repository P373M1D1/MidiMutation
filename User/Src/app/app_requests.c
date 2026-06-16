#include "app/app_requests.h"

#include "app_event.h"
#include "stm32f4xx_hal.h"

static uint8_t app_ui_tick_100ms_event_pending = 0U;
static uint8_t app_redraw_main_screen_event_pending = 0U;
static uint8_t app_save_request_pending_mask = 0U;

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

/* Queues a preset activation request. */
void App_QueuePresetActivateEvent(uint8_t preset_index)
{
    App_QueuePresetActivateEventWithSource(preset_index,
                                           APP_EVENT_SOURCE_NONE,
                                           HAL_GetTick());
}

/* Queues a preset activation request with an explicit source and tick. */
void App_QueuePresetActivateEventWithSource(uint8_t preset_index,
                                            uint8_t source,
                                            uint32_t tick)
{
    AppEvent_t event;

    event.type = APP_EVENT_TYPE_PRESET_ACTIVATE;
    event.source = source;
    event.value = (int16_t)preset_index;
    event.tick = tick;
    (void)AppEvent_Push(&event);
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
