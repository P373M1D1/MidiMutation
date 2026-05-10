#include "app_event.h"

#include "main.h"

static AppEvent_t app_event_queue[APP_EVENT_QUEUE_CAPACITY];
static volatile uint8_t app_event_read_index = 0U;
static volatile uint8_t app_event_write_index = 0U;
static volatile uint8_t app_event_count = 0U;
static volatile uint32_t app_event_dropped_count = 0U;

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

uint32_t AppEvent_GetDroppedCount(void)
{
    return app_event_dropped_count;
}