#include "app/app_ui.h"

#include "app/app_state.h"
#include "display_functions.h"
#include "midi_functions.h"
#include "stm32f4xx_hal.h"

#define APP_UI_RENDER_INVALIDATE_STATUS_STRIP      0x01U
#define APP_UI_RENDER_INVALIDATE_ACTIVE_DISPLAY    0x02U
#define APP_UI_RENDER_INVALIDATE_MAIN_SCREEN       0x04U
#define APP_UI_RENDER_INVALIDATE_PRESET_EDIT_MODE  0x08U
#define APP_UI_RENDER_INVALIDATE_PRESET_EDIT_FIELD 0x10U
#define APP_UI_RENDER_INVALIDATE_LIVE_CONTENT      0x20U

static uint8_t app_ui_render_pending_mask = 0U;
static uint8_t app_ui_render_beat_status_pending = 0U;

/**
 * Lifetime worst-case render times per path.
 * Captures the slowest observed execution for each display operation since
 * power-on so LATENCYDIAG can directly attribute foreground stalls.
 */
static uint32_t app_ui_render_beat_max_us = 0U;
static uint32_t app_ui_render_main_max_us = 0U;
static uint32_t app_ui_render_active_max_us = 0U;
static uint32_t app_ui_render_live_max_us = 0U;
static uint32_t app_ui_render_edit_mode_max_us = 0U;
static uint32_t app_ui_render_edit_field_max_us = 0U;
static uint32_t app_ui_render_status_max_us = 0U;

static uint32_t AppUiRender_TimerDiffUs(uint32_t end, uint32_t start)
{
    return (end >= start) ? (end - start) : (UINT32_MAX - start + end + 1U);
}

static void AppUi_RenderSetPendingMask(uint8_t mask)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    app_ui_render_pending_mask |= mask;
    if (primask == 0U)
        __enable_irq();
}

static uint8_t AppUi_RenderGetPendingMask(void)
{
    uint32_t primask = __get_PRIMASK();
    uint8_t pending_mask;

    __disable_irq();
    pending_mask = app_ui_render_pending_mask;
    if (primask == 0U)
        __enable_irq();

    return pending_mask;
}

static void AppUi_RenderClearPendingMask(uint8_t mask)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    app_ui_render_pending_mask &= (uint8_t)~mask;
    if (primask == 0U)
        __enable_irq();
}

static void AppUi_RenderQueueBeatStatusRefresh(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    /* Beat-strip refresh requests are overwrite/coalesced so the UI always
     * renders the latest transport state rather than draining stale backlog. */
    app_ui_render_beat_status_pending = 1U;
    if (primask == 0U)
        __enable_irq();
}

static uint8_t AppUi_RenderConsumeBeatStatusRefresh(void)
{
    uint32_t primask = __get_PRIMASK();
    uint8_t had_pending = 0U;

    __disable_irq();
    if (app_ui_render_beat_status_pending != 0U)
    {
        app_ui_render_beat_status_pending = 0U;
        had_pending = 1U;
    }
    if (primask == 0U)
        __enable_irq();

    return had_pending;
}

/* Returns the preset currently shown on the live UI. */
const Preset_t *AppUi_GetCurrentDisplayPreset(void)
{
    const Preset_t *active_preset = AppState_GetActivePreset();

    return active_preset
        ? active_preset
        : Presets_Get(AppState_GetCurrentBank() * PRESETS_PER_BANK);
}

/* Marks preset-edit mode for a coalesced redraw. */
void AppUi_RequestPresetEditModeRefresh(void)
{
    AppUi_RenderSetPendingMask(APP_UI_RENDER_INVALIDATE_PRESET_EDIT_MODE);
}

/* Marks the active preset-edit field for a targeted redraw. */
void AppUi_RequestPresetEditFieldRefresh(void)
{
    AppUi_RenderSetPendingMask(APP_UI_RENDER_INVALIDATE_PRESET_EDIT_FIELD);
}

