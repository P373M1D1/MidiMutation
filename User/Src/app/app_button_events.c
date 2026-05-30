#include "app/app_button_events.h"

#include "app/app_requests.h"
#include "app/app_special_functions.h"
#include "app/app_state.h"
#include "app/app_ui.h"
#include "button_functions.h"
#include "display_functions.h"

#define APP_BUTTON_EVENTS_RANDOM_BUTTON_INDEX 8U
#define APP_BUTTON_EVENTS_SPECIAL_FUNCTION_BUTTON_INDEX 9U

static void AppButtonEvents_HandleFootswitchEdge(const AppEvent_t *event);
static void AppButtonEvents_HandleFootswitchPress(uint8_t index, uint32_t now);
static void AppButtonEvents_PushSimpleEvent(AppEventType_t type, int16_t value, uint32_t tick);

uint8_t AppButtonEvents_HandleEvent(const AppEvent_t *event)
{
    if (event == 0)
        return 0U;

    if (event->type != APP_EVENT_TYPE_BUTTON_DOWN && event->type != APP_EVENT_TYPE_BUTTON_UP)
        return 0U;

    AppButtonEvents_HandleFootswitchEdge(event);
    return 1U;
}

static void AppButtonEvents_HandleFootswitchEdge(const AppEvent_t *event)
{
    uint8_t index;

    if (event == 0)
        return;

    if (!APP_EVENT_SOURCE_IS_FOOTSWITCH(event->source))
        return;

    index = APP_EVENT_SOURCE_TO_FOOTSWITCH_INDEX(event->source);

    if (!Button_ProcessInterruptEvent(index, (uint8_t)(event->type == APP_EVENT_TYPE_BUTTON_DOWN ? 1U : 0U), event->tick))
        return;

    if (event->type == APP_EVENT_TYPE_BUTTON_UP)
        return;

    AppButtonEvents_HandleFootswitchPress(index, event->tick);
}

static void AppButtonEvents_HandleFootswitchPress(uint8_t index, uint32_t now)
{
    uint8_t target_preset_index;

    App_QueueScreensaverWakeEvent();

    if (index == APP_BUTTON_EVENTS_RANDOM_BUTTON_INDEX)
    {
        AppButtonEvents_PushSimpleEvent(APP_EVENT_TYPE_PRESET_ACTIVATE_RANDOM, 0, now);
        return;
    }

    if (index == APP_BUTTON_EVENTS_SPECIAL_FUNCTION_BUTTON_INDEX)
    {
        AppButtonEvents_PushSimpleEvent(APP_EVENT_TYPE_SPECIAL_FUNCTION_TOGGLE,
                                        (int16_t)AppSpecialFunctions_Toggle(),
                                        now);
        return;
    }

    target_preset_index = (uint8_t)(AppState_GetCurrentBank() * PRESETS_PER_BANK + index);

    /* In preset edit mode, pressing the already-active preset footswitch now
     * intentionally re-transmits the current preset payload without leaving
     * edit mode or depending on ENC2's learn control. */
    if (Display_PresetEditIsActive() && target_preset_index == AppState_GetActivePresetIndex())
    {
        (void)AppUi_PresetEditSendCurrentPreset();
        return;
    }

    App_QueuePresetActivateEvent(target_preset_index);
}

static void AppButtonEvents_PushSimpleEvent(AppEventType_t type, int16_t value, uint32_t tick)
{
    AppEvent_t event;

    event.type = type;
    event.source = APP_EVENT_SOURCE_NONE;
    event.value = value;
    event.tick = tick;
    (void)AppEvent_Push(&event);
}