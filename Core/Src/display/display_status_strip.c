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
 * and fixed-cadence external BPM updates so live tempo stays readable. */

typedef enum
{
    BPM_TEXT_MODE_INTERNAL = 0,
    BPM_TEXT_MODE_EXTERNAL = 1
} DisplayBpmTextMode_t;

typedef enum
{
    DISPLAY_TRANSPORT_STATUS_NONE = 0,
    DISPLAY_TRANSPORT_STATUS_BARBEAT,
    DISPLAY_TRANSPORT_STATUS_CLOCK_IN,
    DISPLAY_TRANSPORT_STATUS_SYNC_LOST,
} DisplayTransportStatusMode_t;

#define BPM_DISPLAY_DIAGNOSTICS_ENABLED    1U
#define BPM_DISPLAY_DIAGNOSTIC_REPORT_MS 1000U
#define BPM_SYNCING_TEXT                "SYNCING"

static void Display_DrawTransportBarBeat(const char *text);
static void Display_DrawTransportAlert(DisplayTransportStatusMode_t mode);
static uint16_t Display_GetBpmDeltaX10(uint16_t lhs, uint16_t rhs);
static uint16_t Display_GetExternalBpmHysteresisX10(uint16_t bpm_x10);
static uint16_t Display_SlewExternalBpmX10(uint16_t current_bpm_x10, uint16_t target_bpm_x10);
static uint8_t Display_ShouldShowSyncingHeader(void);
static void Display_FormatRightAlignedStatusText(char *buffer,
                                                 size_t buffer_size,
                                                 const char *text,
                                                 uint8_t padded_chars);

static uint16_t Display_GetBpmDeltaX10(uint16_t lhs, uint16_t rhs)
{
    return (lhs >= rhs) ? (uint16_t)(lhs - rhs) : (uint16_t)(rhs - lhs);
}

static uint16_t Display_GetExternalBpmHysteresisX10(uint16_t bpm_x10)
{
    uint16_t hysteresis_x10 = (uint16_t)(((uint32_t)bpm_x10 * BPM_EXT_HYSTERESIS_BPS) / 10000U);

    return (hysteresis_x10 >= BPM_EXT_HYSTERESIS_MIN_X10)
        ? hysteresis_x10
        : BPM_EXT_HYSTERESIS_MIN_X10;
}

static uint16_t Display_SlewExternalBpmX10(uint16_t current_bpm_x10, uint16_t target_bpm_x10)
{
    uint16_t delta_x10 = Display_GetBpmDeltaX10(current_bpm_x10, target_bpm_x10);

    if (delta_x10 <= BPM_EXT_SLEW_STEP_X10)
        return target_bpm_x10;

    if (target_bpm_x10 > current_bpm_x10)
        return (uint16_t)(current_bpm_x10 + BPM_EXT_SLEW_STEP_X10);

    return (uint16_t)(current_bpm_x10 - BPM_EXT_SLEW_STEP_X10);
}

static uint8_t Display_ShouldShowSyncingHeader(void)
{
    return (uint8_t)(MidiTransportIsRunning()
        && MidiClockIsExternalSignalPresent()
        && !MidiClockIsPublicationReady());
}

static void Display_FormatRightAlignedStatusText(char *buffer,
                                                 size_t buffer_size,
                                                 const char *text,
                                                 uint8_t padded_chars)
{
    snprintf(buffer, buffer_size, "%*s", (int)padded_chars, text);
}