/* Marks the BPM/status strip for refresh. */
void AppUi_RequestStatusStripRefresh(void)
{
    AppUi_RenderSetPendingMask(APP_UI_RENDER_INVALIDATE_STATUS_STRIP);
}

/* Reserves one render slot for a beat-edge status update. */
void AppUi_RequestBeatSynchronousStatusStripRefresh(void)
{
    AppUi_RenderQueueBeatStatusRefresh();
}

/* Services only beat-synchronous status-strip work.
 *
 * This keeps beat-edge bar/beat updates flowing even when foreground runtime
 * policy temporarily defers the wider UI invalidation pipeline. */
uint8_t AppUi_ServiceBeatSynchronousStatusStrip(void)
{
    uint32_t t_start;
    uint32_t t_elapsed;

    if (!AppUi_RenderConsumeBeatStatusRefresh())
        return 0U;

    t_start = TIM2->CNT;
    /* LOCKED beat-edge refresh now updates only the transport bar lane so
     * downbeat visibility is not delayed by broader BPM/status text work. */
    if (MidiClockGetSyncState() == MIDI_SYNC_STATE_LOCKED)
        Display_UpdateTransportBarBeatFast();
    else
        Display_UpdateBPM(AppState_GetTempoBpm());

    t_elapsed = AppUiRender_TimerDiffUs(TIM2->CNT, t_start);
    if (t_elapsed > app_ui_render_beat_max_us)
        app_ui_render_beat_max_us = t_elapsed;

    return 1U;
}

/* Marks the whole active display for refresh. */
void AppUi_RequestActiveDisplayRefresh(void)
{
    AppUi_RenderSetPendingMask(APP_UI_RENDER_INVALIDATE_ACTIVE_DISPLAY);
}

/* Marks the live content area for refresh without forcing a full redraw. */
void AppUi_RequestLiveContentRefresh(void)
{
    AppUi_RenderSetPendingMask(APP_UI_RENDER_INVALIDATE_LIVE_CONTENT);
}

/* Marks the entire main screen for redraw. */
void AppUi_RequestMainScreenRefresh(void)
{
    AppUi_RenderSetPendingMask(APP_UI_RENDER_INVALIDATE_MAIN_SCREEN);
}

