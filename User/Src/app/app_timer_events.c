#include "app/app_timer_events.h"

#include "app/app_input.h"
#include "app/app_metronome.h"
#include "app/app_requests.h"
#include "app/app_save_service.h"
#include "app/app_state.h"
#include "app/app_dispatch.h"
#include "app/app_runtime.h"
#include "app/app_tempo.h"
#include "app/app_ui.h"

#include "bpm_functions.h"
#include "button_functions.h"
#include "display_functions.h"
#include "led_functions.h"
#include "midi/clock_engine.h"
#include "midi_functions.h"

#include <stdio.h>

static void AppTimerEvents_Handle10MsTick(void);
static void AppTimerEvents_Handle100MsTick(void);
static void AppTimerEvents_Handle1000MsTick(void);
static void AppTimerEvents_QueueSaveTimeoutEvent(uint32_t now);

#define APP_TIMER_EVENTS_DIAG_GUARD_TICK_MS 100U
#define APP_TIMER_EVENTS_DIAG_GUARD_COOLDOWN_MS 3000U
#define APP_TIMER_EVENTS_DIAG_GUARD_BUDGET_HITS_DELTA 2U
#define APP_TIMER_EVENTS_DIAG_GUARD_OVER_5000US_DELTA 1U
#define APP_TIMER_EVENTS_DIAG_GUARD_OVER_1000US_DELTA 3U
#define APP_TIMER_EVENTS_DIAG_GUARD_EXTEND_LOG_MIN_DECAY_TICKS 5U

#define APP_TIMER_EVENTS_DIAG_GUARD_COOLDOWN_TICKS \
    (APP_TIMER_EVENTS_DIAG_GUARD_COOLDOWN_MS / APP_TIMER_EVENTS_DIAG_GUARD_TICK_MS)

static uint8_t app_timer_events_save_timeout_pending = 0U;
static uint8_t app_timer_events_diagnostic_slot = 0U;
static uint8_t app_timer_events_clkdiag_period_decimator = 0U;
static uint8_t app_timer_events_clkdiag_state_valid = 0U;
static MidiSyncState_t app_timer_events_clkdiag_last_state = MIDI_SYNC_STATE_IDLE;
static uint8_t app_timer_events_diag_guard_cooldown_ticks = 0U;
static uint32_t app_timer_events_diag_guard_last_dispatch_samples = 0U;
static uint32_t app_timer_events_diag_guard_last_over_1000us = 0U;
static uint32_t app_timer_events_diag_guard_last_over_5000us = 0U;
static uint32_t app_timer_events_diag_guard_last_budget_hits = 0U;

/* Handles coarse periodic timer events published by the runtime scheduler. */
uint8_t AppTimerEvents_HandleEvent(const AppEvent_t *event)
{
    if (event == 0)
        return 0U;

    switch (event->type)
    {
    case APP_EVENT_TYPE_TIMER_10MS:
        AppTimerEvents_Handle10MsTick();
        return 1U;

    case APP_EVENT_TYPE_TIMER_100MS:
        AppTimerEvents_Handle100MsTick();
        return 1U;

    case APP_EVENT_TYPE_TIMER_1000MS:
        AppTimerEvents_Handle1000MsTick();
        return 1U;

    default:
        return 0U;
    }
}

static void AppTimerEvents_Handle10MsTick(void)
{
    ClockEngine_Service10ms();
    MidiInput_ServiceRealtimeRx();
    AppTempo_ExternalClockHoldoverMirrorService();
    MidiOutputSchedulerService();
    AppMetronome_Service();
    LED_Update();
    AppInput_ProcessPending();
    Button_ProcessPendingEvents();
    BPM_Service();

    AppUi_ServiceMenuPreviewHold(AppInput_Encoder2SwitchIsPressed());
}

