#include "app_event.h"

#include "main.h"

#include <stdio.h>

#define APP_EVENT_DIAGNOSTICS_ENABLED 0U

static AppEvent_t app_event_queue[APP_EVENT_QUEUE_CAPACITY];
static volatile uint8_t app_event_read_index = 0U;
static volatile uint8_t app_event_write_index = 0U;
static volatile uint8_t app_event_count = 0U;
static volatile uint32_t app_event_dropped_count = 0U;

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