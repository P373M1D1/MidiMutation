#include "app/app_save_service.h"

#include "app_event.h"
#include "app/app_requests.h"
#include "app/app_timer_events.h"
#include "app/app_state.h"
#include "app/app_ui.h"
#include "bpm_functions.h"
#include "display_functions.h"
#include "led_functions.h"
#include "midi/clock_engine.h"
#include "midi_functions.h"
#include "presets.h"
#include "runtime_config.h"

typedef enum {
    APP_SAVE_SERVICE_STATE_IDLE = 0,
    APP_SAVE_SERVICE_STATE_SHOW_COMBINED_POPUP,
    APP_SAVE_SERVICE_STATE_SAVE_COMBINED,
    APP_SAVE_SERVICE_STATE_HIDE_COMBINED_POPUP,
    APP_SAVE_SERVICE_STATE_SAVE_RUNTIME_STATE,
} AppSaveServiceState_t;

static uint8_t app_save_service_requested_mask = 0U;
static AppSaveServiceState_t app_save_service_state = APP_SAVE_SERVICE_STATE_IDLE;

static uint8_t AppSaveService_RequestMaskForKind(uint8_t save_kind);
static uint8_t AppSaveService_CombinedRequestMask(void);
static void AppSaveService_HandleRequestEvent(uint8_t save_kind);
static void AppSaveService_HandleTimeoutEvent(void);
static uint8_t AppSaveService_ShouldDeferFlashWrite(void);

static uint8_t AppSaveService_ShouldDeferFlashWrite(void)
{
    /* External clock presence already includes the transport-running case and
     * a short post-pulse timeout window, which makes it the right low-cost
     * guard for "do not start flash work during live sync". */
    return ClockEngine_IsExternalSignalPresent();
}

/* Collects save requests from the event queue into the deferred save service. */
uint8_t AppSaveService_HandleEvent(const AppEvent_t *event)
{
    if (event == 0)
        return 0U;

    switch (event->type)
    {
    case APP_EVENT_TYPE_SAVE_REQUEST:
        AppSaveService_HandleRequestEvent(event->source);
        return 1U;

    case APP_EVENT_TYPE_SAVE_TIMEOUT:
        AppSaveService_HandleTimeoutEvent();
        AppTimerEvents_AcknowledgeSaveTimeoutEvent();
        return 1U;

    default:
        return 0U;
    }
}

static uint8_t AppSaveService_RequestMaskForKind(uint8_t save_kind)
{
    switch (save_kind)
    {
    case APP_EVENT_SAVE_KIND_RUNTIME_CONFIG:
        return 0x01U;

    case APP_EVENT_SAVE_KIND_PRESETS:
        return 0x02U;

    case APP_EVENT_SAVE_KIND_RUNTIME_STATE:
        return 0x04U;

    default:
        return 0U;
    }
}

static uint8_t AppSaveService_CombinedRequestMask(void)
{
    return (uint8_t)(AppSaveService_RequestMaskForKind(APP_EVENT_SAVE_KIND_RUNTIME_CONFIG)
                    | AppSaveService_RequestMaskForKind(APP_EVENT_SAVE_KIND_PRESETS));
}

static void AppSaveService_HandleRequestEvent(uint8_t save_kind)
{
    App_AcknowledgeSaveRequestEvent(save_kind);
    app_save_service_requested_mask |= AppSaveService_RequestMaskForKind(save_kind);
}

static void AppSaveService_HandleTimeoutEvent(void)
{
    uint8_t combined_mask = AppSaveService_CombinedRequestMask();
    uint8_t runtime_state_mask = AppSaveService_RequestMaskForKind(APP_EVENT_SAVE_KIND_RUNTIME_STATE);

    switch (app_save_service_state)
    {
    case APP_SAVE_SERVICE_STATE_IDLE:
        if ((app_save_service_requested_mask & combined_mask) != 0U)
        {
            if (!RuntimeConfig_IsDirty() && !Presets_IsDirty())
            {
                app_save_service_requested_mask &= (uint8_t)~combined_mask;
                return;
            }

            if (AppSaveService_ShouldDeferFlashWrite())
                return;

            app_save_service_state = APP_SAVE_SERVICE_STATE_SHOW_COMBINED_POPUP;
            return;
        }

        if ((app_save_service_requested_mask & runtime_state_mask) != 0U)
        {
            if (AppSaveService_ShouldDeferFlashWrite())
                return;

            app_save_service_state = APP_SAVE_SERVICE_STATE_SAVE_RUNTIME_STATE;
        }
        return;

    case APP_SAVE_SERVICE_STATE_SHOW_COMBINED_POPUP:
        if (AppSaveService_ShouldDeferFlashWrite())
            return;

        Display_ShowSavingPopup();
        app_save_service_state = APP_SAVE_SERVICE_STATE_SAVE_COMBINED;
        return;

    case APP_SAVE_SERVICE_STATE_SAVE_COMBINED:
        if (AppSaveService_ShouldDeferFlashWrite())
        {
            app_save_service_state = APP_SAVE_SERVICE_STATE_HIDE_COMBINED_POPUP;
            return;
        }

        (void)Presets_SaveIfDirty();
        app_save_service_requested_mask &= (uint8_t)~combined_mask;
        app_save_service_state = APP_SAVE_SERVICE_STATE_HIDE_COMBINED_POPUP;
        return;

    case APP_SAVE_SERVICE_STATE_HIDE_COMBINED_POPUP:
        Display_HideSavingPopup(AppUi_GetCurrentDisplayPreset());
        app_save_service_state = APP_SAVE_SERVICE_STATE_IDLE;
        return;

    case APP_SAVE_SERVICE_STATE_SAVE_RUNTIME_STATE:
        if (AppSaveService_ShouldDeferFlashWrite())
            return;

#if BPM_FLASH_WRITES_ENABLED
        RuntimeState_Flash_Save(AppState_GetTempoBpm(),
                                AppState_GetActivePresetIndex(),
                                AppState_GetCurrentBank());
        LED_FlashPulse();
#endif
        app_save_service_requested_mask &= (uint8_t)~runtime_state_mask;
        app_save_service_state = APP_SAVE_SERVICE_STATE_IDLE;
        return;

    default:
        app_save_service_state = APP_SAVE_SERVICE_STATE_IDLE;
        return;
    }
}

/* Retained for compatibility; save deadlines are now driven by timer events. */
void AppSaveService_Service(void)
{
}

uint8_t AppSaveService_HasPendingWork(void)
{
    /* The state machine must also be considered pending when it has advanced
     * past IDLE but not yet finished: after the flash write the mask is cleared
     * but the machine is in HIDE_COMBINED_POPUP and needs one more timeout
     * event to call Display_HideSavingPopup and return to IDLE. Without this,
     * the popup stays on screen until something else triggers a full redraw. */
    return ((app_save_service_requested_mask != 0U)
            || (app_save_service_state != APP_SAVE_SERVICE_STATE_IDLE)) ? 1U : 0U;
}