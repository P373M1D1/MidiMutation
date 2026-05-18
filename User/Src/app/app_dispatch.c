#include "app/app_dispatch.h"

#include "app/app_requests.h"
#include "app/app_state.h"
#include "app/app_tempo.h"
#include "app/app_ui.h"
#include "app_event.h"
#include "bpm_functions.h"
#include "button_functions.h"
#include "display/display_menu_page_midi_monitor.h"
#include "display_functions.h"
#include "led_functions.h"
#include "midi_functions.h"
#include "presets.h"
#include "runtime_config.h"

typedef enum {
    APP_DISPATCH_SAVE_SERVICE_STATE_IDLE = 0,
    APP_DISPATCH_SAVE_SERVICE_STATE_SHOW_COMBINED_POPUP,
    APP_DISPATCH_SAVE_SERVICE_STATE_SAVE_COMBINED,
    APP_DISPATCH_SAVE_SERVICE_STATE_HIDE_COMBINED_POPUP,
    APP_DISPATCH_SAVE_SERVICE_STATE_SAVE_RUNTIME_STATE,
} AppDispatchSaveServiceState_t;

static uint8_t app_dispatch_save_service_requested_mask = 0U;
static AppDispatchSaveServiceState_t app_dispatch_save_service_state = APP_DISPATCH_SAVE_SERVICE_STATE_IDLE;

static void AppDispatch_HandleEncoderPressEvent(uint8_t press_mask);
static void AppDispatch_HandleBankStepEvent(int8_t delta, uint8_t step_mode);
static void AppDispatch_PreparePresetActivation(uint8_t exit_preset_edit);
static void AppDispatch_HandlePresetActivateEvent(uint8_t preset_index);
static void AppDispatch_HandlePresetActivateRandomEvent(void);
static void AppDispatch_HandlePresetActivateMuteEvent(void);
static void AppDispatch_SendFunctionButtonProgramMessages(const RuntimeConfigProgramMessage_t *messages,
                                                          uint8_t message_count);
static void AppDispatch_SendFunctionButtonCcMessages(const PresetCCSlot_t *messages,
                                                     uint8_t message_count);
static void AppDispatch_HandleSpecialFunctionToggleEvent(uint8_t state_active);
static void AppDispatch_HandleScreensaverWakeEvent(void);
static void AppDispatch_HandleScreensaverActivityEvent(void);
static void AppDispatch_HandlePeriodicUiServiceEvent(void);
static void AppDispatch_HandleRedrawActiveDisplayEvent(void);
static void AppDispatch_HandleRedrawMainScreenEvent(void);
static uint8_t AppDispatch_SaveRequestMaskForKind(uint8_t save_kind);
static uint8_t AppDispatch_SaveCombinedRequestMask(void);
static void AppDispatch_SaveServiceRegisterRequest(uint8_t save_kind);
static void AppDispatch_HandleSaveRequestEvent(uint8_t save_kind);

