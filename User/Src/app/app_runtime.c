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
#include "midi_functions.h"

#define EXT_CLOCK_HOLDOVER_MIRROR_ENABLED 1U

void AppRuntime_ServiceForeground(void)
{
    /* Deferred work stays in the foreground loop: BPM/UI updates and flash-save
     * scheduling on one side, queued EXTI button events on the other. */
#if EXT_CLOCK_HOLDOVER_MIRROR_ENABLED
    AppTempo_ExternalClockHoldoverMirrorService();
#endif
    AppInput_ProcessPending();
    AppDispatch_ProcessPendingEvents();
    AppUi_ServiceMenuPreviewHold(AppInput_Encoder2SwitchIsPressed());
    MidiOutputSchedulerService();
    AppMetronome_Service();
    App_QueuePeriodicUiServiceEvent();
    BPM_Service();
    AppDispatch_ProcessPendingEvents();
    AppUi_ServiceRender();
    AppSaveService_Service();
    AppEvent_DiagnosticService();
    MidiClockDiagnosticService();
    Button_ProcessPendingEvents();
}