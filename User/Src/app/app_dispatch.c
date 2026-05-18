#include "app/app_dispatch.h"

#include "app/app_activation.h"
#include "app/app_button_events.h"
#include "app/app_save_service.h"
#include "app/app_tempo.h"
#include "app/app_ui_events.h"
#include "app_event.h"

void AppDispatch_ProcessPendingEvents(void)
{
    AppEvent_t event;

    while (AppEvent_Pop(&event))
    {
        if (AppTempo_HandleEvent(&event))
            continue;

        if (AppUiEvents_HandleEvent(&event))
            continue;

        if (AppButtonEvents_HandleEvent(&event))
            continue;

        if (AppActivation_HandleEvent(&event))
            continue;

        (void)AppSaveService_HandleEvent(&event);
    }
}