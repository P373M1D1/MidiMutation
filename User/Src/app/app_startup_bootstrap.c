#include "app/app_startup_bootstrap.h"

#include "app/app_board_init.h"
#include "app/app_button_monitor.h"
#include "app/app_input.h"
#include "app/app_input_board.h"
#include "app/app_metronome.h"
#include "app/app_state.h"
#include "app/app_ui.h"
#include "bpm_functions.h"
#include "display_functions.h"
#include "midi_functions.h"
#include "presets.h"

void AppStartupBootstrap_Run(void)
{
    AppBoard_InitStartupPeripherals();

    AppState_SetTempoBpm(BPM_Flash_Load());
    if (!BPM_Flash_IsValid())
        AppState_SetTempoBpm(BPM_DEFAULT);

    AppState_SelectBank(0U);
    AppState_SetActivePresetIndex(0U);
    AppMetronome_Init();
    MidiClockOutputInit(AppState_GetTempoBpm());
    App_ActivatePreset(AppState_GetActivePresetIndex());
    Display_DrawMainScreen(AppUi_GetCurrentDisplayPreset(), AppState_GetTempoBpm());
    AppState_ClearRuntimeStateSaveSchedule();
    AppInputBoard_InitGpio();
    AppInput_Init();
    AppButtonMonitor_Init();
}