/* Flushes the highest-priority pending UI invalidation. */
void AppUi_ServiceRender(void)
{
    const Preset_t *active_preset = AppState_GetActivePreset();
    uint16_t bpm = AppState_GetTempoBpm();
    uint8_t pending_mask = AppUi_RenderGetPendingMask();
    uint32_t t_start;
    uint32_t t_elapsed;

    if (AppUi_ServiceBeatSynchronousStatusStrip())
        return;

    if ((pending_mask & APP_UI_RENDER_INVALIDATE_MAIN_SCREEN) != 0U)
    {
        AppUi_RenderClearPendingMask((uint8_t)(APP_UI_RENDER_INVALIDATE_MAIN_SCREEN
                                             | APP_UI_RENDER_INVALIDATE_ACTIVE_DISPLAY
                                             | APP_UI_RENDER_INVALIDATE_LIVE_CONTENT
                                             | APP_UI_RENDER_INVALIDATE_STATUS_STRIP));
        t_start = TIM2->CNT;
        Display_DrawMainScreen(AppUi_GetCurrentDisplayPreset(), bpm);
        t_elapsed = AppUiRender_TimerDiffUs(TIM2->CNT, t_start);
        if (t_elapsed > app_ui_render_main_max_us)
            app_ui_render_main_max_us = t_elapsed;
        return;
    }

    if ((pending_mask & APP_UI_RENDER_INVALIDATE_ACTIVE_DISPLAY) != 0U)
    {
        AppUi_RenderClearPendingMask((uint8_t)(APP_UI_RENDER_INVALIDATE_ACTIVE_DISPLAY
                                             | APP_UI_RENDER_INVALIDATE_LIVE_CONTENT
                                             | APP_UI_RENDER_INVALIDATE_STATUS_STRIP));
        t_start = TIM2->CNT;
        Display_DrawMainScreen(active_preset ? active_preset : AppUi_GetCurrentDisplayPreset(), bpm);
        t_elapsed = AppUiRender_TimerDiffUs(TIM2->CNT, t_start);
        if (t_elapsed > app_ui_render_active_max_us)
            app_ui_render_active_max_us = t_elapsed;
        return;
    }

    if ((pending_mask & APP_UI_RENDER_INVALIDATE_LIVE_CONTENT) != 0U)
    {
        AppUi_RenderClearPendingMask((uint8_t)(APP_UI_RENDER_INVALIDATE_LIVE_CONTENT
                                             | APP_UI_RENDER_INVALIDATE_STATUS_STRIP));
        t_start = TIM2->CNT;
        Display_RefreshMainScreenContent(active_preset ? active_preset : AppUi_GetCurrentDisplayPreset(), bpm);
        t_elapsed = AppUiRender_TimerDiffUs(TIM2->CNT, t_start);
        if (t_elapsed > app_ui_render_live_max_us)
            app_ui_render_live_max_us = t_elapsed;
        return;
    }

    if ((pending_mask & APP_UI_RENDER_INVALIDATE_PRESET_EDIT_MODE) != 0U)
    {
        AppUi_RenderClearPendingMask((uint8_t)(APP_UI_RENDER_INVALIDATE_PRESET_EDIT_MODE
                                             | APP_UI_RENDER_INVALIDATE_PRESET_EDIT_FIELD));
        t_start = TIM2->CNT;
        Display_RefreshPresetEditMode(AppUi_GetCurrentDisplayPreset(), bpm);
        t_elapsed = AppUiRender_TimerDiffUs(TIM2->CNT, t_start);
        if (t_elapsed > app_ui_render_edit_mode_max_us)
            app_ui_render_edit_mode_max_us = t_elapsed;
        return;
    }

    if ((pending_mask & APP_UI_RENDER_INVALIDATE_PRESET_EDIT_FIELD) != 0U)
    {
        AppUi_RenderClearPendingMask(APP_UI_RENDER_INVALIDATE_PRESET_EDIT_FIELD);
        if (active_preset)
        {
            t_start = TIM2->CNT;
            Display_PresetEditRefreshCurrentField(active_preset);
            t_elapsed = AppUiRender_TimerDiffUs(TIM2->CNT, t_start);
            if (t_elapsed > app_ui_render_edit_field_max_us)
                app_ui_render_edit_field_max_us = t_elapsed;
        }
        return;
    }

    if ((pending_mask & APP_UI_RENDER_INVALIDATE_STATUS_STRIP) != 0U)
    {
        AppUi_RenderClearPendingMask(APP_UI_RENDER_INVALIDATE_STATUS_STRIP);
        t_start = TIM2->CNT;
        Display_UpdateBPM(bpm);
        t_elapsed = AppUiRender_TimerDiffUs(TIM2->CNT, t_start);
        if (t_elapsed > app_ui_render_status_max_us)
            app_ui_render_status_max_us = t_elapsed;
    }
}

void AppUi_GetRenderTimings(AppUiRenderTimings_t *timings)
{
    if (!timings)
        return;

    timings->beat_max_us       = app_ui_render_beat_max_us;
    timings->main_max_us       = app_ui_render_main_max_us;
    timings->active_max_us     = app_ui_render_active_max_us;
    timings->live_max_us       = app_ui_render_live_max_us;
    timings->edit_mode_max_us  = app_ui_render_edit_mode_max_us;
    timings->edit_field_max_us = app_ui_render_edit_field_max_us;
    timings->status_max_us     = app_ui_render_status_max_us;
}