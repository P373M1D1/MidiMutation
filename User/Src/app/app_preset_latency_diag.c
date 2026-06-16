#include "app/app_preset_latency_diag.h"

#include "stm32f4xx_hal.h"

#include <stdio.h>

#define APP_PRESET_LATENCY_DIAG_ENABLED 1U
#define APP_PRESET_LATENCY_KIND_NONE    0U
#define APP_PRESET_LATENCY_KIND_PRESET  1U
#define APP_PRESET_LATENCY_KIND_RANDOM  2U
#define APP_PRESET_LATENCY_KIND_MUTE    3U
#define APP_PRESET_LATENCY_KIND_BYPASS  4U
#define APP_PRESET_LATENCY_KIND_ENC2    5U

typedef struct
{
    uint8_t active;
    uint8_t kind;
    uint8_t button_index;
    uint8_t preset_index;
    uint8_t activation_seen;
    uint8_t led_seen;
    int8_t source_delta;
    uint32_t press_ms;
    uint32_t press_us;
    uint32_t activation_us;
    uint32_t led_us;
} AppPresetLatencyDiagState_t;

static AppPresetLatencyDiagState_t app_preset_latency_diag_state = {0U};

static uint32_t AppPresetLatencyDiag_NowUs(void);
static uint32_t AppPresetLatencyDiag_DiffUs(uint32_t end, uint32_t start);
static void AppPresetLatencyDiag_Start(uint8_t kind, uint8_t button_index, uint8_t preset_index, uint32_t tick_ms);
static void AppPresetLatencyDiag_SetSourceDelta(int8_t delta);
static void AppPresetLatencyDiag_Finish(uint32_t display_done_us);
static const char *AppPresetLatencyDiag_KindLabel(uint8_t kind);

static uint32_t AppPresetLatencyDiag_NowUs(void)
{
    return TIM2->CNT;
}

static uint32_t AppPresetLatencyDiag_DiffUs(uint32_t end, uint32_t start)
{
    return (end >= start) ? (end - start) : (UINT32_MAX - start + end + 1U);
}

static void AppPresetLatencyDiag_Start(uint8_t kind, uint8_t button_index, uint8_t preset_index, uint32_t tick_ms)
{
    app_preset_latency_diag_state.active = 1U;
    app_preset_latency_diag_state.kind = kind;
    app_preset_latency_diag_state.button_index = button_index;
    app_preset_latency_diag_state.preset_index = preset_index;
    app_preset_latency_diag_state.activation_seen = 0U;
    app_preset_latency_diag_state.led_seen = 0U;
    app_preset_latency_diag_state.source_delta = 0;
    app_preset_latency_diag_state.press_ms = tick_ms;
    app_preset_latency_diag_state.press_us = AppPresetLatencyDiag_NowUs();
    app_preset_latency_diag_state.activation_us = 0U;
    app_preset_latency_diag_state.led_us = 0U;
}

static void AppPresetLatencyDiag_SetSourceDelta(int8_t delta)
{
    app_preset_latency_diag_state.source_delta = delta;
}

static void AppPresetLatencyDiag_Finish(uint32_t display_done_us)
{
    uint32_t activation_latency_us;
    uint32_t led_latency_us;
    uint32_t display_latency_us;
    uint32_t total_latency_us;

    if (!app_preset_latency_diag_state.active)
        return;

    activation_latency_us = app_preset_latency_diag_state.activation_seen
        ? AppPresetLatencyDiag_DiffUs(app_preset_latency_diag_state.activation_us,
                                      app_preset_latency_diag_state.press_us)
        : 0U;

    led_latency_us = app_preset_latency_diag_state.led_seen
        ? AppPresetLatencyDiag_DiffUs(app_preset_latency_diag_state.led_us,
                                      app_preset_latency_diag_state.press_us)
        : 0U;

    display_latency_us = AppPresetLatencyDiag_DiffUs(display_done_us,
                                                     app_preset_latency_diag_state.press_us);
    total_latency_us = display_latency_us;

        printf("PRESETLAT kind=%s btn=%u preset=%u delta=%d t_ms=%lu act_us=%lu led_us=%lu ui_us=%lu total_us=%lu\r\n",
           AppPresetLatencyDiag_KindLabel(app_preset_latency_diag_state.kind),
           (unsigned)(app_preset_latency_diag_state.button_index + 1U),
           (unsigned)app_preset_latency_diag_state.preset_index,
            (int)app_preset_latency_diag_state.source_delta,
           (unsigned long)app_preset_latency_diag_state.press_ms,
           (unsigned long)activation_latency_us,
           (unsigned long)led_latency_us,
           (unsigned long)display_latency_us,
           (unsigned long)total_latency_us);

    app_preset_latency_diag_state.active = 0U;
}

