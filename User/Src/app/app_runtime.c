#include "app/app_runtime.h"

#include "app/app_dispatch.h"
#include "app/app_input.h"
#include "app/app_metronome.h"
#include "app/app_requests.h"
#include "app/app_save_service.h"
#include "app/app_tempo.h"
#include "app/app_ui.h"
#include "app_event.h"
#include "bpm_functions.h"
#include "button_functions.h"
#include "display_functions.h"
#include "led_functions.h"
#include "midi_functions.h"
#include "runtime_config.h"

#include <stdio.h>

#define EXT_CLOCK_HOLDOVER_MIRROR_ENABLED 1U
#define APP_RUNTIME_TIMER_10MS_INTERVAL_MS 10U
#define APP_RUNTIME_TIMER_100MS_INTERVAL_MS 100U
#define APP_RUNTIME_TIMER_1000MS_INTERVAL_MS 1000U

static uint32_t app_runtime_queue_pump_cycles = 0U;
static uint32_t app_runtime_events_processed_total = 0U;
static uint32_t app_runtime_max_events_per_pump = 0U;
static uint32_t app_runtime_idle_cycles = 0U;
static uint32_t app_runtime_last_diagnostic_tick = 0U;
static uint32_t app_runtime_last_timer_10ms_tick = 0U;
static uint32_t app_runtime_last_timer_100ms_tick = 0U;
static uint32_t app_runtime_last_timer_1000ms_tick = 0U;
static uint32_t app_runtime_dispatch_time_sum_us = 0U;
static uint32_t app_runtime_dispatch_time_max_us = 0U;
static uint32_t app_runtime_dispatch_time_samples = 0U;
static uint32_t app_runtime_dispatch_over_1000us = 0U;
static uint32_t app_runtime_dispatch_over_5000us = 0U;
static uint32_t app_runtime_dispatch_window_time_sum_us = 0U;
static uint32_t app_runtime_dispatch_window_time_max_us = 0U;
static uint32_t app_runtime_dispatch_window_time_samples = 0U;
static uint32_t app_runtime_dispatch_window_over_1000us = 0U;
static uint32_t app_runtime_dispatch_window_over_5000us = 0U;

static uint8_t AppRuntime_IsFeedbackWindowActive(void);
static uint8_t AppRuntime_PumpEvents(void);
static void AppRuntime_ServiceTimebendPopup(const RuntimeConfigGlobal_t *global);
static void AppRuntime_SleepIfIdle(uint8_t queue_was_drained, uint8_t feedback_active);
static void AppRuntime_ScheduleTimerEvents(void);
static uint32_t AppRuntime_TimerDiffUs(uint32_t end, uint32_t start);

/* Runs one foreground iteration of the application queue pump and idle scheduler. */
void AppRuntime_ServiceForeground(void)
{
    const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();
    uint8_t timebend_live_enabled = 0U;
    uint8_t feedback_active;
    uint8_t queue_was_drained;

    /* Tap-tempo CC mode delegates timing outward, so suppress local realtime
     * MIDI clock generation to avoid sending two competing tempo authorities. */
    if (global && global->sync_style == RUNTIME_CONFIG_SYNC_STYLE_TAP_TEMPO_CC)
        MidiClockSetRealtimeOutputEnabled(0U);
    else
        MidiClockSetRealtimeOutputEnabled(1U);

    if (global)
    {
        timebend_live_enabled = (uint8_t)((global->live_enc2_mode == RUNTIME_CONFIG_LIVE_ENC2_MODE_TIMEBEND)
                                        || (global->expression_pedal_mode == RUNTIME_CONFIG_EXPRESSION_PEDAL_MODE_TIMEBEND));
    }

    MidiTimebendSetActive(timebend_live_enabled);
    AppRuntime_ServiceTimebendPopup(global);

    /* Drain deferred TIM2 compare work outside IRQ context so timing ISR paths
     * remain minimal while metronome/LED behavior still updates at loop speed. */
    AppMetronome_ServiceDeferredTimingWork();
    LED_ServiceDeferredTimingWork();

    AppRuntime_ScheduleTimerEvents();
    queue_was_drained = AppRuntime_PumpEvents();
    (void)AppUi_ServiceBeatSynchronousStatusStrip();

    feedback_active = AppRuntime_IsFeedbackWindowActive();
    if (feedback_active)
    {
        /* Keep beat-edge status updates responsive while preserving the
         * feedback window rule that defers broader UI redraw work. */
        (void)AppUi_ServiceBeatSynchronousStatusStrip();
        AppRuntime_SleepIfIdle(queue_was_drained, feedback_active);
        return;
    }

    AppRuntime_PumpEvents();
    (void)AppUi_ServiceBeatSynchronousStatusStrip();
    AppUi_ServiceRender();

    AppRuntime_SleepIfIdle(queue_was_drained, feedback_active);
}

