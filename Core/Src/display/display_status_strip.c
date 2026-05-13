#include <stdio.h>
#include <string.h>

#include "display_functions.h"
#include "display/display_compose_helpers.h"
#include "display/display_internal.h"
#include "display/display_layout.h"
#include "midi_functions.h"
#include "runtime_config.h"

/* Top status strip renderer.
 *
 * This file owns the compact BPM and bar.beat readout at the top of the main
 * screen, including internal/external clock formatting, sync-lost messaging,
 * and redraw suppression so tiny tempo changes do not repaint more than needed. */

typedef enum
{
    BPM_TEXT_MODE_INTERNAL = 0,
    BPM_TEXT_MODE_EXTERNAL = 1
} DisplayBpmTextMode_t;

static void Display_UpdateTransportBarBeat(void)
{
    uint8_t bar;
    uint8_t beat;
    uint8_t external_signal_present;
    uint8_t sync_lost;
    uint8_t stop_latched;
    char next_text[5];

    external_signal_present = MidiClockIsExternalSignalPresent();
    sync_lost = MidiClockIsSyncLost();
    stop_latched = MidiTransportStopLatched();

    if (MidiClockGetBarBeat(&bar, &beat))
    {
        if (bar > RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MAX)
            bar = RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MAX;
        if (beat > 9U)
            beat = 9U;

        if (bar >= 10U)
        {
            next_text[0] = (char)('0' + (bar / 10U));
            next_text[1] = (char)('0' + (bar % 10U));
            next_text[2] = '.';
            next_text[3] = (char)('0' + beat);
            next_text[4] = '\0';
        }
        else
        {
            next_text[0] = (char)('0' + bar);
            next_text[1] = '.';
            next_text[2] = (char)('0' + beat);
            next_text[3] = '\0';
        }
    }
    else if (external_signal_present || sync_lost || stop_latched)
    {
        /* Show a placeholder when transport state exists but no stable bar.beat
         * is available yet, instead of leaving stale numbers on screen. */
        strcpy(next_text, "-.-");
    }
    else
    {
        next_text[0] = '\0';
    }

    if (strcmp(next_text, display_state.transport_barbeat_text) == 0)
        return;

    Display_ComposeClear(TRANSPORT_BARBEAT_TEXT_W,
                         MAIN_PRESET_FONT.height,
                         DISPLAY_BG_COLOUR);

    if (next_text[0] != '\0')
    {
        Display_ComposeString32(TRANSPORT_BARBEAT_TEXT_W,
                                MAIN_PRESET_FONT.height,
                                0U,
                                0U,
                                next_text,
                                MAIN_PRESET_FONT,
                                TRANSPORT_BARBEAT_TEXT_COLOUR,
                                DISPLAY_BG_COLOUR);
    }

    Display_ComposeBlit(TRANSPORT_BARBEAT_TEXT_X,
                        TRANSPORT_BARBEAT_TEXT_Y,
                        TRANSPORT_BARBEAT_TEXT_W,
                        MAIN_PRESET_FONT_CELL_HEIGHT);

    strcpy(display_state.transport_barbeat_text, next_text);
}

static void Display_FormatBpmText(char *buffer,
                                  size_t buffer_size,
                                  DisplayBpmTextMode_t mode,
                                  uint16_t bpm_or_bpm_x10)
{
    char text[20];

    if (mode == BPM_TEXT_MODE_EXTERNAL)
    {
        snprintf(text, sizeof(text), "EXT %u.%u BPM",
                 (unsigned)(bpm_or_bpm_x10 / 10U),
                 (unsigned)(bpm_or_bpm_x10 % 10U));

        snprintf(buffer, buffer_size, "%*s", (int)BPM_EXT_TEXT_CHARS, text);
        return;
    }

    snprintf(buffer, buffer_size, "INT %u", (unsigned)bpm_or_bpm_x10);
}

static uint16_t Display_GetExternalBpmHysteresisX10(uint16_t reference_bpm_x10)
{
    uint32_t hysteresis_x10 = (((uint32_t)reference_bpm_x10 * BPM_EXT_HYSTERESIS_BPS) + 5000U) / 10000U;

    /* External clock estimates can wobble by small fractions every update, so
     * hold off redraws until the change exceeds a tempo-scaled deadband. */
    if (hysteresis_x10 < BPM_EXT_HYSTERESIS_MIN_X10)
        hysteresis_x10 = BPM_EXT_HYSTERESIS_MIN_X10;

    return (uint16_t)hysteresis_x10;
}

static void Display_DrawBpmAreaComposed(uint16_t primary_text_x,
                                        const char *primary_text,
                                        uint16_t primary_colour,
                                        uint16_t secondary_text_x,
                                        const char *secondary_text,
                                        uint16_t secondary_colour)
{
    Display_ComposeClear(BPM_DISPLAY_AREA_W,
                         BPM_FONT.height,
                         BPM_BG_COLOUR);

    if (primary_text && primary_text[0] != '\0')
    {
        Display_ComposeString32(BPM_DISPLAY_AREA_W,
                                BPM_FONT.height,
                                (uint16_t)(primary_text_x - BPM_DISPLAY_AREA_X),
                                0U,
                                primary_text,
                                BPM_FONT,
                                primary_colour,
                                BPM_BG_COLOUR);
    }

    if (secondary_text && secondary_text[0] != '\0')
    {
        Display_ComposeString32(BPM_DISPLAY_AREA_W,
                                BPM_FONT.height,
                                (uint16_t)(secondary_text_x - BPM_DISPLAY_AREA_X),
                                0U,
                                secondary_text,
                                BPM_FONT,
                                secondary_colour,
                                BPM_BG_COLOUR);
    }

    Display_ComposeBlit(BPM_DISPLAY_AREA_X,
                        BPM_TEXT_Y,
                        BPM_DISPLAY_AREA_W,
                        BPM_FONT.height);
}

