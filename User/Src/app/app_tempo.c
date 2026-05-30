#include "app/app_button_combo.h"
#include "app/app_tempo.h"

#include "app_event.h"
#include "app/app_requests.h"
#include "app/app_state.h"
#include "app/app_ui.h"
#include "bpm_functions.h"
#include "button_functions.h"
#include "display_functions.h"
#include "led_functions.h"
#include "midi_devices.h"
#include "midi_functions.h"
#include "stm32f4xx_hal.h"

#define APP_TEMPO_TAP_BUF_SIZE 4U
#define APP_TEMPO_TAP_RESET_INTERVAL_MS 3000U
#define APP_TEMPO_TAP_MIN_INTERVAL_MS 250U
#define APP_TEMPO_TAP_MIN_COUNT 2U
#define APP_TEMPO_EXT_CLOCK_MIRROR_STABLE_SAMPLES 3U

static volatile uint32_t app_tempo_tap_timestamps[APP_TEMPO_TAP_BUF_SIZE];
static volatile uint8_t app_tempo_tap_count = 0U;
static volatile uint8_t app_tempo_tap_head = 0U;
static uint16_t app_tempo_ext_mirror_candidate_bpm = 0U;
static uint8_t app_tempo_ext_mirror_stable_count = 0U;

static void AppTempo_HandleTapPress(uint32_t now);
static void AppTempo_ApplyInternalTempoBpm(uint16_t bpm, uint8_t pulse_led);
static void AppTempo_ApplyMirroredTempoBpm(uint16_t bpm);
static void AppTempo_SendTapTempoCcToDevices(void);

/* Handles tap-tempo input events from the central queue. */
uint8_t AppTempo_HandleEvent(const AppEvent_t *event)
{
    if (event == 0)
        return 0U;

    if (event->type != APP_EVENT_TYPE_TAP_PRESS)
        return 0U;

    AppTempo_HandleTapPress(event->tick);
    return 1U;
}

static void AppTempo_HandleTapPress(uint32_t now)
{
    uint8_t screensaver_was_active = Display_ScreensaverIsActive();

    LED_TapPressPulse();
    App_QueueScreensaverWakeEvent();

    if (AppButtonCombo_HandleTapPress(now, Button_IsMuteHeld()))
        return;

    AppTempo_SendTapTempoCcToDevices();

    if (screensaver_was_active)
    {
        App_QueueRedrawMainScreenEvent();
        return;
    }

    if (MidiClockIsExternalSignalPresent())
        return;

    if (app_tempo_tap_count > 0U)
    {
        uint8_t prev = (uint8_t)((app_tempo_tap_head + APP_TEMPO_TAP_BUF_SIZE - 1U) % APP_TEMPO_TAP_BUF_SIZE);
        uint32_t interval = now - app_tempo_tap_timestamps[prev];

        if (interval > APP_TEMPO_TAP_RESET_INTERVAL_MS)
        {
            app_tempo_tap_count = 0U;
            app_tempo_tap_head = 0U;
        }
        else if (interval < APP_TEMPO_TAP_MIN_INTERVAL_MS)
        {
            return;
        }
    }

    app_tempo_tap_timestamps[app_tempo_tap_head] = now;
    app_tempo_tap_head = (uint8_t)((app_tempo_tap_head + 1U) % APP_TEMPO_TAP_BUF_SIZE);
    if (app_tempo_tap_count < APP_TEMPO_TAP_BUF_SIZE)
        app_tempo_tap_count++;

    if (app_tempo_tap_count < APP_TEMPO_TAP_MIN_COUNT)
        return;

    {
        uint32_t sum = 0U;
        uint8_t sample_count = app_tempo_tap_count;

        for (uint8_t index = 0U; index < (uint8_t)(sample_count - 1U); index++)
        {
            uint8_t a = (uint8_t)((app_tempo_tap_head + APP_TEMPO_TAP_BUF_SIZE - sample_count + index) % APP_TEMPO_TAP_BUF_SIZE);
            uint8_t b = (uint8_t)((app_tempo_tap_head + APP_TEMPO_TAP_BUF_SIZE - sample_count + index + 1U) % APP_TEMPO_TAP_BUF_SIZE);

            sum += app_tempo_tap_timestamps[b] - app_tempo_tap_timestamps[a];
        }

        {
            uint32_t avg_ms = sum / (uint32_t)(sample_count - 1U);
            uint32_t new_bpm;

            if (avg_ms == 0U)
                return;

            new_bpm = (uint32_t)((60000.0f / (float)avg_ms) + 0.5f);
            if (new_bpm < BPM_MIN || new_bpm > BPM_MAX)
                return;

            AppTempo_ApplyInternalTempoBpm((uint16_t)new_bpm, 1U);
        }
    }
}