/* Returns true while beat or metronome feedback is actively visible/audible. */
static uint8_t AppRuntime_IsFeedbackWindowActive(void)
{
    /* Beat pulses and metronome ticks are treated as the critical short-lived
     * feedback windows that can justify briefly deferring lower-priority work. */
    return (uint8_t)(AppMetronome_IsOutputActive() || LED_IsPulseActive());
}

/* Pumps queued events until the dispatcher reports the queue is empty. */
static uint8_t AppRuntime_PumpEvents(void)
{
    uint8_t processed_any = 0U;
    uint8_t processed_count;
    uint32_t start_us;
    uint32_t elapsed_us;

    app_runtime_queue_pump_cycles++;

    start_us = TIM2->CNT;
    processed_count = AppDispatch_ProcessPendingEvents();
    elapsed_us = AppRuntime_TimerDiffUs(TIM2->CNT, start_us);
    app_runtime_dispatch_time_sum_us += elapsed_us;
    app_runtime_dispatch_time_samples++;
    app_runtime_dispatch_window_time_sum_us += elapsed_us;
    app_runtime_dispatch_window_time_samples++;
    if (elapsed_us > app_runtime_dispatch_time_max_us)
        app_runtime_dispatch_time_max_us = elapsed_us;
    if (elapsed_us > app_runtime_dispatch_window_time_max_us)
        app_runtime_dispatch_window_time_max_us = elapsed_us;
    if (elapsed_us > 1000U)
    {
        app_runtime_dispatch_over_1000us++;
        app_runtime_dispatch_window_over_1000us++;
    }
    if (elapsed_us > 5000U)
    {
        app_runtime_dispatch_over_5000us++;
        app_runtime_dispatch_window_over_5000us++;
    }

    if (processed_count != 0U)
    {
        processed_any = 1U;
        app_runtime_events_processed_total += processed_count;
        if (processed_count > app_runtime_max_events_per_pump)
            app_runtime_max_events_per_pump = processed_count;
    }

    return processed_any;
}

static uint32_t AppRuntime_TimerDiffUs(uint32_t end, uint32_t start)
{
    return (end >= start)
        ? (end - start)
        : (UINT32_MAX - start + end + 1U);
}

/* Schedules coarse timer events from the foreground tick. */
static void AppRuntime_ScheduleTimerEvents(void)
{
    uint32_t now = HAL_GetTick();

    if (app_runtime_last_timer_10ms_tick == 0U)
        app_runtime_last_timer_10ms_tick = now;

    if (app_runtime_last_timer_100ms_tick == 0U)
        app_runtime_last_timer_100ms_tick = now;

    if (app_runtime_last_timer_1000ms_tick == 0U)
        app_runtime_last_timer_1000ms_tick = now;

    while ((uint32_t)(now - app_runtime_last_timer_10ms_tick) >= APP_RUNTIME_TIMER_10MS_INTERVAL_MS)
    {
        AppEvent_t event = { APP_EVENT_TYPE_TIMER_10MS, APP_EVENT_SOURCE_NONE, 0, now };

        app_runtime_last_timer_10ms_tick += APP_RUNTIME_TIMER_10MS_INTERVAL_MS;
        (void)AppEvent_Push(&event);
    }

    while ((uint32_t)(now - app_runtime_last_timer_100ms_tick) >= APP_RUNTIME_TIMER_100MS_INTERVAL_MS)
    {
        AppEvent_t event = { APP_EVENT_TYPE_TIMER_100MS, APP_EVENT_SOURCE_NONE, 0, now };

        app_runtime_last_timer_100ms_tick += APP_RUNTIME_TIMER_100MS_INTERVAL_MS;
        (void)AppEvent_Push(&event);
    }

    while ((uint32_t)(now - app_runtime_last_timer_1000ms_tick) >= APP_RUNTIME_TIMER_1000MS_INTERVAL_MS)
    {
        AppEvent_t event = { APP_EVENT_TYPE_TIMER_1000MS, APP_EVENT_SOURCE_NONE, 0, now };

        app_runtime_last_timer_1000ms_tick += APP_RUNTIME_TIMER_1000MS_INTERVAL_MS;
        (void)AppEvent_Push(&event);
    }
}

