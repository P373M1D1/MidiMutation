#include "app/app_ui.h"

#include "app/app_state.h"
#include "display_functions.h"

#define APP_UI_RENDER_INVALIDATE_STATUS_STRIP      0x01U
#define APP_UI_RENDER_INVALIDATE_ACTIVE_DISPLAY    0x02U
#define APP_UI_RENDER_INVALIDATE_MAIN_SCREEN       0x04U
#define APP_UI_RENDER_INVALIDATE_PRESET_EDIT_MODE  0x08U
#define APP_UI_RENDER_INVALIDATE_PRESET_EDIT_FIELD 0x10U

static uint8_t app_ui_render_pending_mask = 0U;

const Preset_t *AppUi_GetCurrentDisplayPreset(void)
{
    const Preset_t *active_preset = AppState_GetActivePreset();

    return active_preset
        ? active_preset
        : Presets_Get(AppState_GetCurrentBank() * PRESETS_PER_BANK);
}

void AppUi_RequestPresetEditModeRefresh(void)
{
    app_ui_render_pending_mask |= APP_UI_RENDER_INVALIDATE_PRESET_EDIT_MODE;
}

void AppUi_RequestPresetEditFieldRefresh(void)
{
    app_ui_render_pending_mask |= APP_UI_RENDER_INVALIDATE_PRESET_EDIT_FIELD;
}

void AppUi_RequestStatusStripRefresh(void)
{
    app_ui_render_pending_mask |= APP_UI_RENDER_INVALIDATE_STATUS_STRIP;
}

void AppUi_RequestActiveDisplayRefresh(void)
{
    app_ui_render_pending_mask |= APP_UI_RENDER_INVALIDATE_ACTIVE_DISPLAY;
}

void AppUi_RequestMainScreenRefresh(void)
{
    app_ui_render_pending_mask |= APP_UI_RENDER_INVALIDATE_MAIN_SCREEN;
}

void AppUi_ServiceRender(void)
{
    const Preset_t *active_preset = AppState_GetActivePreset();
    uint16_t bpm = AppState_GetTempoBpm();
    uint8_t pending_mask = app_ui_render_pending_mask;

    if ((pending_mask & APP_UI_RENDER_INVALIDATE_MAIN_SCREEN) != 0U)
    {
        app_ui_render_pending_mask &= (uint8_t)~(APP_UI_RENDER_INVALIDATE_MAIN_SCREEN
                                               | APP_UI_RENDER_INVALIDATE_ACTIVE_DISPLAY
                                               | APP_UI_RENDER_INVALIDATE_STATUS_STRIP);
        Display_DrawMainScreen(AppUi_GetCurrentDisplayPreset(), bpm);
        return;
    }

    if ((pending_mask & APP_UI_RENDER_INVALIDATE_ACTIVE_DISPLAY) != 0U)
    {
        app_ui_render_pending_mask &= (uint8_t)~(APP_UI_RENDER_INVALIDATE_ACTIVE_DISPLAY
                                               | APP_UI_RENDER_INVALIDATE_STATUS_STRIP);
        Display_DrawMainScreen(active_preset ? active_preset : AppUi_GetCurrentDisplayPreset(), bpm);
        return;
    }

    if ((pending_mask & APP_UI_RENDER_INVALIDATE_PRESET_EDIT_MODE) != 0U)
    {
        app_ui_render_pending_mask &= (uint8_t)~(APP_UI_RENDER_INVALIDATE_PRESET_EDIT_MODE
                                               | APP_UI_RENDER_INVALIDATE_PRESET_EDIT_FIELD);
        Display_RefreshPresetEditMode(AppUi_GetCurrentDisplayPreset(), bpm);
        return;
    }

    if ((pending_mask & APP_UI_RENDER_INVALIDATE_PRESET_EDIT_FIELD) != 0U)
    {
        app_ui_render_pending_mask &= (uint8_t)~APP_UI_RENDER_INVALIDATE_PRESET_EDIT_FIELD;
        if (active_preset)
            Display_PresetEditRefreshCurrentField(active_preset);
        return;
    }

    if ((pending_mask & APP_UI_RENDER_INVALIDATE_STATUS_STRIP) != 0U)
    {
        app_ui_render_pending_mask &= (uint8_t)~APP_UI_RENDER_INVALIDATE_STATUS_STRIP;
        Display_UpdateBPM(bpm);
    }
}