void AppDispatch_HandleEncoderTurnEvent(uint8_t encoder_source, int8_t delta)
{
    if (delta == 0)
        return;

    if (Display_MenuPreviewIsActive() && Display_MenuIsActive())
        return;

    switch (encoder_source)
    {
    case APP_EVENT_SOURCE_ENC1:
        if (Display_MenuIsActive())
        {
            if (Display_MenuMidiMonitorIsActive())
            {
                Display_MenuMidiMonitorScroll(delta);
                return;
            }

            if (Display_MenuUserThemeEditIsActive())
            {
                Display_MenuMoveSelection(delta);
                return;
            }

            if (Display_MenuTextEditIsActive())
                Display_MenuTextEditMoveCursor(delta);
            else
                Display_MenuMoveSelection(delta);
            return;
        }

        if (active_preset == NULL)
            return;

        if (Display_PresetEditIsActive())
        {
            if (!AppUi_PresetEditCurrentPresetIsEditable())
            {
                AppUi_PresetEditExit();
                return;
            }

            if (Display_PresetInitConfirmIsActive())
                return;

            if (Display_PresetNameEditIsActive())
                Display_PresetNameEditMoveCursor(active_preset, delta);
            else
                Display_PresetEditMoveCursorAndRefresh(active_preset, delta);
            return;
        }

        Display_MainInfoScrollAndRefresh(active_preset, delta);
        return;

    case APP_EVENT_SOURCE_ENC2:
        if (Display_MenuIsActive())
        {
            if (Display_MenuUserThemeEditIsActive())
                Display_MenuAdjustUserThemeHue(delta);
            return;
        }

        if (Display_PresetEditIsActive())
            return;

        {
            int16_t bank_base = (int16_t)(current_bank * PRESETS_PER_BANK);
            int16_t slot_index = (int16_t)active_preset_index - bank_base;
            int16_t next_slot = slot_index + (int16_t)delta;

            if (slot_index < 0 || slot_index >= (int16_t)PRESETS_PER_BANK)
                next_slot = 0;

            while (next_slot < 0)
                next_slot += (int16_t)PRESETS_PER_BANK;

            while (next_slot >= (int16_t)PRESETS_PER_BANK)
                next_slot -= (int16_t)PRESETS_PER_BANK;

            App_QueuePresetActivateEvent((uint8_t)(bank_base + next_slot));
        }
        return;

    case APP_EVENT_SOURCE_ENC3:
        if (Display_MenuIsActive())
        {
            if (Display_MenuMidiMonitorIsActive())
                return;

            if (Display_MenuUserThemeEditIsActive())
            {
                Display_MenuAdjustUserThemeBrightness(delta);
                return;
            }

            Display_MenuAdjustValue(delta);
            return;
        }

        if (Display_PresetEditIsActive())
        {
            if (!AppUi_PresetEditCurrentPresetIsEditable())
            {
                AppUi_PresetEditExit();
                return;
            }

            if (Display_PresetInitConfirmIsActive())
                return;

            if (AppUi_PresetEditApplyDelta(delta))
            {
                Presets_MarkDirty();
                Display_PresetEditRefreshCurrentField(active_preset);
            }
            return;
        }

        AppTempo_ApplyEncoderStep(delta);
        return;

    default:
        return;
    }
}

static void AppDispatch_HandleEncoderPressEvent(uint8_t press_mask)
{
    if (press_mask == 0U)
        return;

    if (Display_ScreensaverIsActive())
    {
        App_QueueScreensaverWakeEvent();
        App_QueueRedrawMainScreenEvent();
        return;
    }

    App_QueueScreensaverActivityEvent();

    if (Display_MenuPreviewIsActive() && Display_MenuIsActive())
        return;

    if (Display_MenuMidiMonitorIsActive())
    {
        if (press_mask & 0x01U)
        {
            Display_MenuMidiMonitorTogglePause();
            return;
        }

        if (press_mask & 0x02U)
        {
            Display_MenuMidiMonitorClear();
            return;
        }
    }

    if ((press_mask & 0x02U) && Display_MenuIsActive())
    {
        if (Display_MenuPreviewCanShow())
            return;

        Display_MenuHome();
        AppUi_MenuSaveIfDirty();
        return;
    }

    if ((press_mask & 0x04U) && Display_MenuIsActive())
    {
        if (Display_MenuTextEditIsActive())
        {
            Display_MenuTextEditExit();
            return;
        }

        AppUi_MenuBackOutOneLevel();
        return;
    }

    if ((press_mask & 0x01U) && Display_MenuIsActive())
    {
        Display_MenuActivate();
        return;
    }

    if ((press_mask & 0x02U) && Display_PresetEditIsActive())
    {
        if (Display_PresetInitConfirmIsActive())
        {
            Display_PresetInitConfirmExit();
            if (AppUi_PresetEditResetCurrentPresetToDefaults())
                Display_RefreshPresetEditMode(active_preset, g_bpm);
            return;
        }

        AppUi_PresetEditSendCurrentPreset();
        return;
    }

    if ((press_mask & 0x02U) && !Display_PresetEditIsActive())
    {
        App_QueueBankStepEvent(1, APP_EVENT_BANK_STEP_MODE_ACTIVE_SLOT);
        return;
    }

    if ((press_mask & 0x04U) && Display_PresetEditIsActive())
    {
        if (Display_PresetInitConfirmIsActive())
        {
            Display_PresetInitConfirmExit();
            return;
        }

        AppUi_PresetEditBackOutOneLevel();
        return;
    }

    if ((press_mask & 0x04U) && !Display_PresetEditIsActive())
    {
        AppUi_MenuEnter();
        return;
    }

    if ((press_mask & 0x01U) && !Display_PresetEditIsActive())
    {
        AppUi_PresetEditEnter();
    }
    else if ((press_mask & 0x01U) && Display_PresetEditIsActive() && !Display_PresetNameEditIsActive())
    {
        if (Display_PresetInitConfirmIsActive())
            return;

        {
            DisplayPresetEditField_t field = Display_PresetEditGetField();

            if (field.type == DISPLAY_PRESET_EDIT_FIELD_NAME)
            {
                Display_PresetNameEditEnter();
                if (active_preset)
                    Display_PresetEditRefreshCurrentField(active_preset);
            }
            else if (field.type == DISPLAY_PRESET_EDIT_FIELD_INIT)
            {
                Display_PresetInitConfirmEnter();
            }
        }
    }
}