/* Shows or hides the live timebend overlay based on the current mode/state. */
static void AppRuntime_ServiceTimebendPopup(const RuntimeConfigGlobal_t *global)
{
    uint8_t should_show = 0U;

    if (global
        && ((global->live_enc2_mode == RUNTIME_CONFIG_LIVE_ENC2_MODE_TIMEBEND)
         || (global->expression_pedal_mode == RUNTIME_CONFIG_EXPRESSION_PEDAL_MODE_TIMEBEND))
        && MidiTimebendIsEngaged()
        && !Display_MenuIsActive()
        && !Display_PresetEditIsActive())
    {
        should_show = 1U;
    }

    if (should_show)
    {
        Display_ShowTimebendPopup();
    }
    else
    {
        Display_HideTimebendPopup(AppUi_GetCurrentDisplayPreset());
    }
}

/* Sleeps the CPU when no work was observed and no feedback window is active. */
static void AppRuntime_SleepIfIdle(uint8_t queue_was_drained, uint8_t feedback_active)
{
    if (queue_was_drained || feedback_active)
        return;

    app_runtime_idle_cycles++;
    __WFI();
}

/* Emits runtime queue and pump diagnostics. */
void AppRuntime_DiagnosticService(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t dispatch_avg_us = 0U;
    uint32_t dispatch_window_avg_us = 0U;
    static uint32_t last_dispatch_calls = 0U;
    static uint32_t last_dispatch_budget_hits = 0U;
    uint32_t dispatch_calls_delta = 0U;
    uint32_t dispatch_budget_hits_delta = 0U;
    AppDispatchDiagnostics_t dispatch_diagnostics = { 0U, 0U, 0U };

    if ((now - app_runtime_last_diagnostic_tick) < APP_RUNTIME_TIMER_1000MS_INTERVAL_MS)
        return;

    app_runtime_last_diagnostic_tick = now;

    AppDispatch_GetDiagnostics(&dispatch_diagnostics);
    if (app_runtime_dispatch_time_samples != 0U)
    {
        dispatch_avg_us = app_runtime_dispatch_time_sum_us / app_runtime_dispatch_time_samples;
    }
    if (app_runtime_dispatch_window_time_samples != 0U)
    {
        dispatch_window_avg_us = app_runtime_dispatch_window_time_sum_us
            / app_runtime_dispatch_window_time_samples;
    }
    dispatch_calls_delta = dispatch_diagnostics.calls - last_dispatch_calls;
    dispatch_budget_hits_delta = dispatch_diagnostics.budget_hits - last_dispatch_budget_hits;
    last_dispatch_calls = dispatch_diagnostics.calls;
    last_dispatch_budget_hits = dispatch_diagnostics.budget_hits;

    printf("RUNTIMEDIAG pumps=%lu events=%lu max=%lu idle=%lu queue=%u dispatch_avg_us=%lu dispatch_max_us=%lu dispatch_samples=%lu dispatch_over_1000us=%lu dispatch_over_5000us=%lu dispatch_win_avg_us=%lu dispatch_win_max_us=%lu dispatch_win_samples=%lu dispatch_win_over_1000us=%lu dispatch_win_over_5000us=%lu dispatch_calls=%lu dispatch_budget_hits=%lu dispatch_calls_delta=%lu dispatch_budget_hits_delta=%lu dispatch_max_events=%u\r\n",
           (unsigned long)app_runtime_queue_pump_cycles,
           (unsigned long)app_runtime_events_processed_total,
           (unsigned long)app_runtime_max_events_per_pump,
           (unsigned long)app_runtime_idle_cycles,
           (unsigned)APP_EVENT_QUEUE_CAPACITY,
           (unsigned long)dispatch_avg_us,
           (unsigned long)app_runtime_dispatch_time_max_us,
           (unsigned long)app_runtime_dispatch_time_samples,
           (unsigned long)app_runtime_dispatch_over_1000us,
           (unsigned long)app_runtime_dispatch_over_5000us,
           (unsigned long)dispatch_window_avg_us,
           (unsigned long)app_runtime_dispatch_window_time_max_us,
           (unsigned long)app_runtime_dispatch_window_time_samples,
           (unsigned long)app_runtime_dispatch_window_over_1000us,
           (unsigned long)app_runtime_dispatch_window_over_5000us,
           (unsigned long)dispatch_diagnostics.calls,
           (unsigned long)dispatch_diagnostics.budget_hits,
           (unsigned long)dispatch_calls_delta,
           (unsigned long)dispatch_budget_hits_delta,
           (unsigned)dispatch_diagnostics.max_events_per_call);

    {
        AppDispatchHandlerTimings_t handler_timings = {0U, 0U, 0U, 0U, 0U, 0U};
        AppUiRenderTimings_t render_timings = {0U, 0U, 0U, 0U, 0U, 0U, 0U};

        AppDispatch_GetHandlerTimings(&handler_timings);
        AppUi_GetRenderTimings(&render_timings);

        /** Emit worst-case execution time per dispatch handler and per display
         *  render path. Fields that stay at 0 were never exercised since
         *  power-on. Primary suspects for the observed 38 ms stall are:
         *  - hdl_act (preset activation)
         *  - hdl_ui  (encoder turn / UI event processing)
         *  - rnd_main / rnd_active / rnd_live (full display redraws)
         *  If any single field accounts for most of dispatch_max_us, that
         *  handler/path is the latency root cause to investigate next. */
        printf("LATENCYDIAG "
               "hdl_tmr=%lu hdl_tempo=%lu hdl_ui=%lu hdl_btn=%lu hdl_act=%lu hdl_sav=%lu "
               "rnd_beat=%lu rnd_main=%lu rnd_active=%lu rnd_live=%lu "
               "rnd_edit_mode=%lu rnd_edit_field=%lu rnd_status=%lu\r\n",
               (unsigned long)handler_timings.timer_max_us,
               (unsigned long)handler_timings.tempo_max_us,
               (unsigned long)handler_timings.ui_max_us,
               (unsigned long)handler_timings.button_max_us,
               (unsigned long)handler_timings.activation_max_us,
               (unsigned long)handler_timings.save_max_us,
               (unsigned long)render_timings.beat_max_us,
               (unsigned long)render_timings.main_max_us,
               (unsigned long)render_timings.active_max_us,
               (unsigned long)render_timings.live_max_us,
               (unsigned long)render_timings.edit_mode_max_us,
               (unsigned long)render_timings.edit_field_max_us,
               (unsigned long)render_timings.status_max_us);
    }

    app_runtime_dispatch_window_time_sum_us = 0U;
    app_runtime_dispatch_window_time_max_us = 0U;
    app_runtime_dispatch_window_time_samples = 0U;
    app_runtime_dispatch_window_over_1000us = 0U;
    app_runtime_dispatch_window_over_5000us = 0U;
}

void AppRuntime_GetPressureCounters(AppRuntimePressureCounters_t *counters)
{
    if (!counters)
        return;

    counters->dispatch_samples = app_runtime_dispatch_time_samples;
    counters->dispatch_over_1000us = app_runtime_dispatch_over_1000us;
    counters->dispatch_over_5000us = app_runtime_dispatch_over_5000us;
    counters->dispatch_max_us = app_runtime_dispatch_time_max_us;
}