void Display_UpdateBPM(uint16_t bpm)
{
    uint16_t display_bpm_x10;
    uint8_t use_external;
    uint8_t sync_lost;
    uint8_t was_sync_lost;
    uint32_t now_ms;
    uint8_t full_redraw;
    char buf[20];

    if (display_state.menu_mode_active)
        return;

    display_bpm_x10 = (uint16_t)(bpm * 10U);
    use_external = MidiClockGetExternalBpmX10(&display_bpm_x10);
    sync_lost = MidiClockIsSyncLost();
    was_sync_lost = display_state.bpm_display_sync_lost;
    now_ms = HAL_GetTick();

    Display_UpdateTransportBarBeat();

    if (sync_lost)
    {
        if (display_state.bpm_display_valid && display_state.bpm_display_sync_lost)
            return;

        Display_DrawBpmAreaComposed(BPM_SYNC_LOST_X,
                                    BPM_SYNC_LOST_TEXT,
                                    BPM_SYNC_LOST_COLOUR,
                                    BPM_DISPLAY_AREA_X,
                                    NULL,
                                    BPM_SYNC_LOST_COLOUR);

        display_state.bpm_display_valid = 1U;
        display_state.bpm_display_external = 0U;
        display_state.bpm_display_sync_lost = 1U;
        display_state.bpm_display_value_x10 = 0U;
        display_state.bpm_display_external_update_tick = 0U;
        return;
    }

    display_state.bpm_display_sync_lost = 0U;

    if (!use_external)
    {
        if (display_state.bpm_display_valid
         && !was_sync_lost
         && !display_state.bpm_display_external
         && display_state.bpm_display_value_x10 == display_bpm_x10)
        {
            return;
        }

        Display_FormatBpmText(buf, sizeof(buf), BPM_TEXT_MODE_INTERNAL, bpm);

        uint8_t internal_head_len = (uint8_t)strlen(buf);
        uint16_t internal_head_x = (uint16_t)(BPM_INTERNAL_SUFFIX_TEXT_X - ((uint16_t)internal_head_len * BPM_FONT.width));

        Display_DrawBpmAreaComposed(internal_head_x,
                                    buf,
                                    BPM_INTERNAL_COLOUR,
                                    BPM_INTERNAL_SUFFIX_TEXT_X,
                                    BPM_INTERNAL_SUFFIX_TEXT,
                                    BPM_INTERNAL_COLOUR);

        display_state.bpm_display_valid = 1U;
        display_state.bpm_display_external = 0U;
        display_state.bpm_display_sync_lost = 0U;
        display_state.bpm_display_value_x10 = display_bpm_x10;
        display_state.bpm_display_external_update_tick = 0U;
        return;
    }

    /* Switching from internal to external clock, or recovering from sync loss,
     * rebuilds the whole BPM area because both primary and suffix text change. */
    full_redraw = (uint8_t)(!display_state.bpm_display_valid || was_sync_lost || !display_state.bpm_display_external);
    if (!full_redraw)
    {
        uint16_t delta_x10;
        uint16_t hysteresis_x10;
        uint16_t upper_threshold_x10;
        uint16_t lower_threshold_x10;

        if ((now_ms - display_state.bpm_display_external_update_tick) < BPM_EXT_UPDATE_MIN_INTERVAL_MS)
            return;

        hysteresis_x10 = Display_GetExternalBpmHysteresisX10(display_state.bpm_display_value_x10);
        upper_threshold_x10 = (uint16_t)(display_state.bpm_display_value_x10 + hysteresis_x10);
        lower_threshold_x10 = (display_state.bpm_display_value_x10 > hysteresis_x10)
            ? (uint16_t)(display_state.bpm_display_value_x10 - hysteresis_x10)
            : 0U;

        if (display_bpm_x10 <= upper_threshold_x10 && display_bpm_x10 >= lower_threshold_x10)
            return;

        delta_x10 = (display_bpm_x10 >= display_state.bpm_display_value_x10)
            ? (uint16_t)(display_bpm_x10 - display_state.bpm_display_value_x10)
            : (uint16_t)(display_state.bpm_display_value_x10 - display_bpm_x10);

        if (delta_x10 < BPM_EXT_FORCE_UPDATE_DELTA_X10)
        {
            if (display_bpm_x10 > display_state.bpm_display_value_x10)
            {
                display_bpm_x10 = (uint16_t)(display_state.bpm_display_value_x10 + BPM_EXT_SLEW_STEP_X10);
            }
            else
            {
                display_bpm_x10 = (display_state.bpm_display_value_x10 > BPM_EXT_SLEW_STEP_X10)
                    ? (uint16_t)(display_state.bpm_display_value_x10 - BPM_EXT_SLEW_STEP_X10)
                    : 0U;
            }
        }
    }

    Display_FormatBpmText(buf, sizeof(buf), BPM_TEXT_MODE_EXTERNAL, display_bpm_x10);
    Display_DrawBpmAreaComposed(BPM_EXT_TEXT_X,
                                buf,
                                EXT_BPM_COLOUR,
                                BPM_DISPLAY_AREA_X,
                                NULL,
                                EXT_BPM_COLOUR);

    display_state.bpm_display_valid = 1U;
    display_state.bpm_display_external = 1U;
    display_state.bpm_display_sync_lost = 0U;
    display_state.bpm_display_value_x10 = display_bpm_x10;
    display_state.bpm_display_external_update_tick = now_ms;
}