static void AppDispatch_HandleBankStepEvent(int8_t delta, uint8_t step_mode)
{
    int16_t next_bank;
    uint8_t preset_slot = 0U;

    if (delta == 0)
        return;

    if (step_mode == APP_EVENT_BANK_STEP_MODE_ACTIVE_SLOT)
        preset_slot = (uint8_t)(active_preset_index % PRESETS_PER_BANK);

    next_bank = (int16_t)current_bank + (int16_t)delta;
    while (next_bank < 0)
        next_bank += (int16_t)PRESET_BANK_COUNT;

    while (next_bank >= (int16_t)PRESET_BANK_COUNT)
        next_bank -= (int16_t)PRESET_BANK_COUNT;

    current_bank = (uint8_t)next_bank;
    App_QueuePresetActivateEvent((uint8_t)(current_bank * PRESETS_PER_BANK + preset_slot));
}

static void AppDispatch_PreparePresetActivation(uint8_t exit_preset_edit)
{
    if (exit_preset_edit && Display_PresetEditIsActive())
        Display_PresetEditExit();

    Button_ResetSpecialFunctions();
    Display_MainInfoScrollReset();
}

static void AppDispatch_HandlePresetActivateEvent(uint8_t preset_index)
{
    const Preset_t *preset;

    (void)App_TakePendingPresetActivate(&preset_index);

    if (preset_index >= Presets_Count())
        return;

    preset = Presets_Get(preset_index);
    if (active_preset == preset)
        return;

    AppDispatch_PreparePresetActivation(0U);
    App_ActivatePreset(preset_index);
    App_QueueRedrawMainScreenEvent();
}

static void AppDispatch_HandlePresetActivateRandomEvent(void)
{
    AppDispatch_PreparePresetActivation(1U);
    Presets_ActivateRandom();
    App_QueueRedrawMainScreenEvent();
}

static void AppDispatch_HandlePresetActivateMuteEvent(void)
{
    AppDispatch_PreparePresetActivation(1U);
    Presets_ActivateMute();
    App_QueueRedrawMainScreenEvent();
}

static void AppDispatch_SendFunctionButtonProgramMessages(const RuntimeConfigProgramMessage_t *messages,
                                                          uint8_t message_count)
{
    if (!messages)
        return;

    for (uint8_t index = 0U; index < message_count; ++index)
    {
        const RuntimeConfigProgramMessage_t *message = &messages[index];

        if (message->channel == PRESET_CC_CHANNEL_UNUSED || message->program == PRESET_PROGRAM_NONE)
            continue;

        MIDI_SendProgramChange(message->channel, message->program);
    }
}

static void AppDispatch_SendFunctionButtonCcMessages(const PresetCCSlot_t *messages,
                                                     uint8_t message_count)
{
    if (!messages)
        return;

    for (uint8_t index = 0U; index < message_count; ++index)
    {
        const PresetCCSlot_t *message = &messages[index];

        if (message->channel == PRESET_CC_CHANNEL_UNUSED
         || message->cc_number == PRESET_CC_NUMBER_UNUSED
         || message->value == PRESET_CC_VALUE_UNUSED)
        {
            continue;
        }

        MIDI_SendCC(message->channel, message->cc_number, message->value);
    }
}

