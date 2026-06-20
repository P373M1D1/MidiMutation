#include "app_event.h"

#include "main.h"

#include <stdio.h>

#define APP_EVENT_DIAGNOSTICS_ENABLED 0U

static AppEvent_t app_event_queue[APP_EVENT_QUEUE_CAPACITY];
static volatile uint8_t app_event_read_index = 0U;
static volatile uint8_t app_event_write_index = 0U;
static volatile uint8_t app_event_count = 0U;
static volatile uint32_t app_event_dropped_count = 0U;

static int16_t AppEvent_ClampValue(int32_t value, int16_t min_value, int16_t max_value);

static int16_t AppEvent_ClampValue(int32_t value, int16_t min_value, int16_t max_value)
{
    if (value < (int32_t)min_value)
        return min_value;

    if (value > (int32_t)max_value)
        return max_value;

    return (int16_t)value;
}

/* Resets the central event queue to an empty state. */
void AppEvent_Init(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    app_event_read_index = 0U;
    app_event_write_index = 0U;
    app_event_count = 0U;
    app_event_dropped_count = 0U;
    if (primask == 0U)
        __enable_irq();
}

/* Pushes one event into the central queue if capacity remains. */
uint8_t AppEvent_Push(const AppEvent_t *event)
{
    uint32_t primask;

    if (!event || event->type == APP_EVENT_TYPE_NONE)
        return 0U;

    primask = __get_PRIMASK();
    __disable_irq();
    if (app_event_count >= APP_EVENT_QUEUE_CAPACITY)
    {
        app_event_dropped_count++;
        if (primask == 0U)
            __enable_irq();
        return 0U;
    }

    app_event_queue[app_event_write_index] = *event;
    app_event_write_index = (uint8_t)((app_event_write_index + 1U) % APP_EVENT_QUEUE_CAPACITY);
    app_event_count++;
    if (primask == 0U)
        __enable_irq();

    return 1U;
}

/* Adds a delta into an already-pending matching event without growing the queue. */
uint8_t AppEvent_CoalesceDelta(AppEventType_t type,
                               uint8_t source,
                               int16_t delta,
                               uint32_t tick,
                               int16_t min_value,
                               int16_t max_value)
{
    uint32_t primask;
    uint8_t index;
    uint8_t scanned_count;

    if (type == APP_EVENT_TYPE_NONE || delta == 0)
        return 0U;

    primask = __get_PRIMASK();
    __disable_irq();
    index = app_event_read_index;
    for (scanned_count = 0U; scanned_count < app_event_count; ++scanned_count)
    {
        if (app_event_queue[index].type == type
         && app_event_queue[index].source == source)
        {
            app_event_queue[index].value =
                AppEvent_ClampValue((int32_t)app_event_queue[index].value + (int32_t)delta,
                                    min_value,
                                    max_value);
            app_event_queue[index].tick = tick;
            if (primask == 0U)
                __enable_irq();
            return 1U;
        }

        index = (uint8_t)((index + 1U) % APP_EVENT_QUEUE_CAPACITY);
    }
    if (primask == 0U)
        __enable_irq();

    return 0U;
}

/* Replaces the payload of an already-pending matching event. */
uint8_t AppEvent_ReplacePending(AppEventType_t type,
                                 uint8_t source,
                                 int16_t value,
                                 uint32_t tick)
{
    uint32_t primask;
    uint8_t index;
    uint8_t scanned_count;

    if (type == APP_EVENT_TYPE_NONE)
        return 0U;

    primask = __get_PRIMASK();
    __disable_irq();
    index = app_event_read_index;
    for (scanned_count = 0U; scanned_count < app_event_count; ++scanned_count)
    {
        if (app_event_queue[index].type == type
         && app_event_queue[index].source == source)
        {
            app_event_queue[index].value = value;
            app_event_queue[index].tick = tick;
            if (primask == 0U)
                __enable_irq();
            return 1U;
        }

        index = (uint8_t)((index + 1U) % APP_EVENT_QUEUE_CAPACITY);
    }
    if (primask == 0U)
        __enable_irq();

    return 0U;
}

/* Pops the oldest queued event for foreground dispatch. */
uint8_t AppEvent_Pop(AppEvent_t *event)
{
    uint32_t primask;

    if (!event)
        return 0U;

    primask = __get_PRIMASK();
    __disable_irq();
    if (app_event_count == 0U)
    {
        if (primask == 0U)
            __enable_irq();
        return 0U;
    }

    *event = app_event_queue[app_event_read_index];
    app_event_read_index = (uint8_t)((app_event_read_index + 1U) % APP_EVENT_QUEUE_CAPACITY);
    app_event_count--;
    if (primask == 0U)
        __enable_irq();

    return 1U;
}

/* Returns the number of events dropped because the queue was full. */
uint32_t AppEvent_GetDroppedCount(void)
{
    return app_event_dropped_count;
}

/* Emits optional queue-drop diagnostics when enabled. */
void AppEvent_DiagnosticService(void)
{
#if APP_EVENT_DIAGNOSTICS_ENABLED
    static uint32_t last_reported_dropped_count = 0U;
    uint32_t dropped_count = AppEvent_GetDroppedCount();

    if (dropped_count <= last_reported_dropped_count)
        return;

    printf("APPQDIAG dropped=+%lu total=%lu cap=%u\r\n",
           (unsigned long)(dropped_count - last_reported_dropped_count),
           (unsigned long)dropped_count,
           (unsigned)APP_EVENT_QUEUE_CAPACITY);
    last_reported_dropped_count = dropped_count;
#endif
}