static const char *AppPresetLatencyDiag_KindLabel(uint8_t kind)
{
    switch (kind)
    {
    case APP_PRESET_LATENCY_KIND_PRESET:
        return "preset";

    case APP_PRESET_LATENCY_KIND_RANDOM:
        return "random";

    case APP_PRESET_LATENCY_KIND_MUTE:
        return "mute";

    case APP_PRESET_LATENCY_KIND_BYPASS:
        return "bypass";

    case APP_PRESET_LATENCY_KIND_ENC2:
        return "enc2";

    default:
        return "unknown";
    }
}

void AppPresetLatencyDiag_OnPresetButtonPress(uint8_t button_index,
                                              uint8_t preset_index,
                                              uint32_t tick_ms)
{
#if APP_PRESET_LATENCY_DIAG_ENABLED
    AppPresetLatencyDiag_Start(APP_PRESET_LATENCY_KIND_PRESET,
                               button_index,
                               preset_index,
                               tick_ms);
#else
    (void)button_index;
    (void)preset_index;
    (void)tick_ms;
#endif
}

void AppPresetLatencyDiag_OnRandomButtonPress(uint8_t button_index,
                                              uint32_t tick_ms)
{
#if APP_PRESET_LATENCY_DIAG_ENABLED
    AppPresetLatencyDiag_Start(APP_PRESET_LATENCY_KIND_RANDOM,
                               button_index,
                               0xFFU,
                               tick_ms);
#else
    (void)button_index;
    (void)tick_ms;
#endif
}

void AppPresetLatencyDiag_OnEnc2PresetStep(int8_t delta,
                                           uint8_t preset_index,
                                           uint32_t tick_ms)
{
#if APP_PRESET_LATENCY_DIAG_ENABLED
    AppPresetLatencyDiag_Start(APP_PRESET_LATENCY_KIND_ENC2,
                               0U,
                               preset_index,
                               tick_ms);
    AppPresetLatencyDiag_SetSourceDelta(delta);
#else
    (void)delta;
    (void)preset_index;
    (void)tick_ms;
#endif
}

void AppPresetLatencyDiag_OnMuteButtonPress(uint8_t button_index,
                                            uint32_t tick_ms)
{
#if APP_PRESET_LATENCY_DIAG_ENABLED
    AppPresetLatencyDiag_Start(APP_PRESET_LATENCY_KIND_MUTE,
                               button_index,
                               0xFFU,
                               tick_ms);
#else
    (void)button_index;
    (void)tick_ms;
#endif
}

void AppPresetLatencyDiag_OnActivationStart(void)
{
    /* Reserved for future queue-to-handler split instrumentation. */
}

void AppPresetLatencyDiag_OnActivationApplied(void)
{
#if APP_PRESET_LATENCY_DIAG_ENABLED
    if (!app_preset_latency_diag_state.active || app_preset_latency_diag_state.activation_seen)
        return;

    app_preset_latency_diag_state.activation_seen = 1U;
    app_preset_latency_diag_state.activation_us = AppPresetLatencyDiag_NowUs();
#endif
}

void AppPresetLatencyDiag_OnOverlayResolved(uint8_t is_mute_overlay)
{
#if APP_PRESET_LATENCY_DIAG_ENABLED
    if (!app_preset_latency_diag_state.active)
        return;

    app_preset_latency_diag_state.kind = is_mute_overlay
        ? APP_PRESET_LATENCY_KIND_MUTE
        : APP_PRESET_LATENCY_KIND_BYPASS;
#endif
}

void AppPresetLatencyDiag_OnLedIndicatorUpdated(void)
{
#if APP_PRESET_LATENCY_DIAG_ENABLED
    if (!app_preset_latency_diag_state.active || app_preset_latency_diag_state.led_seen)
        return;

    app_preset_latency_diag_state.led_seen = 1U;
    app_preset_latency_diag_state.led_us = AppPresetLatencyDiag_NowUs();
#endif
}

void AppPresetLatencyDiag_OnDisplayRefreshComplete(void)
{
#if APP_PRESET_LATENCY_DIAG_ENABLED
    AppPresetLatencyDiag_Finish(AppPresetLatencyDiag_NowUs());
#endif
}
