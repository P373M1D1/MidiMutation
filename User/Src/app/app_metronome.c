#include "app/app_metronome.h"

#include "app/app_board_init.h"
#include "app/app_state.h"
#include "main.h"
#include "runtime_config.h"

#define APP_METRONOME_MAX_CLICKS_PER_QUARTER 3U
#define APP_METRONOME_US_PER_MINUTE         60000000UL
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

static void app_metronome_trigger_backend(AppMetronomeOutput_t output,
                                          AppMetronomeSource_t source,
                                          uint8_t accent,
                                          uint8_t volume,
                                          uint16_t pitch_hz);
static RuntimeConfigMetronome_t app_metronome_get_config_snapshot(void);
static void app_metronome_sync_config_snapshot(const RuntimeConfigMetronome_t *config);
static uint32_t app_metronome_now_us(void);
static uint32_t app_metronome_diff_us(uint32_t now, uint32_t previous);
static uint8_t app_metronome_time_reached(uint32_t now, uint32_t due);
static uint32_t app_metronome_quarter_interval_from_bpm(uint16_t bpm);
static uint16_t app_metronome_pitch_hz(uint8_t pitch_mode, uint8_t accent);
static uint32_t app_metronome_click_duration_us(uint8_t accent);
static void app_metronome_queue_click(uint8_t *count, uint32_t due_us, uint8_t accent);
static void app_metronome_prepare_schedule(uint32_t anchor_us,
                                           uint32_t quarter_interval_us,
                                           uint8_t beat_in_bar,
                                           uint8_t rhythm);
static void app_metronome_request_output_stop(void);
static void app_metronome_stop_backend_output(void);
static void app_metronome_service_output_timeout(void);

void AppMetronome_Init(void)
{
    uint32_t primask = __get_PRIMASK();
    RuntimeConfigMetronome_t config = app_metronome_get_config_snapshot();

    __disable_irq();
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
    if (primask == 0U)
        __enable_irq();

    app_metronome_last_config_snapshot = config;
    app_metronome_last_config_valid = 1U;
}

void AppMetronome_Service(void)
{
    RuntimeConfigMetronome_t config = app_metronome_get_config_snapshot();

    app_metronome_sync_config_snapshot(&config);
    app_metronome_service_output_timeout();

    for (;;)
    {
        uint32_t primask = __get_PRIMASK();
        uint32_t generation;
        uint32_t due_us[APP_METRONOME_MAX_CLICKS_PER_QUARTER];
        uint8_t accents[APP_METRONOME_MAX_CLICKS_PER_QUARTER];
        uint8_t click_count;
        uint8_t click_index;
        uint8_t enabled;
        AppMetronomeSource_t source;
        AppMetronomeOutput_t output;
        uint32_t now_us;
        uint8_t next_index;

        __disable_irq();
        generation = app_metronome_schedule_generation;
        click_count = app_metronome_pending_click_count;
        click_index = app_metronome_pending_click_index;
        enabled = app_metronome_enabled;
        source = app_metronome_last_source;
        output = app_metronome_output;
        for (uint8_t index = 0U; index < APP_METRONOME_MAX_CLICKS_PER_QUARTER; ++index)
        {
            due_us[index] = app_metronome_pending_click_due_us[index];
            accents[index] = app_metronome_pending_click_accents[index];
        }
        if (primask == 0U)
            __enable_irq();

        if (!enabled
         || output == APP_METRONOME_OUTPUT_NONE
         || !AppMetronome_IsOutputAvailable(output)
         || config.volume == 0U)
        {
            app_metronome_request_output_stop();
            app_metronome_service_output_timeout();
            return;
        }

        if (click_index >= click_count)
            return;

        now_us = app_metronome_now_us();
        next_index = click_index;

        while (next_index < click_count && app_metronome_time_reached(now_us, due_us[next_index]))
        {
            uint8_t accent = accents[next_index];
            uint16_t pitch_hz = app_metronome_pitch_hz((uint8_t)config.pitch, accent);

            app_metronome_trigger_backend(output,
                                          source,
                                          accent,
                                          config.volume,
                                          pitch_hz);
            next_index++;
            now_us = app_metronome_now_us();
        }

        if (next_index == click_index)
            return;

        primask = __get_PRIMASK();
        __disable_irq();
        if (generation == app_metronome_schedule_generation)
        {
            app_metronome_pending_click_index = next_index;
            if (next_index >= app_metronome_pending_click_count)
            {
                app_metronome_pending_click_index = 0U;
                app_metronome_pending_click_count = 0U;
            }
        }
        if (primask == 0U)
            __enable_irq();

        if (generation == app_metronome_schedule_generation)
            return;
    }
}

