#include "app/app_metronome.h"

#include "app/app_board_init.h"
#include "app/app_state.h"
#include "main.h"
#include "runtime_config.h"

#define APP_METRONOME_MAX_CLICKS_PER_QUARTER 3U
#define APP_METRONOME_US_PER_MINUTE         60000000UL
#define APP_METRONOME_TIMING_COMPARE_GUARD_US 20UL
#define APP_METRONOME_PWM_CLICK_DURATION_US 18000UL
#define APP_METRONOME_PWM_ACCENT_DURATION_US 26000UL

static volatile uint8_t app_metronome_enabled = 1U;
static volatile uint8_t app_metronome_beat_in_bar = 0U;
static volatile AppMetronomeSource_t app_metronome_last_source = APP_METRONOME_SOURCE_INTERNAL;
static volatile AppMetronomeOutput_t app_metronome_output = APP_METRONOME_OUTPUT_NONE;
static volatile uint8_t app_metronome_config_volume = 0U;
static volatile uint8_t app_metronome_config_pitch = (uint8_t)RUNTIME_CONFIG_METRONOME_PITCH_MID;
static volatile uint8_t app_metronome_config_beats_per_bar = 4U;
static volatile uint8_t app_metronome_config_rhythm = (uint8_t)RUNTIME_CONFIG_METRONOME_RHYTHM_QUARTER_NOTES;
static volatile uint8_t app_metronome_pending_click_count = 0U;
static volatile uint8_t app_metronome_pending_click_index = 0U;
static volatile uint8_t app_metronome_pending_click_accents[APP_METRONOME_MAX_CLICKS_PER_QUARTER] = { 0U };
static volatile uint32_t app_metronome_pending_click_due_us[APP_METRONOME_MAX_CLICKS_PER_QUARTER] = { 0U };
static volatile uint32_t app_metronome_last_quarter_anchor_us = 0U;
static volatile uint32_t app_metronome_quarter_interval_us = 0U;
static volatile uint32_t app_metronome_schedule_generation = 0U;
static volatile uint16_t app_metronome_last_click_pitch_hz = 0U;
static volatile uint8_t app_metronome_output_active = 0U;
static volatile uint8_t app_metronome_output_stop_requested = 0U;
static volatile uint32_t app_metronome_output_stop_due_us = 0U;

static RuntimeConfigMetronome_t app_metronome_last_config_snapshot;
static uint8_t app_metronome_last_config_valid = 0U;

__attribute__((always_inline))
static inline uint32_t app_metronome_enter_critical(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    return primask;
}

__attribute__((always_inline))
static inline void app_metronome_exit_critical(uint32_t primask)
{
    if (primask == 0U)
        __enable_irq();
}

static RuntimeConfigMetronome_t app_metronome_get_config_snapshot(void);
static void app_metronome_sync_config_snapshot(const RuntimeConfigMetronome_t *config);
__attribute__((section(".RamFunc")))
static uint32_t app_metronome_now_us(void);
__attribute__((section(".RamFunc")))
static uint32_t app_metronome_diff_us(uint32_t now, uint32_t previous);
__attribute__((section(".RamFunc")))
static uint8_t app_metronome_time_reached(uint32_t now, uint32_t due);
__attribute__((section(".RamFunc")))
static uint32_t app_metronome_quarter_interval_from_bpm(uint16_t bpm);
__attribute__((section(".RamFunc")))
static uint16_t app_metronome_pitch_hz(uint8_t pitch_mode, uint8_t accent);
__attribute__((section(".RamFunc")))
static uint32_t app_metronome_click_duration_us(uint8_t accent);
__attribute__((section(".RamFunc")))
static void app_metronome_queue_click(uint8_t *count, uint32_t due_us, uint8_t accent);
__attribute__((section(".RamFunc")))
static void app_metronome_prepare_schedule(uint32_t anchor_us,
                                           uint32_t quarter_interval_us,
                                           uint8_t beat_in_bar,
                                           uint8_t rhythm);
