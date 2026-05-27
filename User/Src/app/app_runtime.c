#include "app/app_runtime.h"

#include "app/app_dispatch.h"
#include "app/app_input.h"
#include "app/app_metronome.h"
#include "app/app_requests.h"
#include "app/app_save_service.h"
#include "app/app_tempo.h"
#include "app/app_ui.h"
#include "app_event.h"
#include "bpm_functions.h"
#include "button_functions.h"
#include "display_functions.h"
#include "led_functions.h"
#include "midi_functions.h"
#include "runtime_config.h"

#define EXT_CLOCK_HOLDOVER_MIRROR_ENABLED 1U

static uint8_t AppRuntime_IsFeedbackWindowActive(void);
static void AppRuntime_ServiceTimebendPopup(const RuntimeConfigGlobal_t *global);

void AppRuntime_ServiceForeground(void)
{
    const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();

    /* Tap-tempo CC mode delegates timing outward, so suppress local realtime
     * MIDI clock generation to avoid sending two competing tempo authorities. */
    if (global && global->sync_style == RUNTIME_CONFIG_SYNC_STYLE_TAP_TEMPO_CC)
        MidiClockSetRealtimeOutputEnabled(0U);
    else
        MidiClockSetRealtimeOutputEnabled(1U);

    MidiTimebendSetActive((global && global->live_enc2_mode == RUNTIME_CONFIG_LIVE_ENC2_MODE_TIMEBEND) ? 1U : 0U);
    AppRuntime_ServiceTimebendPopup(global);

    /* Keep transport-adjacent work running every loop, but defer input/UI/save
     * activity while the beat LED or metronome output is actively visible or
     * audible so those feedback windows stay clear under load. */
    MidiInput_ServiceRealtimeRx();
#if EXT_CLOCK_HOLDOVER_MIRROR_ENABLED
    AppTempo_ExternalClockHoldoverMirrorService();
#endif
    MidiOutputSchedulerService();
    AppMetronome_Service();
    LED_Update();
    App_QueuePeriodicUiServiceEvent();
    BPM_Service();

    /* When the unit is in a visible/audible feedback window, skip the heavier
     * UI/input path for this loop iteration to keep those short cues crisp. */
    if (AppRuntime_IsFeedbackWindowActive())
        return;

    AppInput_ProcessPending();
    AppDispatch_ProcessPendingEvents();
    AppUi_ServiceMenuPreviewHold(AppInput_Encoder2SwitchIsPressed());
    AppDispatch_ProcessPendingEvents();
    AppUi_ServiceRender();
    AppSaveService_Service();
    AppEvent_DiagnosticService();
    MidiClockDiagnosticService();
    Display_BpmDiagnosticService();
    AppMetronome_DiagnosticService();
    Button_ProcessPendingEvents();
}

static uint8_t AppRuntime_IsFeedbackWindowActive(void)
{
    /* Beat pulses and metronome ticks are treated as the critical short-lived
     * feedback windows that can justify briefly deferring lower-priority work. */
    return (uint8_t)(AppMetronome_IsOutputActive() || LED_IsPulseActive());
}

static void AppRuntime_ServiceTimebendPopup(const RuntimeConfigGlobal_t *global)
{
    uint8_t should_show = 0U;

    if (global
        && (global->live_enc2_mode == RUNTIME_CONFIG_LIVE_ENC2_MODE_TIMEBEND)
        && MidiTimebendIsEngaged()
        && !Display_MenuIsActive()
        && !Display_PresetEditIsActive())
    {
        should_show = 1U;
    }

    if (should_show)
    {
        Display_ShowTimebendPopup();
    }
    else
    {
        Display_HideTimebendPopup(AppUi_GetCurrentDisplayPreset());
    }
}