static void AppDispatch_HandleSpecialFunctionToggleEvent(uint8_t state_active)
{
    const RuntimeConfigFunctionButton_t *function_button = RuntimeConfig_GetFunctionButton(current_bank);

    if (function_button)
    {
        if (state_active)
        {
            AppDispatch_SendFunctionButtonProgramMessages(function_button->active_programs,
                                                          RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT);
            AppDispatch_SendFunctionButtonCcMessages(function_button->active_cc,
                                                     RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT);
        }
        else
        {
            AppDispatch_SendFunctionButtonProgramMessages(function_button->inactive_programs,
                                                          RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT);
            AppDispatch_SendFunctionButtonCcMessages(function_button->inactive_cc,
                                                     RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT);
        }
    }

    AppDispatch_HandleRedrawActiveDisplayEvent();
}

static void AppDispatch_HandleScreensaverWakeEvent(void)
{
    App_AcknowledgeScreensaverWakeEvent();
    Display_ScreensaverDismiss();
    Display_ScreensaverActivity();
}

static void AppDispatch_HandleScreensaverActivityEvent(void)
{
    App_AcknowledgeScreensaverActivityEvent();
    Display_ScreensaverActivity();
}

static void AppDispatch_HandlePeriodicUiServiceEvent(void)
{
    App_AcknowledgePeriodicUiServiceEvent();
    Display_UpdateBPM(g_bpm);
    Display_MenuMidiMonitorService();
    LED_Update();
    if (Display_ScreensaverUpdate())
        App_QueueRedrawMainScreenEvent();
}

static void AppDispatch_HandleRedrawActiveDisplayEvent(void)
{
    if (active_preset)
        Display_DrawMainScreen(active_preset, g_bpm);
}

static void AppDispatch_HandleRedrawMainScreenEvent(void)
{
    App_AcknowledgeRedrawMainScreenEvent();
    Display_DrawMainScreen(AppUi_GetCurrentDisplayPreset(), g_bpm);
}

static uint8_t AppDispatch_SaveRequestMaskForKind(uint8_t save_kind)
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

static uint8_t AppDispatch_SaveCombinedRequestMask(void)
{
    return (uint8_t)(AppDispatch_SaveRequestMaskForKind(APP_EVENT_SAVE_KIND_RUNTIME_CONFIG)
                    | AppDispatch_SaveRequestMaskForKind(APP_EVENT_SAVE_KIND_PRESETS));
}

static void AppDispatch_SaveServiceRegisterRequest(uint8_t save_kind)
{
    app_dispatch_save_service_requested_mask |= AppDispatch_SaveRequestMaskForKind(save_kind);
}

static void AppDispatch_HandleSaveRequestEvent(uint8_t save_kind)
{
    App_AcknowledgeSaveRequestEvent(save_kind);
    AppDispatch_SaveServiceRegisterRequest(save_kind);
}