__attribute__((section(".RamFunc")))
static uint8_t app_metronome_output_ready_locked(void);
__attribute__((section(".RamFunc")))
static void app_metronome_disarm_click_compare(void);
__attribute__((section(".RamFunc")))
static void app_metronome_arm_next_click_compare(uint32_t now_us);
__attribute__((section(".RamFunc")))
static void app_metronome_dispatch_due_clicks(uint32_t now_us);
static void app_metronome_request_output_stop(void);
static void app_metronome_stop_backend_output(void);
static void app_metronome_service_output_timeout(void);

void AppMetronome_Init(void)
{
    uint32_t primask = app_metronome_enter_critical();
    RuntimeConfigMetronome_t config = app_metronome_get_config_snapshot();

    app_metronome_enabled = 1U;
    app_metronome_beat_in_bar = 0U;
    app_metronome_last_source = APP_METRONOME_SOURCE_INTERNAL;
    app_metronome_output = AppBoard_MetronomePwmIsAvailable()
        ? APP_METRONOME_OUTPUT_PWM_CLICK
        : APP_METRONOME_OUTPUT_NONE;
    app_metronome_config_volume = config.volume;
    app_metronome_config_pitch = (uint8_t)config.pitch;
    app_metronome_config_beats_per_bar = config.beats_per_bar;
    app_metronome_config_rhythm = (uint8_t)config.rhythm;
    app_metronome_pending_click_count = 0U;
    app_metronome_pending_click_index = 0U;
    app_metronome_last_quarter_anchor_us = 0U;
    app_metronome_quarter_interval_us = app_metronome_quarter_interval_from_bpm(AppState_GetTempoBpm());
    app_metronome_schedule_generation = 0U;
    app_metronome_last_click_pitch_hz = 0U;
    app_metronome_output_active = 0U;
    app_metronome_output_stop_requested = 0U;
    app_metronome_output_stop_due_us = 0U;
    app_metronome_disarm_click_compare();
    app_metronome_exit_critical(primask);

    app_metronome_last_config_snapshot = config;
    app_metronome_last_config_valid = 1U;
}

void AppMetronome_Service(void)
{
    RuntimeConfigMetronome_t config = app_metronome_get_config_snapshot();

    app_metronome_sync_config_snapshot(&config);
    app_metronome_service_output_timeout();

    if (!app_metronome_enabled
     || app_metronome_output == APP_METRONOME_OUTPUT_NONE
     || !AppMetronome_IsOutputAvailable(app_metronome_output)
     || config.volume == 0U)
    {
        uint32_t primask = app_metronome_enter_critical();

        app_metronome_disarm_click_compare();
        app_metronome_exit_critical(primask);
        app_metronome_request_output_stop();
        app_metronome_service_output_timeout();
    }
}

__attribute__((section(".RamFunc")))
void AppMetronome_HandleTimingCounterIrq(void)
{
    if (((TIM2->SR & TIM_SR_CC1IF) == 0U)
     || ((TIM2->DIER & TIM_DIER_CC1IE) == 0U))
        return;

    TIM2->SR = ~TIM_SR_CC1IF;
    app_metronome_dispatch_due_clicks(app_metronome_now_us());
}

__attribute__((section(".RamFunc")))
void AppMetronome_ResetCycle(void)
{
    uint32_t primask = app_metronome_enter_critical();

    app_metronome_beat_in_bar = 0U;
    app_metronome_pending_click_count = 0U;
    app_metronome_pending_click_index = 0U;
    app_metronome_last_quarter_anchor_us = 0U;
    app_metronome_schedule_generation++;
    app_metronome_output_stop_requested = 1U;
    app_metronome_disarm_click_compare();
    app_metronome_exit_critical(primask);
}

__attribute__((section(".RamFunc")))
void AppMetronome_OnQuarterNote(AppMetronomeSource_t source)
{
    AppMetronome_OnQuarterNoteAt(source, app_metronome_now_us());
}