void AppMetronome_ResetCycle(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    app_metronome_beat_in_bar = 0U;
    app_metronome_pending_click_count = 0U;
    app_metronome_pending_click_index = 0U;
    app_metronome_last_quarter_anchor_us = 0U;
    app_metronome_schedule_generation++;
    app_metronome_output_stop_requested = 1U;
    if (primask == 0U)
        __enable_irq();
}

void AppMetronome_OnQuarterNote(AppMetronomeSource_t source)
{
    uint32_t primask = __get_PRIMASK();
    uint32_t now_us = app_metronome_now_us();
    uint32_t quarter_interval_us = app_metronome_quarter_interval_us;
    uint8_t beats_per_bar = app_metronome_config_beats_per_bar;
    uint8_t next_beat_in_bar;
    uint8_t rhythm = app_metronome_config_rhythm;

    if (beats_per_bar < RUNTIME_CONFIG_METRONOME_BEATS_PER_BAR_MIN
     || beats_per_bar > RUNTIME_CONFIG_METRONOME_BEATS_PER_BAR_MAX)
        beats_per_bar = 4U;

    if (app_metronome_last_quarter_anchor_us != 0U && now_us != app_metronome_last_quarter_anchor_us)
        quarter_interval_us = app_metronome_diff_us(now_us, app_metronome_last_quarter_anchor_us);

    if (quarter_interval_us == 0U)
        quarter_interval_us = app_metronome_quarter_interval_from_bpm(AppState_GetTempoBpm());

    __disable_irq();
    app_metronome_last_source = source;
    app_metronome_quarter_interval_us = quarter_interval_us;
    app_metronome_last_quarter_anchor_us = now_us;
    next_beat_in_bar = (app_metronome_beat_in_bar == 0U || app_metronome_beat_in_bar >= beats_per_bar)
        ? 1U
        : (uint8_t)(app_metronome_beat_in_bar + 1U);
    app_metronome_beat_in_bar = next_beat_in_bar;
    app_metronome_pending_click_count = 0U;
    app_metronome_pending_click_index = 0U;
    app_metronome_prepare_schedule(now_us,
                                   quarter_interval_us,
                                   next_beat_in_bar,
                                   rhythm);
    app_metronome_schedule_generation++;
    if (primask == 0U)
        __enable_irq();
}

void AppMetronome_SetEnabled(uint8_t enabled)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    app_metronome_enabled = enabled ? 1U : 0U;
    if (!app_metronome_enabled)
    {
        app_metronome_pending_click_count = 0U;
        app_metronome_pending_click_index = 0U;
        app_metronome_schedule_generation++;
        app_metronome_output_stop_requested = 1U;
    }
    if (primask == 0U)
        __enable_irq();
}

uint8_t AppMetronome_IsEnabled(void)
{
    return (app_metronome_enabled && app_metronome_config_volume > 0U) ? 1U : 0U;
}

