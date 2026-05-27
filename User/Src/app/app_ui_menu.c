#include "app/app_ui.h"

#include "app_event.h"
#include "app/app_requests.h"
#include "app/app_state.h"
#include "display_functions.h"
#include "runtime_config.h"

void AppUi_ServiceMenuPreviewHold(uint8_t encoder2_switch_pressed)
{
    static uint8_t preview_visible = 0U;
    uint8_t should_preview = (Display_MenuPreviewCanShow() && encoder2_switch_pressed) ? 1U : 0U;

    if (should_preview == preview_visible)
        return;

    preview_visible = should_preview;

    if (should_preview)
    {
        Display_MenuPreviewEnter(AppUi_GetCurrentDisplayPreset(), AppState_GetTempoBpm());
        return;
    }

    Display_MenuPreviewExit();
}

void AppUi_MenuSaveIfDirty(void)
{
    if (!RuntimeConfig_IsDirty())
        return;

    App_QueueSaveRequestEvent(APP_EVENT_SAVE_KIND_RUNTIME_CONFIG);
}

uint8_t AppUi_MenuBackOutOneLevel(void)
{
    uint8_t sub_editor_active;

    if (!Display_MenuIsActive())
        return 0U;

    sub_editor_active = Display_MenuSubEditorIsActive();
    Display_MenuBack();
    App_QueueScreensaverActivityEvent();
    if (!sub_editor_active)
        AppUi_MenuSaveIfDirty();

    if (!Display_MenuIsActive())
    {
        if (Display_PresetEditIsActive())
            AppUi_RequestPresetEditModeRefresh();
        else
            App_QueueRedrawMainScreenEvent();
    }

    return 1U;
}

uint8_t AppUi_MenuEnter(void)
{
    if (Display_MenuIsActive() || Display_PresetEditIsActive())
        return 0U;

    App_QueueScreensaverWakeEvent();
    Display_MenuEnter();
    return 1U;
}

uint8_t AppUi_MenuEnterMetronomeQuickAccess(void)
{
    if (Display_PresetEditIsActive())
        return 0U;

    App_QueueScreensaverWakeEvent();
    return Display_MenuEnterMetronomeQuickAccess();
}