__attribute__((section(".RamFunc")))
void AppMetronome_OnQuarterNoteAt(AppMetronomeSource_t source, uint32_t anchor_us)
{
    uint32_t primask = app_metronome_enter_critical();
    uint32_t quarter_interval_us = app_metronome_quarter_interval_us;
    uint8_t beats_per_bar = app_metronome_config_beats_per_bar;
    uint8_t next_beat_in_bar;
    uint8_t rhythm = app_metronome_config_rhythm;

    if (beats_per_bar < RUNTIME_CONFIG_METRONOME_BEATS_PER_BAR_MIN
     || beats_per_bar > RUNTIME_CONFIG_METRONOME_BEATS_PER_BAR_MAX)
        beats_per_bar = 4U;

    if (app_metronome_last_quarter_anchor_us != 0U && anchor_us != app_metronome_last_quarter_anchor_us)
        quarter_interval_us = app_metronome_diff_us(anchor_us, app_metronome_last_quarter_anchor_us);

    if (quarter_interval_us == 0U)
        quarter_interval_us = app_metronome_quarter_interval_from_bpm(AppState_GetTempoBpm());

    app_metronome_last_source = source;
    app_metronome_quarter_interval_us = quarter_interval_us;
    app_metronome_last_quarter_anchor_us = anchor_us;
    next_beat_in_bar = (app_metronome_beat_in_bar == 0U || app_metronome_beat_in_bar >= beats_per_bar)
        ? 1U
        : (uint8_t)(app_metronome_beat_in_bar + 1U);
    app_metronome_beat_in_bar = next_beat_in_bar;
    app_metronome_pending_click_count = 0U;
    app_metronome_pending_click_index = 0U;
    app_metronome_prepare_schedule(anchor_us,
                                   quarter_interval_us,
                                   next_beat_in_bar,
                                   rhythm);
    app_metronome_schedule_generation++;

    if (app_metronome_output_ready_locked() && app_metronome_pending_click_count != 0U)
        app_metronome_arm_next_click_compare(anchor_us);
    else
        app_metronome_disarm_click_compare();

    app_metronome_exit_critical(primask);
}

void AppMetronome_SetEnabled(uint8_t enabled)
{
    uint32_t primask = app_metronome_enter_critical();

    app_metronome_enabled = enabled ? 1U : 0U;
    if (!app_metronome_enabled)
    {
        app_metronome_pending_click_count = 0U;
        app_metronome_pending_click_index = 0U;
        app_metronome_schedule_generation++;
        app_metronome_output_stop_requested = 1U;
        app_metronome_disarm_click_compare();
    }
    app_metronome_exit_critical(primask);
}

uint8_t AppMetronome_IsEnabled(void)
{
    return (app_metronome_enabled && app_metronome_config_volume > 0U) ? 1U : 0U;
}

void AppMetronome_SetOutput(AppMetronomeOutput_t output)
{
    uint32_t primask = app_metronome_enter_critical();

    if (output > APP_METRONOME_OUTPUT_GPIO_PULSE)
    {
        app_metronome_exit_critical(primask);
        return;
    }

    app_metronome_output = output;
    if (!app_metronome_output_ready_locked())
        app_metronome_disarm_click_compare();
    app_metronome_exit_critical(primask);
}

AppMetronomeOutput_t AppMetronome_GetOutput(void)
{
    return app_metronome_output;
}

uint8_t AppMetronome_IsOutputAvailable(AppMetronomeOutput_t output)
{
    switch (output)
    {
    case APP_METRONOME_OUTPUT_NONE:
        return 1U;
    case APP_METRONOME_OUTPUT_PWM_CLICK:
        return AppBoard_MetronomePwmIsAvailable();
    case APP_METRONOME_OUTPUT_DAC_CLICK:
    case APP_METRONOME_OUTPUT_GPIO_PULSE:
    default:
        return 0U;
    }
}