void AppMetronome_SetOutput(AppMetronomeOutput_t output)
{
    uint32_t primask = __get_PRIMASK();

    if (output > APP_METRONOME_OUTPUT_GPIO_PULSE)
        return;

    __disable_irq();
    app_metronome_output = output;
    if (primask == 0U)
        __enable_irq();
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

static void app_metronome_trigger_backend(AppMetronomeOutput_t output,
                                          AppMetronomeSource_t source,
                                          uint8_t accent,
                                          uint8_t volume,
                                          uint16_t pitch_hz)
{
    uint32_t primask;

    (void)source;

    app_metronome_last_click_pitch_hz = pitch_hz;

    switch (output)
    {
    case APP_METRONOME_OUTPUT_NONE:
        app_metronome_request_output_stop();
        return;

    case APP_METRONOME_OUTPUT_PWM_CLICK:
        if (!AppBoard_MetronomePwmStart(pitch_hz, volume))
        {
            app_metronome_request_output_stop();
            return;
        }

        primask = __get_PRIMASK();
        __disable_irq();
        app_metronome_output_active = 1U;
        app_metronome_output_stop_requested = 0U;
        app_metronome_output_stop_due_us = app_metronome_now_us() + app_metronome_click_duration_us(accent);
        if (primask == 0U)
            __enable_irq();
        return;

    case APP_METRONOME_OUTPUT_DAC_CLICK:
    case APP_METRONOME_OUTPUT_GPIO_PULSE:
    default:
        (void)accent;
        app_metronome_request_output_stop();
        return;
    }
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

    if ((uint8_t)snapshot.rhythm > (uint8_t)RUNTIME_CONFIG_METRONOME_RHYTHM_SHUFFLE)
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

static uint32_t app_metronome_now_us(void)
{
    return TIM2->CNT;
}

static uint32_t app_metronome_diff_us(uint32_t now, uint32_t previous)
{
    return (now >= previous)
        ? (now - previous)
        : (UINT32_MAX - previous + now + 1U);
}

static uint8_t app_metronome_time_reached(uint32_t now, uint32_t due)
{
    return ((int32_t)(now - due) >= 0) ? 1U : 0U;
}

static uint32_t app_metronome_quarter_interval_from_bpm(uint16_t bpm)
{
    uint32_t safe_bpm = (bpm == 0U) ? 120U : (uint32_t)bpm;

    return (APP_METRONOME_US_PER_MINUTE + (safe_bpm / 2U)) / safe_bpm;
}

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

static uint32_t app_metronome_click_duration_us(uint8_t accent)
{
    return accent ? APP_METRONOME_PWM_ACCENT_DURATION_US
                  : APP_METRONOME_PWM_CLICK_DURATION_US;
}

static void app_metronome_queue_click(uint8_t *count, uint32_t due_us, uint8_t accent)
{
    if (!count || *count >= APP_METRONOME_MAX_CLICKS_PER_QUARTER)
        return;

    app_metronome_pending_click_due_us[*count] = due_us;
    app_metronome_pending_click_accents[*count] = accent ? 1U : 0U;
    (*count)++;
}

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
    case RUNTIME_CONFIG_METRONOME_RHYTHM_OFFBEAT:
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

static void app_metronome_request_output_stop(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    app_metronome_output_stop_requested = 1U;
    if (primask == 0U)
        __enable_irq();
}

static void app_metronome_stop_backend_output(void)
{
    uint32_t primask = __get_PRIMASK();
    uint8_t was_active;

    __disable_irq();
    was_active = app_metronome_output_active;
    app_metronome_output_active = 0U;
    app_metronome_output_stop_requested = 0U;
    app_metronome_output_stop_due_us = 0U;
    if (primask == 0U)
        __enable_irq();

    if (!was_active)
        return;

    AppBoard_MetronomePwmStop();
}

static void app_metronome_service_output_timeout(void)
{
    uint32_t primask = __get_PRIMASK();
    uint8_t output_active;
    uint8_t stop_requested;
    uint32_t stop_due_us;

    __disable_irq();
    output_active = app_metronome_output_active;
    stop_requested = app_metronome_output_stop_requested;
    stop_due_us = app_metronome_output_stop_due_us;
    if (primask == 0U)
        __enable_irq();

    if (!output_active)
        return;

    if (stop_requested || app_metronome_time_reached(app_metronome_now_us(), stop_due_us))
        app_metronome_stop_backend_output();
}