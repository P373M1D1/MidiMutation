#include <stdio.h>
#include <string.h>

#include "display_functions.h"
#include "display/display_compose_helpers.h"
#include "display/display_internal.h"
#include "display/display_layout.h"
#include "midi/clock_engine.h"
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
#define BPM_DISPLAY_DIAG_VERBOSE_LOCKED     1U
#define BPM_SYNCING_TEXT                "SYNCING"

static uint32_t display_transport_render_counter = 0U;
static uint32_t display_transport_last_rendered_quarter = 0U;
static uint32_t display_transport_last_rendered_event = 0U;
static uint32_t display_transport_last_rendered_beat_to_ui_lag_us = 0U;
static uint8_t display_transport_last_rendered_beat_to_ui_lag_valid = 0U;
static uint8_t display_transport_last_rendered_bar = 0U;
static uint8_t display_transport_last_rendered_beat = 0U;
static uint8_t display_transport_last_rendered_barbeat_valid = 0U;
static uint8_t display_transport_start_grace_active = 0U;

static void Display_DrawTransportBarBeat(const char *text);
static void Display_DrawTransportAlert(DisplayTransportStatusMode_t mode);
static uint16_t Display_GetBpmDeltaX10(uint16_t lhs, uint16_t rhs);
static uint16_t Display_GetExternalBpmHysteresisX10(uint16_t bpm_x10);
static uint8_t Display_ShouldShowSyncingHeader(void);
static void Display_FormatRightAlignedStatusText(char *buffer,
                                                 size_t buffer_size,
                                                 const char *text,
                                                 uint8_t padded_chars);
static uint32_t Display_TimerDiffUs(uint32_t end_us, uint32_t start_us);
static void Display_FormatBarBeatText(char *buffer, uint8_t bar, uint8_t beat);

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

static uint8_t Display_ShouldShowSyncingHeader(void)
{
    MidiSyncState_t sync_state;

    if (ClockEngine_IsSyncLost())
        return 0U;

    if (!ClockEngine_IsRunning() && !MidiTransportStopLatched())
        return 0U;

    sync_state = ClockEngine_GetSyncState();
    /**
     * Keep SYNC text only for active acquisition phases.
     * RELOCK/REARM can still provide a usable external tempo source and
     * showing SYNC there can appear stuck after stop/play cycles.
     */
    return (uint8_t)(sync_state == MIDI_SYNC_STATE_ACQUIRE
                  || sync_state == MIDI_SYNC_STATE_TRACKING);
}

static void Display_FormatRightAlignedStatusText(char *buffer,
                                                 size_t buffer_size,
                                                 const char *text,
                                                 uint8_t padded_chars)
{
    snprintf(buffer, buffer_size, "%*s", (int)padded_chars, text);
}

static uint32_t Display_TimerDiffUs(uint32_t end_us, uint32_t start_us)
{
    return (end_us >= start_us)
        ? (end_us - start_us)
        : (UINT32_MAX - start_us + end_us + 1U);
}

static void Display_FormatBarBeatText(char *buffer, uint8_t bar, uint8_t beat)
{
    if (!buffer)
        return;

    if (bar > RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MAX)
        bar = RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MAX;
    if (beat > 9U)
        beat = 9U;

    if (bar >= 10U)
    {
        buffer[0] = (char)('0' + (bar / 10U));
        buffer[1] = (char)('0' + (bar % 10U));
        buffer[2] = '.';
        buffer[3] = (char)('0' + beat);
        buffer[4] = '\0';
    }
    else
    {
        buffer[0] = (char)('0' + bar);
        buffer[1] = '.';
        buffer[2] = (char)('0' + beat);
        buffer[3] = '\0';
    }
}