void AppMetronome_GetState(AppMetronomeState_t *state)
{
    uint32_t primask;

    if (!state)
        return;

    primask = __get_PRIMASK();
    __disable_irq();
    state->enabled = (app_metronome_enabled && app_metronome_config_volume > 0U) ? 1U : 0U;
    state->volume = app_metronome_config_volume;
    state->beat_in_bar = app_metronome_beat_in_bar;
    state->beats_per_bar = app_metronome_config_beats_per_bar;
    state->pending_click_count = (app_metronome_pending_click_count > app_metronome_pending_click_index)
        ? (uint8_t)(app_metronome_pending_click_count - app_metronome_pending_click_index)
        : 0U;
    state->next_click_accent = (app_metronome_pending_click_index < app_metronome_pending_click_count)
        ? app_metronome_pending_click_accents[app_metronome_pending_click_index]
        : 0U;
    state->rhythm = app_metronome_config_rhythm;
    state->quarter_interval_us = app_metronome_quarter_interval_us;
    state->last_source = app_metronome_last_source;
    state->output = app_metronome_output;
    state->last_click_pitch_hz = app_metronome_last_click_pitch_hz;
    if (primask == 0U)
        __enable_irq();

    state->normal_pitch_hz = app_metronome_pitch_hz(app_metronome_config_pitch, 0U);
    state->accent_pitch_hz = app_metronome_pitch_hz(app_metronome_config_pitch, 1U);
    state->output_available = AppMetronome_IsOutputAvailable(state->output);
}

static RuntimeConfigMetronome_t app_metronome_get_config_snapshot(void)
{
    const RuntimeConfigMetronome_t *config = RuntimeConfig_GetMetronome();
    RuntimeConfigMetronome_t snapshot = {
        .volume = 50U,
        .pitch = RUNTIME_CONFIG_METRONOME_PITCH_MID,
        .beats_per_bar = 4U,
        .rhythm = RUNTIME_CONFIG_METRONOME_RHYTHM_QUARTER_NOTES,
    };

    if (!config)
        return snapshot;

    snapshot = *config;
    if (snapshot.volume > RUNTIME_CONFIG_METRONOME_VOLUME_MAX)
        snapshot.volume = RUNTIME_CONFIG_METRONOME_VOLUME_MAX;

    if ((uint8_t)snapshot.pitch > (uint8_t)RUNTIME_CONFIG_METRONOME_PITCH_HIGH)
        snapshot.pitch = RUNTIME_CONFIG_METRONOME_PITCH_MID;

    if (snapshot.beats_per_bar < RUNTIME_CONFIG_METRONOME_BEATS_PER_BAR_MIN
     || snapshot.beats_per_bar > RUNTIME_CONFIG_METRONOME_BEATS_PER_BAR_MAX)
        snapshot.beats_per_bar = 4U;

    if ((uint8_t)snapshot.rhythm > (uint8_t)RUNTIME_CONFIG_METRONOME_RHYTHM_FOUR_EIGHT)
        snapshot.rhythm = RUNTIME_CONFIG_METRONOME_RHYTHM_QUARTER_NOTES;

    return snapshot;
}

