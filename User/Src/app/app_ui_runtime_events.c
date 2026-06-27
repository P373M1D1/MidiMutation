#include "app/app_ui_events.h"

#include "app_event.h"
#include "app/app_activation.h"
#include "app/app_requests.h"
#include "app/app_special_functions.h"
#include "app/app_state.h"
#include "app/app_ui.h"
#include "display/display_menu_page_midi_monitor.h"
#include "display_functions.h"
#include "led_functions.h"
#include "midi_dispatch.h"
#include "midi/midi_monitor.h"
#include "runtime_config.h"
#include "stm32f4xx_hal.h"

static void AppUiEvents_SendFunctionButtonProgramMessages(const RuntimeConfigProgramMessage_t *messages,
                                                          uint8_t message_count);
static void AppUiEvents_SendFunctionButtonCcMessages(const PresetCCSlot_t *messages,
                                                     uint8_t message_count);
static void AppUiEvents_HandleSpecialFunctionToggle(uint8_t state_active);
static void AppUiEvents_HandleUiTick100Ms(void);
static void AppUiEvents_HandleMidiMonitorChanged(void);
static void AppUiEvents_HandleRedrawActiveDisplay(void);
static void AppUiEvents_HandleRedrawMainScreen(void);

/* Handles UI-facing events that were already classified by the dispatcher. */
uint8_t AppUiEvents_HandleEvent(const AppEvent_t *event)
{
    if (event == 0)
        return 0U;

    switch (event->type)
    {
    case APP_EVENT_TYPE_ENCODER_PRESS:
        AppUiEvents_HandleEncoderPress((uint8_t)event->value);
        return 1U;

    case APP_EVENT_TYPE_ENCODER_TURN:
        AppUiEvents_HandleEncoderTurn(event->source, (int8_t)event->value);
        return 1U;

    case APP_EVENT_TYPE_SPECIAL_FUNCTION_TOGGLE:
        AppUiEvents_HandleSpecialFunctionToggle((uint8_t)event->value);
        return 1U;

    case APP_EVENT_TYPE_UI_TICK_100MS:
        AppUiEvents_HandleUiTick100Ms();
        return 1U;

    case APP_EVENT_TYPE_MIDI_MONITOR_CHANGED:
        AppUiEvents_HandleMidiMonitorChanged();
        return 1U;

    case APP_EVENT_TYPE_REDRAW_ACTIVE_DISPLAY:
        AppUiEvents_HandleRedrawActiveDisplay();
        return 1U;

    case APP_EVENT_TYPE_REDRAW_MAIN_SCREEN:
        AppUiEvents_HandleRedrawMainScreen();
        return 1U;

    default:
        return 0U;
    }
}

/* Resets UI state before a preset activation is applied. */
void AppUiEvents_PreparePresetActivation(uint8_t exit_preset_edit)
{
    if (exit_preset_edit && Display_PresetEditIsActive())
        Display_PresetEditExit();

    AppSpecialFunctions_Reset();
    Display_MainInfoScrollReset();
}

static void AppUiEvents_SendFunctionButtonProgramMessages(const RuntimeConfigProgramMessage_t *messages,
                                                          uint8_t message_count)
{
    if (!messages)
        return;

    for (uint8_t index = 0U; index < message_count; ++index)
    {
        const RuntimeConfigProgramMessage_t *message = &messages[index];

        if (message->channel == PRESET_CC_CHANNEL_UNUSED || message->program == PRESET_PROGRAM_NONE)
            continue;

        (void)MidiDispatch_SubmitProgramChange(
            message->channel,
            message->program,
            MIDI_COMMAND_POLICY_RELIABLE_ORDERED,
            0U,
            NULL);
    }
}

static void AppUiEvents_SendFunctionButtonCcMessages(const PresetCCSlot_t *messages,
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

        (void)MidiDispatch_SubmitControlChange(
            message->channel,
            message->cc_number,
            message->value,
            MIDI_COMMAND_POLICY_RELIABLE_ORDERED,
            0U,
            NULL);
    }
}

static void AppUiEvents_HandleSpecialFunctionToggle(uint8_t state_active)
{
    const RuntimeConfigFunctionButton_t *function_button = Presets_GetActiveFunctionButton();

    if (function_button)
    {
        if (state_active)
        {
            AppUiEvents_SendFunctionButtonProgramMessages(function_button->active_programs,
                                                          RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT);
            AppUiEvents_SendFunctionButtonCcMessages(function_button->active_cc,
                                                     RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT);
        }
        else
        {
            AppUiEvents_SendFunctionButtonProgramMessages(function_button->inactive_programs,
                                                          RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT);
            AppUiEvents_SendFunctionButtonCcMessages(function_button->inactive_cc,
                                                     RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT);
        }
    }

    AppUiEvents_HandleRedrawActiveDisplay();
}

static void AppUiEvents_HandleUiTick100Ms(void)
{
    App_AcknowledgeUiTick100MsEvent();
    AppActivation_ServiceDeferredUiRefresh(HAL_GetTick());
    Display_RefreshTransportInfoRows();

    Display_MenuMidiMonitorService();
    AppUi_PresetEditLearningService();
    LED_Update();
}

static void AppUiEvents_HandleMidiMonitorChanged(void)
{
    Display_MenuApplyMidiLearnIfPending();
    AppUi_PresetEditLearningService();
    MidiMonitor_AcknowledgeChangedEvent();
    AppUi_RequestLiveContentRefresh();
}

static void AppUiEvents_HandleRedrawActiveDisplay(void)
{
    AppUi_RequestActiveDisplayRefresh();
}

static void AppUiEvents_HandleRedrawMainScreen(void)
{
    App_AcknowledgeRedrawMainScreenEvent();
    AppUi_RequestMainScreenRefresh();
}