static void Display_UpdateTransportBarBeat(void)
{
    uint8_t bar;
    uint8_t beat;
    uint8_t have_barbeat;
    uint8_t have_phase_locked_barbeat_precision;
    uint8_t transport_running;
    uint8_t external_signal_present;
    uint8_t sync_lost;
    uint8_t stop_latched;
    ClockState_t clock_state;
    DisplayTransportStatusMode_t mode = DISPLAY_TRANSPORT_STATUS_NONE;
    char next_text[5];
    uint32_t transport_quarter_count = 0U;
    uint8_t have_transport_quarter_count = 0U;
    uint32_t transport_quarter_event_count = 0U;
    uint32_t stamped_quarter_count = 0U;
    uint32_t stamped_quarter_anchor_us = 0U;
    uint8_t have_stamped_quarter = 0U;
    uint8_t force_refresh_from_stamp = 0U;

    sync_lost = ClockEngine_IsSyncLost();
    transport_running = ClockEngine_IsRunning();
    external_signal_present = ClockEngine_IsExternalSignalPresent();
    clock_state = ClockEngine_GetState();
    have_phase_locked_barbeat_precision = (clock_state == CLOCK_STATE_LOCKED && transport_running) ? 1U : 0U;
    stop_latched = MidiTransportStopLatched();
    have_barbeat = MidiClockGetBarBeat(&bar, &beat);
    have_transport_quarter_count = MidiClockGetQuarterNoteCount(&transport_quarter_count);
    have_stamped_quarter = MidiClockGetQuarterNoteRenderStampWithAnchor(&transport_quarter_event_count,
                                                                         &stamped_quarter_count,
                                                                         &stamped_quarter_anchor_us);
    if (have_stamped_quarter
     && transport_quarter_event_count != display_transport_last_rendered_event)
    {
        force_refresh_from_stamp = 1U;
    }

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
        /* When transport is not truly phase-locked, bar precision is unknown.
         * Show a placeholder instead of reusing stale bar.beat values. */
        if (!have_phase_locked_barbeat_precision)
        {
            if (!transport_running || stop_latched)
            {
                display_transport_start_grace_active = 0U;
                strcpy(next_text, "-.-");
            }
            else if (external_signal_present && bar == 1U && beat == 1U)
            {
                /* START/CONTINUE anchors at 1.1. Hold non-locked bar display
                 * through the first natural beat advance to avoid a brief -.-
                 * flash between 1.1 and 1.2. */
                display_transport_start_grace_active = 1U;
                Display_FormatBarBeatText(next_text, bar, beat);
            }
            else if (display_transport_start_grace_active && external_signal_present)
            {
                Display_FormatBarBeatText(next_text, bar, beat);
                if (!(bar == 1U && beat == 1U))
                    display_transport_start_grace_active = 0U;
            }
            else
            {
                display_transport_start_grace_active = 0U;
                strcpy(next_text, "-.-");
            }
        }
        else
        {
            display_transport_start_grace_active = 0U;
            Display_FormatBarBeatText(next_text, bar, beat);
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
         && !force_refresh_from_stamp
         && strcmp(next_text, display_state.transport_barbeat_text) == 0)
        {
            return;
        }

        Display_DrawTransportBarBeat(next_text);
        strcpy(display_state.transport_barbeat_text, next_text);
        if (have_stamped_quarter)
        {
            uint32_t now_us = TIM2->CNT;
            uint32_t lag_us;

            display_transport_last_rendered_quarter = stamped_quarter_count;
            display_transport_last_rendered_event = transport_quarter_event_count;
            if (stamped_quarter_anchor_us == 0U)
            {
                display_transport_last_rendered_beat_to_ui_lag_us = 0U;
                display_transport_last_rendered_beat_to_ui_lag_valid = 0U;
            }
            else
            {
                if (now_us >= stamped_quarter_anchor_us)
                    lag_us = now_us - stamped_quarter_anchor_us;
                else
                    lag_us = UINT32_MAX - stamped_quarter_anchor_us + now_us + 1U;

                if (lag_us > 2000000U)
                {
                    display_transport_last_rendered_beat_to_ui_lag_us = 0U;
                    display_transport_last_rendered_beat_to_ui_lag_valid = 0U;
                }
                else
                {
                    display_transport_last_rendered_beat_to_ui_lag_us = lag_us;
                    display_transport_last_rendered_beat_to_ui_lag_valid = 1U;
                }
            }
        }
        else if (have_transport_quarter_count)
            display_transport_last_rendered_quarter = transport_quarter_count;
        display_transport_last_rendered_bar = bar;
        display_transport_last_rendered_beat = beat;
        display_transport_last_rendered_barbeat_valid = 1U;
        if (display_transport_render_counter < UINT32_MAX)
            display_transport_render_counter++;
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
        display_transport_last_rendered_barbeat_valid = 0U;
        display_transport_last_rendered_beat_to_ui_lag_us = 0U;
        display_transport_last_rendered_beat_to_ui_lag_valid = 0U;
    }

    display_state.transport_status_valid = 1U;
    display_state.transport_status_mode = (uint8_t)mode;
    display_state.transport_status_blink_visible = 1U;
}

