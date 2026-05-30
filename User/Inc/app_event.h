#ifndef APP_EVENT_H
#define APP_EVENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_EVENT_QUEUE_CAPACITY 32U
#define APP_EVENT_SOURCE_FOOTSWITCH_BASE 16U
#define APP_EVENT_SOURCE_FOOTSWITCH_SLOTS 16U
#define APP_EVENT_SOURCE_FOOTSWITCH(index) ((uint8_t)(APP_EVENT_SOURCE_FOOTSWITCH_BASE + (uint8_t)(index)))
#define APP_EVENT_SOURCE_IS_FOOTSWITCH(source) \
        (((uint8_t)(source) >= APP_EVENT_SOURCE_FOOTSWITCH_BASE) \
            && ((uint8_t)(source) < (APP_EVENT_SOURCE_FOOTSWITCH_BASE + APP_EVENT_SOURCE_FOOTSWITCH_SLOTS)))
#define APP_EVENT_SOURCE_TO_FOOTSWITCH_INDEX(source) ((uint8_t)((uint8_t)(source) - APP_EVENT_SOURCE_FOOTSWITCH_BASE))
#define APP_EVENT_SAVE_KIND_RUNTIME_CONFIG 1U
#define APP_EVENT_SAVE_KIND_PRESETS 2U
#define APP_EVENT_SAVE_KIND_RUNTIME_STATE 3U
#define APP_EVENT_BANK_STEP_MODE_FIRST_PRESET 1U
#define APP_EVENT_BANK_STEP_MODE_ACTIVE_SLOT 2U

typedef enum {
    APP_EVENT_TYPE_NONE = 0,
    APP_EVENT_TYPE_TAP_PRESS,
    APP_EVENT_TYPE_ENCODER_TURN,
    APP_EVENT_TYPE_ENCODER_PRESS,
    APP_EVENT_TYPE_BUTTON_DOWN,
    APP_EVENT_TYPE_BUTTON_UP,
    APP_EVENT_TYPE_BANK_STEP,
    APP_EVENT_TYPE_PRESET_ACTIVATE,
    APP_EVENT_TYPE_PRESET_ACTIVATE_RANDOM,
    APP_EVENT_TYPE_PRESET_ACTIVATE_MUTE,
    APP_EVENT_TYPE_SPECIAL_FUNCTION_TOGGLE,
    APP_EVENT_TYPE_SCREENSAVER_WAKE,
    APP_EVENT_TYPE_SCREENSAVER_ACTIVITY,
    APP_EVENT_TYPE_TIMER_10MS,
    APP_EVENT_TYPE_TIMER_100MS,
    APP_EVENT_TYPE_TIMER_1000MS,
    APP_EVENT_TYPE_UI_TICK_100MS,
    APP_EVENT_TYPE_MIDI_MONITOR_CHANGED,
    APP_EVENT_TYPE_REDRAW_ACTIVE_DISPLAY,
    APP_EVENT_TYPE_REDRAW_MAIN_SCREEN,
    APP_EVENT_TYPE_SAVE_REQUEST,
    APP_EVENT_TYPE_SAVE_TIMEOUT,
} AppEventType_t;

typedef enum {
    APP_EVENT_SOURCE_NONE = 0,
    APP_EVENT_SOURCE_TAP,
    APP_EVENT_SOURCE_ENC1,
    APP_EVENT_SOURCE_ENC2,
    APP_EVENT_SOURCE_ENC3,
    APP_EVENT_SOURCE_EXPRESSION,
} AppEventSource_t;

typedef struct {
    AppEventType_t type;
    uint8_t source; /* event-specific source id, e.g. TAP or encoded footswitch index */
    int16_t value; /* event-specific payload, e.g. pressed state or turn delta */
    uint32_t tick;
} AppEvent_t;

void AppEvent_Init(void);
uint8_t AppEvent_Push(const AppEvent_t *event);
uint8_t AppEvent_Pop(AppEvent_t *event);
uint32_t AppEvent_GetDroppedCount(void);
void AppEvent_DiagnosticService(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_EVENT_H */