static void app_metronome_sync_config_snapshot(const RuntimeConfigMetronome_t *config)
{
    uint8_t reset_cycle = 0U;
    uint32_t primask;

    if (!config)
        return;

    if (!app_metronome_last_config_valid)
    {
        app_metronome_last_config_snapshot = *config;
        app_metronome_last_config_valid = 1U;
    }
    else
    {
        if (config->beats_per_bar != app_metronome_last_config_snapshot.beats_per_bar
         || config->rhythm != app_metronome_last_config_snapshot.rhythm
         || ((config->volume == 0U) != (app_metronome_last_config_snapshot.volume == 0U)))
            reset_cycle = 1U;

        app_metronome_last_config_snapshot = *config;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    app_metronome_config_volume = config->volume;
    app_metronome_config_pitch = (uint8_t)config->pitch;
    app_metronome_config_beats_per_bar = config->beats_per_bar;
    app_metronome_config_rhythm = (uint8_t)config->rhythm;
    if (primask == 0U)
        __enable_irq();

    if (reset_cycle)
        AppMetronome_ResetCycle();
}

__attribute__((section(".RamFunc")))
static uint32_t app_metronome_now_us(void)
{
    return TIM2->CNT;
}

__attribute__((section(".RamFunc")))
static uint32_t app_metronome_diff_us(uint32_t now, uint32_t previous)
{
    return (now >= previous)
        ? (now - previous)
        : (UINT32_MAX - previous + now + 1U);
}

__attribute__((section(".RamFunc")))
static uint8_t app_metronome_time_reached(uint32_t now, uint32_t due)
{
    return ((int32_t)(now - due) >= 0) ? 1U : 0U;
}

__attribute__((section(".RamFunc")))
static uint32_t app_metronome_quarter_interval_from_bpm(uint16_t bpm)
{
    uint32_t safe_bpm = (bpm == 0U) ? 120U : (uint32_t)bpm;

    return (APP_METRONOME_US_PER_MINUTE + (safe_bpm / 2U)) / safe_bpm;
}

__attribute__((section(".RamFunc")))
static uint16_t app_metronome_pitch_hz(uint8_t pitch_mode, uint8_t accent)
{
    switch ((RuntimeConfigMetronomePitch_t)pitch_mode)
    {
    case RUNTIME_CONFIG_METRONOME_PITCH_LOW:
        return accent ? 1319U : 880U;

    case RUNTIME_CONFIG_METRONOME_PITCH_HIGH:
        return accent ? 1976U : 1319U;

    case RUNTIME_CONFIG_METRONOME_PITCH_MID:
    default:
        return accent ? 1568U : 1047U;
    }
}

__attribute__((section(".RamFunc")))
static uint32_t app_metronome_click_duration_us(uint8_t accent)
{
    return accent ? APP_METRONOME_PWM_ACCENT_DURATION_US
                  : APP_METRONOME_PWM_CLICK_DURATION_US;
}

__attribute__((section(".RamFunc")))
static void app_metronome_queue_click(uint8_t *count, uint32_t due_us, uint8_t accent)
{
    if (!count || *count >= APP_METRONOME_MAX_CLICKS_PER_QUARTER)
        return;

    app_metronome_pending_click_due_us[*count] = due_us;
    app_metronome_pending_click_accents[*count] = accent ? 1U : 0U;
    (*count)++;
}

__attribute__((section(".RamFunc")))
static void app_metronome_prepare_schedule(uint32_t anchor_us,
                                           uint32_t quarter_interval_us,
                                           uint8_t beat_in_bar,
                                           uint8_t rhythm)
{
    uint8_t count = 0U;
    uint8_t anchor_accent = (beat_in_bar == 1U) ? 1U : 0U;
    uint32_t triplet_third = quarter_interval_us / 3U;
    uint32_t half_note = quarter_interval_us / 2U;
    uint32_t swing_short = (quarter_interval_us * 2U) / 3U;

    switch ((RuntimeConfigMetronomeRhythm_t)rhythm)
    {
    case RUNTIME_CONFIG_METRONOME_RHYTHM_FOUR_EIGHT:
        app_metronome_queue_click(&count, anchor_us, anchor_accent);
        app_metronome_queue_click(&count, anchor_us + half_note, 0U);
        break;

    case RUNTIME_CONFIG_METRONOME_RHYTHM_OFFBEAT:
        if (anchor_accent)
            app_metronome_queue_click(&count, anchor_us, 1U);
        app_metronome_queue_click(&count, anchor_us + half_note, 0U);
        break;

    case RUNTIME_CONFIG_METRONOME_RHYTHM_TRIPLETS:
        app_metronome_queue_click(&count, anchor_us, anchor_accent);
        app_metronome_queue_click(&count, anchor_us + triplet_third, 0U);
        app_metronome_queue_click(&count, anchor_us + (triplet_third * 2U), 0U);
        break;

    case RUNTIME_CONFIG_METRONOME_RHYTHM_SHUFFLE:
        app_metronome_queue_click(&count, anchor_us, anchor_accent);
        app_metronome_queue_click(&count, anchor_us + swing_short, 0U);
        break;

    case RUNTIME_CONFIG_METRONOME_RHYTHM_QUARTER_NOTES:
    default:
        app_metronome_queue_click(&count, anchor_us, anchor_accent);
        break;
    }

    app_metronome_pending_click_count = count;
}

__attribute__((section(".RamFunc")))
static uint8_t app_metronome_output_ready_locked(void)
{
    if (!app_metronome_enabled || app_metronome_config_volume == 0U)
        return 0U;

    switch (app_metronome_output)
    {
    case APP_METRONOME_OUTPUT_PWM_CLICK:
        return AppBoard_MetronomePwmIsAvailable();

    case APP_METRONOME_OUTPUT_NONE:
    case APP_METRONOME_OUTPUT_DAC_CLICK:
    case APP_METRONOME_OUTPUT_GPIO_PULSE:
    default:
        return 0U;
    }
}

__attribute__((section(".RamFunc")))
static void app_metronome_disarm_click_compare(void)
{
    TIM2->DIER &= ~TIM_DIER_CC1IE;
    TIM2->SR = ~TIM_SR_CC1IF;
}

__attribute__((section(".RamFunc")))
static void app_metronome_arm_next_click_compare(uint32_t now_us)
{
    uint32_t due_us;
    uint32_t earliest_due_us;

    if (!app_metronome_output_ready_locked()
     || app_metronome_pending_click_index >= app_metronome_pending_click_count)
    {
        app_metronome_disarm_click_compare();
        return;
    }

    due_us = app_metronome_pending_click_due_us[app_metronome_pending_click_index];
    earliest_due_us = now_us + APP_METRONOME_TIMING_COMPARE_GUARD_US;
    if (app_metronome_time_reached(earliest_due_us, due_us))
        due_us = earliest_due_us;

    TIM2->CCR1 = due_us;
    TIM2->SR = ~TIM_SR_CC1IF;
    TIM2->DIER |= TIM_DIER_CC1IE;
}

__attribute__((section(".RamFunc")))
static void app_metronome_dispatch_due_clicks(uint32_t now_us)
{
    if (!app_metronome_output_ready_locked())
    {
        app_metronome_output_stop_requested = 1U;
        app_metronome_disarm_click_compare();
        return;
    }

    while (app_metronome_pending_click_index < app_metronome_pending_click_count
        && app_metronome_time_reached(now_us,
                                      app_metronome_pending_click_due_us[app_metronome_pending_click_index]))
    {
        uint8_t accent = app_metronome_pending_click_accents[app_metronome_pending_click_index];
        uint16_t pitch_hz = app_metronome_pitch_hz(app_metronome_config_pitch, accent);
        uint32_t duration_us = app_metronome_click_duration_us(accent);

        app_metronome_last_click_pitch_hz = pitch_hz;
        if (AppBoard_MetronomePwmStart(pitch_hz, app_metronome_config_volume, duration_us))
        {
            app_metronome_output_active = 1U;
            app_metronome_output_stop_requested = 0U;
            app_metronome_output_stop_due_us = now_us + duration_us;
        }
        else
        {
            app_metronome_output_active = 0U;
            app_metronome_output_stop_requested = 1U;
            app_metronome_output_stop_due_us = 0U;
        }

        app_metronome_pending_click_index++;
        now_us = app_metronome_now_us();
    }

    if (app_metronome_pending_click_index >= app_metronome_pending_click_count)
    {
        app_metronome_pending_click_index = 0U;
        app_metronome_pending_click_count = 0U;
        app_metronome_disarm_click_compare();
        return;
    }

    app_metronome_arm_next_click_compare(now_us);
}

static void app_metronome_request_output_stop(void)
{
    uint32_t primask = app_metronome_enter_critical();

    app_metronome_output_stop_requested = 1U;
    app_metronome_exit_critical(primask);
}

static void app_metronome_stop_backend_output(void)
{
    uint32_t primask = app_metronome_enter_critical();
    uint8_t was_active;

    was_active = app_metronome_output_active;
    app_metronome_output_active = 0U;
    app_metronome_output_stop_requested = 0U;
    app_metronome_output_stop_due_us = 0U;
    app_metronome_exit_critical(primask);

    if (!was_active)
        return;

    AppBoard_MetronomePwmStop();
}

static void app_metronome_service_output_timeout(void)
{
    uint32_t primask = app_metronome_enter_critical();
    uint8_t output_active;
    uint8_t stop_requested;
    uint32_t stop_due_us;

    output_active = app_metronome_output_active;
    stop_requested = app_metronome_output_stop_requested;
    stop_due_us = app_metronome_output_stop_due_us;
    app_metronome_exit_critical(primask);

    if (!output_active)
        return;

    if (stop_requested || app_metronome_time_reached(app_metronome_now_us(), stop_due_us))
        app_metronome_stop_backend_output();
}