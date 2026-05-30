#include "app/app_dispatch.h"

#include "app/app_activation.h"
#include "app/app_button_events.h"
#include "app/app_save_service.h"
#include "app/app_tempo.h"
#include "app/app_timer_events.h"
#include "app/app_ui_events.h"
#include "app_event.h"
#include "stm32f4xx_hal.h"

#define APP_DISPATCH_MAX_EVENTS_PER_CALL 8U

static uint32_t app_dispatch_calls = 0U;
static uint32_t app_dispatch_budget_hits = 0U;
static uint8_t app_dispatch_max_events_per_call = 0U;

/**
 * Lifetime worst-case dispatch times per handler bucket.
 * These are never cleared so they represent the worst event ever seen since
 * power-on. Useful for attributing the single most-expensive observed event.
 */
static uint32_t app_dispatch_timer_max_us = 0U;
static uint32_t app_dispatch_tempo_max_us = 0U;
static uint32_t app_dispatch_ui_max_us = 0U;
static uint32_t app_dispatch_button_max_us = 0U;
static uint32_t app_dispatch_activation_max_us = 0U;
static uint32_t app_dispatch_save_max_us = 0U;

/**
 * Wrap-safe 32-bit microsecond counter difference using TIM2 (1 MHz, 32-bit).
 * Identical in form to the helper in app_runtime.c but kept local to avoid
 * cross-module coupling for a one-liner.
 */
static uint32_t AppDispatch_TimerDiffUs(uint32_t end, uint32_t start)
{
    return (end >= start) ? (end - start) : (UINT32_MAX - start + end + 1U);
}

/* Drains the central event queue and hands each event to the first handler that
 * claims it. Returns the number of events processed. */
uint8_t AppDispatch_ProcessPendingEvents(void)
{
    AppEvent_t event;
    uint8_t processed_count = 0U;

    app_dispatch_calls++;

    while (processed_count < APP_DISPATCH_MAX_EVENTS_PER_CALL
        && AppEvent_Pop(&event))
    {
        uint32_t h_start;
        uint32_t h_elapsed;

        processed_count++;

        h_start = TIM2->CNT;
        if (AppTimerEvents_HandleEvent(&event))
        {
            h_elapsed = AppDispatch_TimerDiffUs(TIM2->CNT, h_start);
            if (h_elapsed > app_dispatch_timer_max_us)
                app_dispatch_timer_max_us = h_elapsed;
            continue;
        }
        h_elapsed = AppDispatch_TimerDiffUs(TIM2->CNT, h_start);
        if (h_elapsed > app_dispatch_timer_max_us)
            app_dispatch_timer_max_us = h_elapsed;

        h_start = TIM2->CNT;
        if (AppTempo_HandleEvent(&event))
        {
            h_elapsed = AppDispatch_TimerDiffUs(TIM2->CNT, h_start);
            if (h_elapsed > app_dispatch_tempo_max_us)
                app_dispatch_tempo_max_us = h_elapsed;
            continue;
        }

        h_start = TIM2->CNT;
        if (AppUiEvents_HandleEvent(&event))
        {
            h_elapsed = AppDispatch_TimerDiffUs(TIM2->CNT, h_start);
            if (h_elapsed > app_dispatch_ui_max_us)
                app_dispatch_ui_max_us = h_elapsed;
            continue;
        }

        h_start = TIM2->CNT;
        if (AppButtonEvents_HandleEvent(&event))
        {
            h_elapsed = AppDispatch_TimerDiffUs(TIM2->CNT, h_start);
            if (h_elapsed > app_dispatch_button_max_us)
                app_dispatch_button_max_us = h_elapsed;
            continue;
        }

        h_start = TIM2->CNT;
        if (AppActivation_HandleEvent(&event))
        {
            h_elapsed = AppDispatch_TimerDiffUs(TIM2->CNT, h_start);
            if (h_elapsed > app_dispatch_activation_max_us)
                app_dispatch_activation_max_us = h_elapsed;
            continue;
        }

        h_start = TIM2->CNT;
        (void)AppSaveService_HandleEvent(&event);
        h_elapsed = AppDispatch_TimerDiffUs(TIM2->CNT, h_start);
        if (h_elapsed > app_dispatch_save_max_us)
            app_dispatch_save_max_us = h_elapsed;
    }

    if (processed_count > app_dispatch_max_events_per_call)
        app_dispatch_max_events_per_call = processed_count;
    if (processed_count == APP_DISPATCH_MAX_EVENTS_PER_CALL)
        app_dispatch_budget_hits++;

    return processed_count;
}

void AppDispatch_GetDiagnostics(AppDispatchDiagnostics_t *diagnostics)
{
    if (!diagnostics)
        return;

    diagnostics->calls = app_dispatch_calls;
    diagnostics->budget_hits = app_dispatch_budget_hits;
    diagnostics->max_events_per_call = app_dispatch_max_events_per_call;
}

void AppDispatch_GetHandlerTimings(AppDispatchHandlerTimings_t *timings)
{
    if (!timings)
        return;

    timings->timer_max_us      = app_dispatch_timer_max_us;
    timings->tempo_max_us      = app_dispatch_tempo_max_us;
    timings->ui_max_us         = app_dispatch_ui_max_us;
    timings->button_max_us     = app_dispatch_button_max_us;
    timings->activation_max_us = app_dispatch_activation_max_us;
    timings->save_max_us       = app_dispatch_save_max_us;
}