static void AppTimerEvents_Handle100MsTick(void)
{
    uint32_t now = HAL_GetTick();
    AppRuntimePressureCounters_t pressure_counters = {0U, 0U, 0U, 0U};
    AppDispatchDiagnostics_t dispatch_diagnostics = {0U, 0U, 0U};
    uint32_t over_1000us_delta;
    uint32_t over_5000us_delta;
    uint32_t budget_hits_delta;
    uint8_t pressure_violation;
    uint8_t diagnostics_guard_was_active;
    uint8_t diagnostics_guard_active;
    uint8_t diagnostics_guard_extended;
    uint8_t diag_guard_reason_o1k;
    uint8_t diag_guard_reason_o5k;
    uint8_t diag_guard_reason_bh;
    uint8_t cooldown_ticks_before_update;

    App_QueueUiTick100MsEvent();

    if ((AppState_GetRuntimeStateSaveTick() != 0U && now >= AppState_GetRuntimeStateSaveTick())
     || AppSaveService_HasPendingWork())
    {
        AppTimerEvents_QueueSaveTimeoutEvent(now);
    }

    AppRuntime_GetPressureCounters(&pressure_counters);
    AppDispatch_GetDiagnostics(&dispatch_diagnostics);

    over_1000us_delta = pressure_counters.dispatch_over_1000us
        - app_timer_events_diag_guard_last_over_1000us;
    over_5000us_delta = pressure_counters.dispatch_over_5000us
        - app_timer_events_diag_guard_last_over_5000us;
    budget_hits_delta = dispatch_diagnostics.budget_hits
        - app_timer_events_diag_guard_last_budget_hits;

    app_timer_events_diag_guard_last_dispatch_samples = pressure_counters.dispatch_samples;
    app_timer_events_diag_guard_last_over_1000us = pressure_counters.dispatch_over_1000us;
    app_timer_events_diag_guard_last_over_5000us = pressure_counters.dispatch_over_5000us;
    app_timer_events_diag_guard_last_budget_hits = dispatch_diagnostics.budget_hits;

    pressure_violation = (uint8_t)((over_5000us_delta >= APP_TIMER_EVENTS_DIAG_GUARD_OVER_5000US_DELTA)
        || (over_1000us_delta >= APP_TIMER_EVENTS_DIAG_GUARD_OVER_1000US_DELTA)
        || (budget_hits_delta >= APP_TIMER_EVENTS_DIAG_GUARD_BUDGET_HITS_DELTA));

    diagnostics_guard_was_active = (app_timer_events_diag_guard_cooldown_ticks != 0U) ? 1U : 0U;
    cooldown_ticks_before_update = app_timer_events_diag_guard_cooldown_ticks;
    diagnostics_guard_extended = 0U;

    diag_guard_reason_o1k = (over_1000us_delta >= APP_TIMER_EVENTS_DIAG_GUARD_OVER_1000US_DELTA) ? 1U : 0U;
    diag_guard_reason_o5k = (over_5000us_delta >= APP_TIMER_EVENTS_DIAG_GUARD_OVER_5000US_DELTA) ? 1U : 0U;
    diag_guard_reason_bh = (budget_hits_delta >= APP_TIMER_EVENTS_DIAG_GUARD_BUDGET_HITS_DELTA) ? 1U : 0U;

    if (pressure_violation)
    {
        if (diagnostics_guard_was_active
            && cooldown_ticks_before_update <=
                (APP_TIMER_EVENTS_DIAG_GUARD_COOLDOWN_TICKS
                 - APP_TIMER_EVENTS_DIAG_GUARD_EXTEND_LOG_MIN_DECAY_TICKS))
            diagnostics_guard_extended = 1U;

        app_timer_events_diag_guard_cooldown_ticks = APP_TIMER_EVENTS_DIAG_GUARD_COOLDOWN_TICKS;
    }
    else if (app_timer_events_diag_guard_cooldown_ticks > 0U)
    {
        app_timer_events_diag_guard_cooldown_ticks--;
    }

    diagnostics_guard_active = (app_timer_events_diag_guard_cooldown_ticks != 0U) ? 1U : 0U;

    if (!diagnostics_guard_was_active && diagnostics_guard_active)
    {
        printf("DIAG_GUARD ENTER o1k=%lu o5k=%lu bh=%lu o1k_d=%lu o5k_d=%lu bh_d=%lu\r\n",
               (unsigned long)diag_guard_reason_o1k,
               (unsigned long)diag_guard_reason_o5k,
               (unsigned long)diag_guard_reason_bh,
               (unsigned long)over_1000us_delta,
               (unsigned long)over_5000us_delta,
               (unsigned long)budget_hits_delta);
    }
    else if (diagnostics_guard_extended)
    {
        printf("DIAG_GUARD EXTENDED o1k=%lu o5k=%lu bh=%lu o1k_d=%lu o5k_d=%lu bh_d=%lu\r\n",
               (unsigned long)diag_guard_reason_o1k,
               (unsigned long)diag_guard_reason_o5k,
               (unsigned long)diag_guard_reason_bh,
               (unsigned long)over_1000us_delta,
               (unsigned long)over_5000us_delta,
               (unsigned long)budget_hits_delta);
    }
    else if (diagnostics_guard_was_active && !diagnostics_guard_active)
    {
        printf("DIAG_GUARD EXIT\r\n");
    }

    /* Spread heavy UART diagnostic prints across the 1s window so one loop
     * iteration does not block for multiple long printf calls back-to-back. */
    switch (app_timer_events_diagnostic_slot)
    {
    case 0U:
        if (!diagnostics_guard_active)
            AppRuntime_DiagnosticService();
        break;
    case 2U:
        if (!diagnostics_guard_active)
            AppEvent_DiagnosticService();
        break;
    case 4U:
        {
            MidiSyncState_t sync_state = ClockEngine_GetSyncState();
            uint8_t emit_clkdiag = 0U;

            if (!app_timer_events_clkdiag_state_valid
             || sync_state != app_timer_events_clkdiag_last_state)
            {
                emit_clkdiag = 1U;
                app_timer_events_clkdiag_state_valid = 1U;
                app_timer_events_clkdiag_last_state = sync_state;
                app_timer_events_clkdiag_period_decimator = 5U;
            }
            else if (!diagnostics_guard_active
                  && app_timer_events_clkdiag_period_decimator == 0U)
            {
                emit_clkdiag = 1U;
                app_timer_events_clkdiag_period_decimator = 5U;
            }
            else if (!diagnostics_guard_active)
            {
                app_timer_events_clkdiag_period_decimator--;
            }

            if (emit_clkdiag)
            {
                /* CLKDIAG lines are very large. Emit on state changes and a
                 * slow cadence so transition insight is preserved while UART
                 * burst stalls stay bounded. */
                MidiClockDiagnosticService();
            }
        }
        break;
    case 6U:
        if (!diagnostics_guard_active)
            Display_BpmDiagnosticService();
        break;
    case 8U:
        if (!diagnostics_guard_active)
            AppMetronome_DiagnosticService();
        break;
    default:
        break;
    }

    app_timer_events_diagnostic_slot++;
    if (app_timer_events_diagnostic_slot >= 10U)
        app_timer_events_diagnostic_slot = 0U;
}

void AppTimerEvents_AcknowledgeSaveTimeoutEvent(void)
{
    app_timer_events_save_timeout_pending = 0U;
}

static void AppTimerEvents_QueueSaveTimeoutEvent(uint32_t now)
{
    AppEvent_t save_timeout_event;

    if (app_timer_events_save_timeout_pending)
        return;

    save_timeout_event.type = APP_EVENT_TYPE_SAVE_TIMEOUT;
    save_timeout_event.source = APP_EVENT_SOURCE_NONE;
    save_timeout_event.value = 0;
    save_timeout_event.tick = now;
    if (AppEvent_Push(&save_timeout_event))
        app_timer_events_save_timeout_pending = 1U;
}

static void AppTimerEvents_Handle1000MsTick(void)
{
    /* Diagnostics are now scheduled in staggered 100 ms slots. */
}