#include "app/app_ui_events.h"

#include "app_event.h"
#include "app/app_requests.h"
#include "app/app_state.h"
#include "app/app_tempo.h"
#include "app/app_ui.h"
#include "display/display_menu_page_midi_monitor.h"
#include "display_functions.h"
#include "presets.h"

#include <stddef.h>

void AppUiEvents_HandleEncoderTurn(uint8_t encoder_source, int8_t delta)
{
    const Preset_t *active_preset = AppState_GetActivePreset();
    uint8_t current_bank = AppState_GetCurrentBank();
    uint8_t active_preset_index = AppState_GetActivePresetIndex();

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
                AppUi_PresetEditMarkDirty();
                AppUi_RequestPresetEditFieldRefresh();
            }
            return;
        }

        AppTempo_ApplyEncoderStep(delta);
        return;

    default:
        return;
    }
}

void AppUiEvents_HandleEncoderPress(uint8_t press_mask)
{
    const Preset_t *active_preset = AppState_GetActivePreset();

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

    if ((press_mask & 0x01U) && Display_MenuIsActive())
    {
        if (Display_MenuTextEditIsActive())
        {
            Display_MenuTextEditExit();
            return;
        }

        AppUi_MenuBackOutOneLevel();
        return;
    }

    if ((press_mask & 0x04U) && Display_MenuIsActive())
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
                AppUi_RequestPresetEditModeRefresh();
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
                    AppUi_RequestPresetEditFieldRefresh();
            }
            else if (field.type == DISPLAY_PRESET_EDIT_FIELD_FUNCTION_BUTTON)
            {
                AppUi_PresetEditEnterFunctionButtonEditor();
            }
            else if (field.type == DISPLAY_PRESET_EDIT_FIELD_INIT)
            {
                Display_PresetInitConfirmEnter();
            }
        }
    }
}