static void Display_UpdateTransportBarBeat(void)
{
    uint8_t bar;
    uint8_t beat;
    uint8_t have_barbeat;
    uint8_t transport_running;
    uint8_t external_signal_present;
    uint8_t sync_lost;
    uint8_t stop_latched;
    DisplayTransportStatusMode_t mode = DISPLAY_TRANSPORT_STATUS_NONE;
    char next_text[5];

    sync_lost = MidiClockIsSyncLost();
    transport_running = MidiTransportIsRunning();
    external_signal_present = MidiClockIsExternalSignalPresent();
    stop_latched = MidiTransportStopLatched();
    have_barbeat = MidiClockGetBarBeat(&bar, &beat);

    if (sync_lost)
    {
        mode = external_signal_present
            ? DISPLAY_TRANSPORT_STATUS_CLOCK_IN
            : DISPLAY_TRANSPORT_STATUS_SYNC_LOST;
    }
    else if (have_barbeat && (transport_running || stop_latched))
    {
        mode = DISPLAY_TRANSPORT_STATUS_BARBEAT;
    }
    else if (external_signal_present)
    {
        mode = DISPLAY_TRANSPORT_STATUS_CLOCK_IN;
    }

    if (mode == DISPLAY_TRANSPORT_STATUS_BARBEAT)
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
    else
    {
        next_text[0] = '\0';
    }

    if (mode == DISPLAY_TRANSPORT_STATUS_BARBEAT)
    {
        if (display_state.transport_status_valid
         && display_state.transport_status_mode == (uint8_t)mode
         && strcmp(next_text, display_state.transport_barbeat_text) == 0)
        {
            return;
        }

        Display_DrawTransportBarBeat(next_text);
        strcpy(display_state.transport_barbeat_text, next_text);
    }
    else
    {
        if (display_state.transport_status_valid
         && display_state.transport_status_mode == (uint8_t)mode)
        {
            return;
        }

        Display_DrawTransportAlert(mode);
        display_state.transport_barbeat_text[0] = '\0';
    }

    display_state.transport_status_valid = 1U;
    display_state.transport_status_mode = (uint8_t)mode;
    display_state.transport_status_blink_visible = 1U;
}

static void Display_DrawTransportBarBeat(const char *text)
{
    Display_ComposeClear(TRANSPORT_STATUS_AREA_W,
                         TRANSPORT_STATUS_AREA_H,
                         DISPLAY_BG_COLOUR);

    if (text && text[0] != '\0')
    {
        Display_ComposeString32(TRANSPORT_STATUS_AREA_W,
                                TRANSPORT_STATUS_AREA_H,
                                0U,
                                0U,
                                text,
                                MAIN_PRESET_FONT,
                                TRANSPORT_BARBEAT_TEXT_COLOUR,
                                DISPLAY_BG_COLOUR);
    }

    Display_ComposeBlit(TRANSPORT_STATUS_AREA_X,
                        TRANSPORT_STATUS_AREA_Y,
                        TRANSPORT_STATUS_AREA_W,
                        TRANSPORT_STATUS_AREA_H);
}

static void Display_DrawTransportAlert(DisplayTransportStatusMode_t mode)
{
    const char *text = NULL;
    uint16_t colour = BPM_SYNC_LOST_COLOUR;
    uint16_t text_x = 0U;
    uint16_t text_y = 0U;

    Display_ComposeClear(TRANSPORT_STATUS_AREA_W,
                         TRANSPORT_STATUS_AREA_H,
                         DISPLAY_BG_COLOUR);

    if (mode == DISPLAY_TRANSPORT_STATUS_NONE)
    {
        Display_ComposeBlit(TRANSPORT_STATUS_AREA_X,
                            TRANSPORT_STATUS_AREA_Y,
                            TRANSPORT_STATUS_AREA_W,
                            TRANSPORT_STATUS_AREA_H);
        return;
    }

    switch (mode)
    {
    case DISPLAY_TRANSPORT_STATUS_CLOCK_IN:
        text = "CLOCK IN";
        colour = EXT_BPM_COLOUR;
        break;

    case DISPLAY_TRANSPORT_STATUS_SYNC_LOST:
        text = "SYNC LOST";
        break;

    case DISPLAY_TRANSPORT_STATUS_NONE:
    case DISPLAY_TRANSPORT_STATUS_BARBEAT:
    default:
        Display_ComposeBlit(TRANSPORT_STATUS_AREA_X,
                            TRANSPORT_STATUS_AREA_Y,
                            TRANSPORT_STATUS_AREA_W,
                            TRANSPORT_STATUS_AREA_H);
        return;
    }

    Display_ComposeString32(TRANSPORT_STATUS_AREA_W,
                            TRANSPORT_STATUS_AREA_H,
                            text_x,
                            text_y,
                            text,
                            TRANSPORT_STATUS_MESSAGE_FONT,
                            colour,
                            DISPLAY_BG_COLOUR);

    Display_ComposeBlit(TRANSPORT_STATUS_AREA_X,
                        TRANSPORT_STATUS_AREA_Y,
                        TRANSPORT_STATUS_AREA_W,
                        TRANSPORT_STATUS_AREA_H);
}