/* Applies an internal-tempo encoder step when external sync is absent. */
void AppTempo_ApplyEncoderStep(int8_t step)
{
    int32_t next_bpm = (int32_t)g_bpm + (int32_t)step;

    if (MidiClockIsExternalSignalPresent())
        return;

    if (next_bpm < (int32_t)BPM_MIN)
        next_bpm = (int32_t)BPM_MIN;
    else if (next_bpm > (int32_t)BPM_MAX)
        next_bpm = (int32_t)BPM_MAX;

    if ((uint16_t)next_bpm == g_bpm)
        return;

    AppTempo_ApplyInternalTempoBpm((uint16_t)next_bpm, 0U);
}

/* Mirrors a stable external BPM back into the internal tempo state. */
void AppTempo_ExternalClockHoldoverMirrorService(void)
{
    uint16_t external_bpm_x10;
    uint16_t external_bpm;

    if (!MidiClockGetExternalBpmX10(&external_bpm_x10))
    {
        app_tempo_ext_mirror_stable_count = 0U;
        return;
    }

    external_bpm = (uint16_t)((external_bpm_x10 + 5U) / 10U);
    if (external_bpm < BPM_MIN || external_bpm > BPM_MAX)
    {
        app_tempo_ext_mirror_stable_count = 0U;
        return;
    }

    if (external_bpm != app_tempo_ext_mirror_candidate_bpm)
    {
        app_tempo_ext_mirror_candidate_bpm = external_bpm;
        app_tempo_ext_mirror_stable_count = 1U;
        return;
    }

    if (app_tempo_ext_mirror_stable_count < 0xFFU)
        app_tempo_ext_mirror_stable_count++;

    if (app_tempo_ext_mirror_stable_count < APP_TEMPO_EXT_CLOCK_MIRROR_STABLE_SAMPLES)
        return;

    if (g_bpm == app_tempo_ext_mirror_candidate_bpm)
        return;

    AppTempo_ApplyMirroredTempoBpm(app_tempo_ext_mirror_candidate_bpm);
}

static void AppTempo_ApplyInternalTempoBpm(uint16_t bpm, uint8_t pulse_led)
{
    AppState_SetTempoBpm(bpm);
    MidiClockUseInternalTempo();
    MidiClockOutputSetTempoBpm(bpm);
    AppUi_RequestStatusStripRefresh();

    if (pulse_led)
        LED_BeatPulse();

    AppState_ScheduleRuntimeStateSaveAt(HAL_GetTick() + BPM_SAVE_DELAY_MS);
}

static void AppTempo_ApplyMirroredTempoBpm(uint16_t bpm)
{
    AppState_SetTempoBpm(bpm);
    MidiClockOutputSetTempoBpm(bpm);
    AppUi_RequestStatusStripRefresh();
}

static void AppTempo_SendTapTempoCcToDevices(void)
{
    for (uint8_t device_index = 0U; device_index < MIDI_DEVICE_COUNT; ++device_index)
    {
        const MidiDevice_t *device = MidiDevices_Get(device_index);

        if (!device)
            continue;

        if (device->tap_tempo.cc == PRESET_CC_NUMBER_UNUSED)
            continue;

        MIDI_SendCC(device->channel, device->tap_tempo.cc, device->tap_tempo.value);
    }
}