void AppDispatch_SaveService(void)
{
    uint8_t combined_mask = AppDispatch_SaveCombinedRequestMask();
    uint8_t runtime_state_mask = AppDispatch_SaveRequestMaskForKind(APP_EVENT_SAVE_KIND_RUNTIME_STATE);

    switch (app_dispatch_save_service_state)
    {
    case APP_DISPATCH_SAVE_SERVICE_STATE_IDLE:
        if ((app_dispatch_save_service_requested_mask & combined_mask) != 0U)
        {
            if (!RuntimeConfig_IsDirty() && !Presets_IsDirty())
            {
                app_dispatch_save_service_requested_mask &= (uint8_t)~combined_mask;
                return;
            }

            app_dispatch_save_service_state = APP_DISPATCH_SAVE_SERVICE_STATE_SHOW_COMBINED_POPUP;
            return;
        }

        if ((app_dispatch_save_service_requested_mask & runtime_state_mask) != 0U)
            app_dispatch_save_service_state = APP_DISPATCH_SAVE_SERVICE_STATE_SAVE_RUNTIME_STATE;
        return;

    case APP_DISPATCH_SAVE_SERVICE_STATE_SHOW_COMBINED_POPUP:
        Display_ShowSavingPopup();
        app_dispatch_save_service_state = APP_DISPATCH_SAVE_SERVICE_STATE_SAVE_COMBINED;
        return;

    case APP_DISPATCH_SAVE_SERVICE_STATE_SAVE_COMBINED:
        (void)Presets_SaveIfDirty();
        app_dispatch_save_service_requested_mask &= (uint8_t)~combined_mask;
        app_dispatch_save_service_state = APP_DISPATCH_SAVE_SERVICE_STATE_HIDE_COMBINED_POPUP;
        return;

    case APP_DISPATCH_SAVE_SERVICE_STATE_HIDE_COMBINED_POPUP:
        Display_HideSavingPopup(AppUi_GetCurrentDisplayPreset());
        app_dispatch_save_service_state = APP_DISPATCH_SAVE_SERVICE_STATE_IDLE;
        return;

    case APP_DISPATCH_SAVE_SERVICE_STATE_SAVE_RUNTIME_STATE:
#if BPM_FLASH_WRITES_ENABLED
        RuntimeState_Flash_Save(g_bpm, active_preset_index, current_bank);
        LED_FlashPulse();
#endif
        app_dispatch_save_service_requested_mask &= (uint8_t)~runtime_state_mask;
        app_dispatch_save_service_state = APP_DISPATCH_SAVE_SERVICE_STATE_IDLE;
        return;

    default:
        app_dispatch_save_service_state = APP_DISPATCH_SAVE_SERVICE_STATE_IDLE;
        return;
    }
}

void AppDispatch_ProcessPendingEvents(void)
{
    AppEvent_t event;

    while (AppEvent_Pop(&event))
    {
        switch (event.type)
        {
        case APP_EVENT_TYPE_TAP_PRESS:
            AppTempo_HandleTapPress(event.tick);
            break;

        case APP_EVENT_TYPE_ENCODER_PRESS:
            AppDispatch_HandleEncoderPressEvent((uint8_t)event.value);
            break;

        case APP_EVENT_TYPE_ENCODER_TURN:
            AppDispatch_HandleEncoderTurnEvent(event.source, (int8_t)event.value);
            break;

        case APP_EVENT_TYPE_FOOTSWITCH_EDGE:
            if (APP_EVENT_SOURCE_IS_FOOTSWITCH(event.source))
            {
                Button_ProcessInterruptEvent(APP_EVENT_SOURCE_TO_FOOTSWITCH_INDEX(event.source),
                                             (uint8_t)event.value,
                                             event.tick);
            }
            break;

        case APP_EVENT_TYPE_BANK_STEP:
            AppDispatch_HandleBankStepEvent((int8_t)event.value, event.source);
            break;

        case APP_EVENT_TYPE_PRESET_ACTIVATE:
            AppDispatch_HandlePresetActivateEvent((uint8_t)event.value);
            break;

        case APP_EVENT_TYPE_PRESET_ACTIVATE_RANDOM:
            AppDispatch_HandlePresetActivateRandomEvent();
            break;

        case APP_EVENT_TYPE_PRESET_ACTIVATE_MUTE:
            AppDispatch_HandlePresetActivateMuteEvent();
            break;

        case APP_EVENT_TYPE_SPECIAL_FUNCTION_TOGGLE:
            AppDispatch_HandleSpecialFunctionToggleEvent((uint8_t)event.value);
            break;

        case APP_EVENT_TYPE_SCREENSAVER_WAKE:
            AppDispatch_HandleScreensaverWakeEvent();
            break;

        case APP_EVENT_TYPE_SCREENSAVER_ACTIVITY:
            AppDispatch_HandleScreensaverActivityEvent();
            break;

        case APP_EVENT_TYPE_PERIODIC_UI_SERVICE:
            AppDispatch_HandlePeriodicUiServiceEvent();
            break;

        case APP_EVENT_TYPE_REDRAW_ACTIVE_DISPLAY:
            AppDispatch_HandleRedrawActiveDisplayEvent();
            break;

        case APP_EVENT_TYPE_REDRAW_MAIN_SCREEN:
            AppDispatch_HandleRedrawMainScreenEvent();
            break;

        case APP_EVENT_TYPE_SAVE_REQUEST:
            AppDispatch_HandleSaveRequestEvent(event.source);
            break;

        default:
            break;
        }
    }
}