static void Display_FormatBpmText(char *buffer,
                                  size_t buffer_size,
                                  DisplayBpmTextMode_t mode,
                                  uint16_t bpm_or_bpm_x10)
{
    char text[20];

    if (mode == BPM_TEXT_MODE_EXTERNAL)
    {
        if (bpm_or_bpm_x10 < BPM_EXT_DISPLAY_MIN_X10)
        {
            snprintf(text, sizeof(text), "%s", BPM_EXT_LOW_TEXT);
        }
        else if (bpm_or_bpm_x10 > BPM_EXT_DISPLAY_MAX_X10)
        {
            snprintf(text, sizeof(text), "%s", BPM_EXT_HIGH_TEXT);
        }
        else
        {
            snprintf(text, sizeof(text), "EXT %u.%u BPM",
                     (unsigned)(bpm_or_bpm_x10 / 10U),
                     (unsigned)(bpm_or_bpm_x10 % 10U));
        }

        Display_FormatRightAlignedStatusText(buffer,
                                             buffer_size,
                                             text,
                                             BPM_EXT_TEXT_CHARS);
        return;
    }

    snprintf(buffer, buffer_size, "INT %u", (unsigned)bpm_or_bpm_x10);
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
    uint16_t measured_bpm_x10 = 0U;
    uint16_t delta_x10;
    uint16_t hysteresis_x10;
    uint8_t use_external = 0U;
    uint8_t show_syncing;
    uint8_t sync_lost;
    uint32_t now_ms;
    uint8_t full_redraw;
    char buf[20];

    if (display_state.menu_mode_active && !display_state.menu_preview_active)
        return;

    display_bpm_x10 = (uint16_t)(bpm * 10U);
    sync_lost = MidiClockIsSyncLost();
    show_syncing = Display_ShouldShowSyncingHeader();
    if (!show_syncing && !sync_lost && MidiClockGetMeasuredExternalBpmX10(&measured_bpm_x10))
    {
        display_bpm_x10 = measured_bpm_x10;
        use_external = 1U;
    }
    now_ms = HAL_GetTick();

    Display_UpdateTransportBarBeat();

    if (show_syncing)
    {
        if (display_state.bpm_display_valid
         && display_state.bpm_display_syncing)
        {
            display_state.bpm_display_sync_lost = sync_lost;
            return;
        }

        Display_FormatRightAlignedStatusText(buf,
                                             sizeof(buf),
                                             BPM_SYNCING_TEXT,
                                             BPM_EXT_TEXT_CHARS);
        Display_DrawBpmAreaComposed(BPM_EXT_TEXT_X,
                                    buf,
                                    EXT_BPM_COLOUR,
                                    BPM_DISPLAY_AREA_X,
                                    NULL,
                                    EXT_BPM_COLOUR);

        display_state.bpm_display_valid = 1U;
        display_state.bpm_display_external = 0U;
        display_state.bpm_display_syncing = 1U;
        display_state.bpm_display_sync_lost = sync_lost;
        display_state.bpm_display_value_x10 = display_bpm_x10;
        display_state.bpm_display_external_update_tick = 0U;
        return;
    }

    if (!use_external)
    {
        if (display_state.bpm_display_valid
         && !display_state.bpm_display_external
         && !display_state.bpm_display_syncing
         && display_state.bpm_display_value_x10 == display_bpm_x10)
        {
            display_state.bpm_display_sync_lost = sync_lost;
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
        display_state.bpm_display_syncing = 0U;
        display_state.bpm_display_sync_lost = sync_lost;
        display_state.bpm_display_value_x10 = display_bpm_x10;
        display_state.bpm_display_external_update_tick = 0U;
        return;
    }

    full_redraw = (uint8_t)(!display_state.bpm_display_valid
        || !display_state.bpm_display_external
        || display_state.bpm_display_syncing);
    if (!full_redraw)
    {
        if ((now_ms - display_state.bpm_display_external_update_tick) < BPM_EXT_UPDATE_MIN_INTERVAL_MS)
        {
            display_state.bpm_display_sync_lost = sync_lost;
            return;
        }

        delta_x10 = Display_GetBpmDeltaX10(display_state.bpm_display_value_x10, display_bpm_x10);
        hysteresis_x10 = Display_GetExternalBpmHysteresisX10(display_state.bpm_display_value_x10);

        if (delta_x10 <= hysteresis_x10)
        {
            display_state.bpm_display_sync_lost = sync_lost;
            return;
        }

        if (delta_x10 < BPM_EXT_FORCE_UPDATE_DELTA_X10)
            display_bpm_x10 = Display_SlewExternalBpmX10(display_state.bpm_display_value_x10, display_bpm_x10);

        if (display_state.bpm_display_value_x10 == display_bpm_x10)
        {
            display_state.bpm_display_sync_lost = sync_lost;
            return;
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
    display_state.bpm_display_syncing = 0U;
    display_state.bpm_display_sync_lost = sync_lost;
    display_state.bpm_display_value_x10 = display_bpm_x10;
    display_state.bpm_display_external_update_tick = now_ms;
}

void Display_BpmDiagnosticService(void)
{
#if BPM_DISPLAY_DIAGNOSTICS_ENABLED
    static uint32_t last_report_tick = 0U;
    uint32_t now_ms = HAL_GetTick();
    uint16_t raw_bpm_x10 = 0U;
    uint16_t source_bpm_x10 = 0U;
    uint16_t displayed_bpm_x10 = display_state.bpm_display_value_x10;
    uint16_t delta_x10 = 0U;
    uint16_t hysteresis_x10 = 0U;
    uint32_t age_ms = 0U;
    uint8_t estimator_window_pulses;
    uint8_t estimator_valid;
    uint8_t publication_ready;
    char sync_state = '-';
    char transport_confidence = '-';
    char live_lock = '-';
    uint8_t raw_valid;
    uint8_t source_valid;
    uint8_t display_valid = display_state.bpm_display_valid;
    uint8_t display_external = display_state.bpm_display_external;
    uint8_t sync_lost = MidiClockIsSyncLost();
    uint8_t hold = 0U;
    uint8_t slew_pending = 0U;

    if ((now_ms - last_report_tick) < BPM_DISPLAY_DIAGNOSTIC_REPORT_MS)
        return;

    last_report_tick = now_ms;
    raw_valid = MidiClockGetRawExternalBpmX10(&raw_bpm_x10);
    source_valid = MidiClockGetMeasuredExternalBpmX10(&source_bpm_x10);
    estimator_window_pulses = MidiClockGetExternalBpmWindowPulses();
    estimator_valid = MidiClockIsEstimatorValid();
    publication_ready = MidiClockIsPublicationReady();
    switch (MidiClockGetSyncState())
    {
    case MIDI_SYNC_STATE_LOCKED:
        sync_state = 'L';
        break;
    case MIDI_SYNC_STATE_TRACKING:
        sync_state = 'T';
        break;
    case MIDI_SYNC_STATE_ACQUIRE:
        sync_state = 'A';
        break;
    case MIDI_SYNC_STATE_HOLDOVER:
        sync_state = 'H';
        break;
    case MIDI_SYNC_STATE_RELOCK:
        sync_state = 'R';
        break;
    case MIDI_SYNC_STATE_REARM:
        sync_state = 'M';
        break;
    case MIDI_SYNC_STATE_LOST:
        sync_state = 'X';
        break;
    case MIDI_SYNC_STATE_IDLE:
        sync_state = 'I';
        break;
    default:
        sync_state = '-';
        break;
    }
    switch (MidiClockGetTransportConfidence())
    {
    case MIDI_CLOCK_TRANSPORT_CONFIDENCE_STABLE:
        transport_confidence = 'S';
        break;
    case MIDI_CLOCK_TRANSPORT_CONFIDENCE_TRACKING:
        transport_confidence = 'T';
        break;
    case MIDI_CLOCK_TRANSPORT_CONFIDENCE_OPERATIONAL:
        transport_confidence = 'O';
        break;
    default:
        transport_confidence = '-';
        break;
    }
    switch (MidiClockGetLockQuality())
    {
    case MIDI_CLOCK_LOCK_QUALITY_LOCKED:
        live_lock = 'L';
        break;
    case MIDI_CLOCK_LOCK_QUALITY_ACQUIRING:
        live_lock = 'A';
        break;
    default:
        live_lock = '-';
        break;
    }

    if (!raw_valid && !source_valid && !(display_valid && display_external) && !sync_lost)
        return;

    if (display_external && display_state.bpm_display_external_update_tick != 0U)
        age_ms = now_ms - display_state.bpm_display_external_update_tick;

    if (display_valid && display_external)
    {
        if (source_valid)
        {
            delta_x10 = Display_GetBpmDeltaX10(source_bpm_x10, displayed_bpm_x10);
            hysteresis_x10 = Display_GetExternalBpmHysteresisX10(displayed_bpm_x10);

            if (delta_x10 <= hysteresis_x10)
            {
                hold = 1U;
            }
            else if (delta_x10 < BPM_EXT_FORCE_UPDATE_DELTA_X10)
            {
                slew_pending = 1U;
                hold = (age_ms < BPM_EXT_UPDATE_MIN_INTERVAL_MS) ? 1U : 0U;
            }
        }
    }

          printf("BPMDIAG raw_bpm=%u.%u raw_valid=%u source_bpm=%u.%u source_valid=%u est_window=%u history_confidence=%c est_valid=%u publication_ready=%u sync_state=%c live_lock=%c display_bpm=%u.%u display_external=%u sync_lost=%u display_age_ms=%lu delta=%u.%u hysteresis=%u.%u hold=%u slew_pending=%u\r\n",
           (unsigned)(raw_bpm_x10 / 10U),
           (unsigned)(raw_bpm_x10 % 10U),
           (unsigned)raw_valid,
           (unsigned)(source_bpm_x10 / 10U),
           (unsigned)(source_bpm_x10 % 10U),
           (unsigned)source_valid,
              (unsigned)estimator_window_pulses,
              transport_confidence,
              (unsigned)estimator_valid,
              (unsigned)publication_ready,
              sync_state,
              live_lock,
           (unsigned)(displayed_bpm_x10 / 10U),
           (unsigned)(displayed_bpm_x10 % 10U),
           (unsigned)display_external,
           (unsigned)sync_lost,
           (unsigned long)age_ms,
           (unsigned)(delta_x10 / 10U),
           (unsigned)(delta_x10 % 10U),
           (unsigned)(hysteresis_x10 / 10U),
           (unsigned)(hysteresis_x10 % 10U),
           (unsigned)hold,
           (unsigned)slew_pending);
#endif
}