void Display_UpdateTransportBarBeatFast(void)
{
    if (display_state.menu_mode_active && !display_state.menu_preview_active)
        return;

    Display_UpdateTransportBarBeat();
}

static void Display_DrawTransportBarBeat(const char *text)
{
    Display_ComposeLoadThemeBackgroundRegion(TRANSPORT_STATUS_AREA_W,
                                             TRANSPORT_STATUS_AREA_H,
                                             TRANSPORT_STATUS_AREA_X,
                                             TRANSPORT_STATUS_AREA_Y,
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

    Display_ComposeLoadThemeBackgroundRegion(TRANSPORT_STATUS_AREA_W,
                                             TRANSPORT_STATUS_AREA_H,
                                             TRANSPORT_STATUS_AREA_X,
                                             TRANSPORT_STATUS_AREA_Y,
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
    Display_ComposeLoadThemeBackgroundRegion(BPM_DISPLAY_AREA_W,
                                             BPM_FONT.height,
                                             BPM_DISPLAY_AREA_X,
                                             BPM_TEXT_Y,
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
    sync_lost = ClockEngine_IsSyncLost();
    show_syncing = Display_ShouldShowSyncingHeader();
    if (!show_syncing
     && !sync_lost
     && (MidiClockGetMeasuredExternalBpmX10(&measured_bpm_x10)
      || MidiClockGetExternalBpmX10(&measured_bpm_x10)))
    {
        display_bpm_x10 = measured_bpm_x10;
        use_external = 1U;
    }
    now_ms = HAL_GetTick();

    Display_UpdateTransportBarBeatFast();

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
        delta_x10 = Display_GetBpmDeltaX10(display_state.bpm_display_value_x10, display_bpm_x10);
        hysteresis_x10 = Display_GetExternalBpmHysteresisX10(display_state.bpm_display_value_x10);

        if (delta_x10 <= hysteresis_x10)
        {
            display_state.bpm_display_sync_lost = sync_lost;
            return;
        }

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
    uint8_t display_syncing = display_state.bpm_display_syncing;
    uint8_t transport_running = ClockEngine_IsRunning();
    uint8_t stop_latched = ClockEngine_IsStopLatched();
    uint8_t header_should_sync = 0U;
    uint8_t sync_lost = ClockEngine_IsSyncLost();
    uint8_t hold = 0U;
    uint8_t slew_pending = 0U;
    uint32_t transport_quarter_count = 0U;
    uint8_t transport_quarter_valid;
    uint32_t ui_render_counter_snapshot;
    uint32_t ui_last_rendered_quarter_snapshot;
    uint32_t ui_transport_quarter_gap = 0U;
    uint32_t beat_led_quarter_snapshot = 0U;
    uint32_t bar_counter_quarter_snapshot = 0U;
    uint32_t beat_led_to_bar_lag_quarters = 0U;
    uint32_t beat_led_to_bar_lag_ms = 0U;
    uint32_t lag_reference_bpm_x10 = 0U;
    uint32_t beat_led_to_bar_lag_us_snapshot = 0U;
    uint32_t beat_led_to_bar_lag_ms_true = 0U;
    uint32_t beat_led_to_bar_lag_effective_us = 0U;
    uint32_t beat_led_to_bar_lag_effective_ms = 0U;
    uint32_t quarter_period_us = 0U;
    uint32_t ui_rendered_event_snapshot = 0U;
    uint32_t latest_stamp_event_count = 0U;
    uint32_t latest_stamp_quarter_count = 0U;
    uint32_t latest_stamp_anchor_us = 0U;
    uint8_t latest_stamp_valid = 0U;
    uint32_t pending_stamp_events = 0U;
    uint8_t transport_bar = 0U;
    uint8_t transport_beat = 0U;
    uint8_t transport_barbeat_valid;
    uint8_t ui_rendered_bar_snapshot;
    uint8_t ui_rendered_beat_snapshot;
    uint8_t ui_rendered_barbeat_valid_snapshot;
    uint8_t compact_locked_report = 0U;
    int16_t bar_delta = 0;
    int16_t beat_delta = 0;

    if ((now_ms - last_report_tick) < BPM_DISPLAY_DIAGNOSTIC_REPORT_MS)
        return;

    last_report_tick = now_ms;
    raw_valid = MidiClockGetRawExternalBpmX10(&raw_bpm_x10);
    source_valid = MidiClockGetMeasuredExternalBpmX10(&source_bpm_x10);
    transport_quarter_valid = MidiClockGetQuarterNoteCount(&transport_quarter_count);
    transport_barbeat_valid = MidiClockGetBarBeat(&transport_bar, &transport_beat);
    ui_render_counter_snapshot = display_transport_render_counter;
    ui_last_rendered_quarter_snapshot = display_transport_last_rendered_quarter;
    ui_rendered_event_snapshot = display_transport_last_rendered_event;
    ui_rendered_bar_snapshot = display_transport_last_rendered_bar;
    ui_rendered_beat_snapshot = display_transport_last_rendered_beat;
    ui_rendered_barbeat_valid_snapshot = display_transport_last_rendered_barbeat_valid;
    beat_led_to_bar_lag_us_snapshot = (display_transport_last_rendered_beat_to_ui_lag_valid != 0U)
        ? display_transport_last_rendered_beat_to_ui_lag_us
        : 0U;
    if (!ui_rendered_barbeat_valid_snapshot)
        beat_led_to_bar_lag_us_snapshot = 0U;

    latest_stamp_valid = MidiClockGetQuarterNoteRenderStampWithAnchor(&latest_stamp_event_count,
                                                                       &latest_stamp_quarter_count,
                                                                       &latest_stamp_anchor_us);
    if (latest_stamp_valid && latest_stamp_event_count >= ui_rendered_event_snapshot)
        pending_stamp_events = latest_stamp_event_count - ui_rendered_event_snapshot;

    if (pending_stamp_events != 0U)
    {
        uint32_t now_us = TIM2->CNT;

        beat_led_to_bar_lag_us_snapshot = (latest_stamp_anchor_us != 0U)
            ? Display_TimerDiffUs(now_us, latest_stamp_anchor_us)
            : 0U;
        beat_led_to_bar_lag_quarters = pending_stamp_events;
        bar_counter_quarter_snapshot = latest_stamp_quarter_count - pending_stamp_events;
        beat_led_quarter_snapshot = latest_stamp_quarter_count;
    }

    if (beat_led_to_bar_lag_us_snapshot > 2000000U)
        beat_led_to_bar_lag_us_snapshot = 0U;
    if (transport_quarter_valid && transport_quarter_count >= ui_last_rendered_quarter_snapshot)
        ui_transport_quarter_gap = transport_quarter_count - ui_last_rendered_quarter_snapshot;
    beat_led_quarter_snapshot = transport_quarter_count;
    bar_counter_quarter_snapshot = ui_last_rendered_quarter_snapshot;
    beat_led_to_bar_lag_quarters = ui_transport_quarter_gap;
    if (source_valid)
        lag_reference_bpm_x10 = source_bpm_x10;
    else if (raw_valid)
        lag_reference_bpm_x10 = raw_bpm_x10;
    else if (displayed_bpm_x10 > 0U)
        lag_reference_bpm_x10 = displayed_bpm_x10;

    if (beat_led_to_bar_lag_quarters != 0U && lag_reference_bpm_x10 != 0U)
    {
        /* Convert quarter-note lag to milliseconds using the best available
         * tempo snapshot so serial captures show a direct time-domain lag. */
        beat_led_to_bar_lag_ms = (uint32_t)((600000UL * beat_led_to_bar_lag_quarters)
                                         / lag_reference_bpm_x10);
    }
    beat_led_to_bar_lag_ms_true = beat_led_to_bar_lag_us_snapshot / 1000U;

    /* Effective lag to current beat includes both:
     * 1) full quarters the UI is behind, and
     * 2) elapsed time since the last rendered beat edge. */
    if (lag_reference_bpm_x10 != 0U)
    {
        quarter_period_us = (uint32_t)(600000000UL / lag_reference_bpm_x10);
        beat_led_to_bar_lag_effective_us = beat_led_to_bar_lag_us_snapshot
            + (beat_led_to_bar_lag_quarters * quarter_period_us);
    }
    else
    {
        beat_led_to_bar_lag_effective_us = beat_led_to_bar_lag_us_snapshot;
    }
    beat_led_to_bar_lag_effective_ms = beat_led_to_bar_lag_effective_us / 1000U;
    if (transport_barbeat_valid && ui_rendered_barbeat_valid_snapshot)
    {
        bar_delta = (int16_t)transport_bar - (int16_t)ui_rendered_bar_snapshot;
        beat_delta = (int16_t)transport_beat - (int16_t)ui_rendered_beat_snapshot;
    }
    estimator_window_pulses = ClockEngine_GetEstimatorWindowPulses();
    estimator_valid = ClockEngine_IsEstimatorValid();
    publication_ready = ClockEngine_IsPublicationReady();
    switch (ClockEngine_GetSyncState())
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
    switch (ClockEngine_GetTransportConfidence())
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
    switch (ClockEngine_GetLockQuality())
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

    header_should_sync = Display_ShouldShowSyncingHeader();

    if (!raw_valid
     && !source_valid
     && !transport_quarter_valid
     && !ui_rendered_barbeat_valid_snapshot
     && !(display_valid && display_external)
     && !sync_lost)
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
        }
    }

    if (!BPM_DISPLAY_DIAG_VERBOSE_LOCKED)
    {
        if (sync_state == 'L'
         && ui_transport_quarter_gap == 0U
         && bar_delta == 0
         && beat_delta == 0
         && pending_stamp_events == 0U
         && beat_led_to_bar_lag_effective_ms <= 50U)
        {
            compact_locked_report = 1U;
        }
    }

    if (compact_locked_report)
    {
        /**
         * Keep steady LOCKED diagnostics compact so serial logging does not
         * monopolize foreground time and delay bar/beat rendering.
         */
         printf("BPMDIAG_V2_BAR sync_state=%c live_lock=%c transport_run=%u stop_latched=%u sync_lost=%u header_should_sync=%u header_syncing=%u display_bpm=%u.%u display_valid=%u display_external=%u display_age_ms=%lu transport_qn=%lu ui_qn=%lu ui_qn_gap=%lu lag_ms=%lu lag_effective_ms=%lu transport_bar=%u transport_beat=%u ui_bar=%u ui_beat=%u ui_bar_delta=%d ui_beat_delta=%d\r\n",
               sync_state,
               live_lock,
             (unsigned)transport_running,
             (unsigned)stop_latched,
             (unsigned)sync_lost,
             (unsigned)header_should_sync,
             (unsigned)display_syncing,
             (unsigned)(displayed_bpm_x10 / 10U),
             (unsigned)(displayed_bpm_x10 % 10U),
             (unsigned)display_valid,
             (unsigned)display_external,
             (unsigned long)age_ms,
               (unsigned long)transport_quarter_count,
               (unsigned long)ui_last_rendered_quarter_snapshot,
               (unsigned long)ui_transport_quarter_gap,
               (unsigned long)beat_led_to_bar_lag_ms_true,
               (unsigned long)beat_led_to_bar_lag_effective_ms,
               (unsigned)transport_bar,
               (unsigned)transport_beat,
               (unsigned)ui_rendered_bar_snapshot,
               (unsigned)ui_rendered_beat_snapshot,
               (int)bar_delta,
               (int)beat_delta);
        return;
    }

          printf("BPMDIAG_V2 raw_bpm=%u.%u raw_valid=%u source_bpm=%u.%u source_valid=%u transport_qn_valid=%u transport_qn=%lu ui_qn_rendered=%lu ui_qn_gap=%lu beat_led_qn=%lu bar_counter_qn=%lu beat_led_to_bar_lag_qn=%lu beat_led_to_bar_lag_ms=%lu beat_led_to_bar_lag_us=%lu beat_led_to_bar_lag_ms_true=%lu beat_led_to_bar_lag_effective_us=%lu beat_led_to_bar_lag_effective_ms=%lu ui_render_count=%lu transport_bar_valid=%u transport_bar=%u transport_beat=%u ui_bar_valid=%u ui_bar=%u ui_beat=%u ui_bar_delta=%d ui_beat_delta=%d est_window=%u history_confidence=%c est_valid=%u publication_ready=%u sync_state=%c live_lock=%c transport_run=%u stop_latched=%u header_should_sync=%u header_syncing=%u display_bpm=%u.%u display_external=%u sync_lost=%u display_age_ms=%lu delta=%u.%u hysteresis=%u.%u hold=%u slew_pending=%u\r\n",
           (unsigned)(raw_bpm_x10 / 10U),
           (unsigned)(raw_bpm_x10 % 10U),
           (unsigned)raw_valid,
           (unsigned)(source_bpm_x10 / 10U),
           (unsigned)(source_bpm_x10 % 10U),
           (unsigned)source_valid,
              (unsigned)transport_quarter_valid,
              (unsigned long)transport_quarter_count,
              (unsigned long)ui_last_rendered_quarter_snapshot,
              (unsigned long)ui_transport_quarter_gap,
              (unsigned long)beat_led_quarter_snapshot,
              (unsigned long)bar_counter_quarter_snapshot,
              (unsigned long)beat_led_to_bar_lag_quarters,
              (unsigned long)beat_led_to_bar_lag_ms,
              (unsigned long)beat_led_to_bar_lag_us_snapshot,
              (unsigned long)beat_led_to_bar_lag_ms_true,
              (unsigned long)beat_led_to_bar_lag_effective_us,
              (unsigned long)beat_led_to_bar_lag_effective_ms,
              (unsigned long)ui_render_counter_snapshot,
              (unsigned)transport_barbeat_valid,
              (unsigned)transport_bar,
              (unsigned)transport_beat,
              (unsigned)ui_rendered_barbeat_valid_snapshot,
              (unsigned)ui_rendered_bar_snapshot,
              (unsigned)ui_rendered_beat_snapshot,
              (int)bar_delta,
              (int)beat_delta,
              (unsigned)estimator_window_pulses,
              transport_confidence,
              (unsigned)estimator_valid,
              (unsigned)publication_ready,
              sync_state,
              live_lock,
              (unsigned)transport_running,
              (unsigned)stop_latched,
              (unsigned)header_should_sync,
              (unsigned)display_syncing,
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

uint8_t Display_IsBpmHeaderSyncing(void)
{
    return display_state.bpm_display_syncing;
}
