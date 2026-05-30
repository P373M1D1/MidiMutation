#include "display/display_menu_page_midi_monitor.h"

#include <stdio.h>
#include <string.h>

#include "display_functions.h"
#include "display/display_internal.h"
#include "display/display_layout.h"
#include "midi/midi_monitor.h"
#include "midi_functions.h"
#include "st7796.h"

#define MIDI_MONITOR_FONT                  (*Display_GetThemeInfoFont())
#define MIDI_MONITOR_FONT_CHAR_WIDTH       15U
#define MIDI_MONITOR_FONT_LINE_HEIGHT      35U
#define MIDI_MONITOR_BG_COLOUR             BLACK
#define MIDI_MONITOR_TEXT_COLOUR           WHITE
#define MIDI_MONITOR_TEXT_X                8U
#define MIDI_MONITOR_LINE_NUMBER_CHARS     2U
#define MIDI_MONITOR_LINE_GUTTER_CHARS     (1U + MIDI_MONITOR_LINE_NUMBER_CHARS)
#define MIDI_MONITOR_DATA_COLUMN_CHARS     4U
#define MIDI_MONITOR_HEADER_DATA_PAD_CHARS MIDI_MONITOR_DATA_COLUMN_CHARS
#define MIDI_MONITOR_ROW_DATA_PAD_CHARS    (MIDI_MONITOR_DATA_COLUMN_CHARS - MIDI_MONITOR_LINE_GUTTER_CHARS)
#define MIDI_MONITOR_LINE_TEXT_CHARS       ((ST7796_WIDTH - MIDI_MONITOR_TEXT_X) / MIDI_MONITOR_FONT_CHAR_WIDTH)
#define MIDI_MONITOR_CLOCK_Y               (MAIN_PRESET_TEXT_Y - MIDI_MONITOR_FONT_LINE_HEIGHT)
#define MIDI_MONITOR_COLUMNS_Y             MAIN_PRESET_TEXT_Y
#define MIDI_MONITOR_MESSAGES_Y            (MIDI_MONITOR_COLUMNS_Y + MIDI_MONITOR_FONT_LINE_HEIGHT)
#define MIDI_MONITOR_VISIBLE_MESSAGE_COUNT ((MAIN_FOOTBAR_Y - MIDI_MONITOR_MESSAGES_Y) / MIDI_MONITOR_FONT_LINE_HEIGHT)
#define MIDI_MONITOR_REFRESH_MS            50U

static uint8_t midi_monitor_paused = 0U;
static uint8_t midi_monitor_scroll_offset = 0U;
static uint8_t midi_monitor_last_drawn_clock_present = 0U;
static uint32_t midi_monitor_last_refresh_tick = 0U;
static uint8_t midi_monitor_cache_valid = 0U;
static char midi_monitor_cached_clock_line[MIDI_MONITOR_LINE_TEXT_CHARS + 1U];
static char midi_monitor_cached_column_line[MIDI_MONITOR_LINE_TEXT_CHARS + 1U];
static char midi_monitor_cached_message_lines[MIDI_MONITOR_VISIBLE_MESSAGE_COUNT][MIDI_MONITOR_LINE_TEXT_CHARS + 1U];
static uint8_t midi_monitor_cached_bottom_indent[MIDI_MONITOR_VISIBLE_MESSAGE_COUNT];

static uint8_t Display_MenuMidiMonitorGetMaxScroll(uint8_t entry_count)
{
    return (entry_count > MIDI_MONITOR_VISIBLE_MESSAGE_COUNT)
        ? (uint8_t)(entry_count - MIDI_MONITOR_VISIBLE_MESSAGE_COUNT)
        : 0U;
}

static void Display_MenuMidiMonitorResetCache(void)
{
    midi_monitor_cache_valid = 0U;
    midi_monitor_cached_clock_line[0] = '\0';
    midi_monitor_cached_column_line[0] = '\0';

    for (uint8_t line_index = 0U; line_index < MIDI_MONITOR_VISIBLE_MESSAGE_COUNT; ++line_index)
    {
        midi_monitor_cached_message_lines[line_index][0] = '\0';
        midi_monitor_cached_bottom_indent[line_index] = 0U;
    }
}

static void Display_MenuMidiMonitorWriteLine(uint16_t y, const char *text)
{
    char padded[MIDI_MONITOR_LINE_TEXT_CHARS + 1U];
    size_t text_len = 0U;

    memset(padded, ' ', MIDI_MONITOR_LINE_TEXT_CHARS);
    if (text)
    {
        text_len = strnlen(text, MIDI_MONITOR_LINE_TEXT_CHARS);
        memcpy(padded, text, text_len);
    }

    padded[MIDI_MONITOR_LINE_TEXT_CHARS] = '\0';

    ST7796_WriteString32(MIDI_MONITOR_TEXT_X,
                         y,
                         padded,
                         MIDI_MONITOR_FONT,
                         MIDI_MONITOR_TEXT_COLOUR,
                         MIDI_MONITOR_BG_COLOUR);
}

static void Display_MenuMidiMonitorDrawBottomScrollIndent(uint16_t row_y)
{
    uint16_t left_x = (uint16_t)(MIDI_MONITOR_TEXT_X + 2U);
    uint16_t right_x = (uint16_t)(MIDI_MONITOR_TEXT_X + MIDI_MONITOR_FONT_CHAR_WIDTH - 3U);
    uint16_t center_x = (uint16_t)((left_x + right_x) / 2U);
    uint16_t top_y = (uint16_t)(row_y + 9U);
    uint16_t apex_y = (uint16_t)(top_y + 10U);

    ST7796_DrawLine(left_x, top_y, center_x, apex_y, MIDI_MONITOR_TEXT_COLOUR);
    ST7796_DrawLine(center_x, apex_y, right_x, top_y, MIDI_MONITOR_TEXT_COLOUR);
    ST7796_DrawLine(left_x, (uint16_t)(top_y + 1U), center_x, (uint16_t)(apex_y + 1U), MIDI_MONITOR_TEXT_COLOUR);
    ST7796_DrawLine(center_x, (uint16_t)(apex_y + 1U), right_x, (uint16_t)(top_y + 1U), MIDI_MONITOR_TEXT_COLOUR);
}

static void Display_MenuMidiMonitorDrawLineIfChanged(uint16_t y,
                                                     const char *text,
                                                     char *cached_text,
                                                     uint8_t force_redraw)
{
    if (!cached_text)
        return;

    if (!force_redraw && text && strcmp(cached_text, text) == 0)
        return;

    if (!force_redraw && !text && cached_text[0] == '\0')
        return;

    Display_MenuMidiMonitorWriteLine(y, text ? text : "");

    if (text)
    {
        strncpy(cached_text, text, MIDI_MONITOR_LINE_TEXT_CHARS);
        cached_text[MIDI_MONITOR_LINE_TEXT_CHARS] = '\0';
    }
    else
    {
        cached_text[0] = '\0';
    }
}

static void Display_FormatMidiMonitorEntry(const MidiMonitorEntry_t *entry,
                                           char *buffer,
                                           size_t buffer_size)
{
    char channel_text[4] = "--";
    char value1_text[8] = "---";
    char value2_text[8] = "---";
    const char *type_text = "--";

    if (!entry || !buffer || buffer_size == 0U)
        return;

    switch ((MidiMonitorMessageType_t)entry->type)
    {
    case MIDI_MONITOR_MESSAGE_PROGRAM_CHANGE:
        type_text = "Program";
        (void)snprintf(channel_text, sizeof(channel_text), "%02u", entry->channel);
        /* MIDI Program Change is encoded on the wire as 0..127, but most DAWs
         * and patch lists present it as 1..128. Show that user-facing number
         * here so monitor output matches what Ableton labels in its UI. */
        (void)snprintf(value1_text, sizeof(value1_text), "%3u", (unsigned)(entry->value1 + 1U));
        break;

    case MIDI_MONITOR_MESSAGE_CONTROL_CHANGE:
        type_text = "Control";
        (void)snprintf(channel_text, sizeof(channel_text), "%02u", entry->channel);
        (void)snprintf(value1_text, sizeof(value1_text), "%3u", entry->value1);
        (void)snprintf(value2_text, sizeof(value2_text), "%3u", entry->value2);
        break;

    case MIDI_MONITOR_MESSAGE_START:
        type_text = "Start";
        break;

    case MIDI_MONITOR_MESSAGE_CONTINUE:
        type_text = "Continue";
        break;

    case MIDI_MONITOR_MESSAGE_STOP:
        type_text = "Stop";
        break;

    default:
        type_text = "--";
        break;
    }

    (void)snprintf(buffer,
                   buffer_size,
                   "%u: %s %-8s %3s %3s",
                   (unsigned)entry->source_uart,
                   channel_text,
                   type_text,
                   value1_text,
                   value2_text);
}

static void Display_FormatMidiMonitorMessageLine(const MidiMonitorEntry_t *entry,
                                                 uint8_t line_number,
                                                 char scroll_marker,
                                                 char *buffer,
                                                 size_t buffer_size)
{
    char data_text[40];

    if (!buffer || buffer_size == 0U)
        return;

    if (!entry)
    {
        buffer[0] = '\0';
        return;
    }

    Display_FormatMidiMonitorEntry(entry, data_text, sizeof(data_text));
    (void)snprintf(buffer,
                   buffer_size,
                   "%c%0*u%*s%s",
                   scroll_marker,
                   MIDI_MONITOR_LINE_NUMBER_CHARS,
                   (unsigned)line_number,
                   (int)MIDI_MONITOR_ROW_DATA_PAD_CHARS,
                   "",
                   data_text);
}

uint8_t Display_MenuMidiMonitorIsActive(void)
{
    return (display_state.menu_mode_active
         && (DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_MIDI_MONITOR) ? 1U : 0U;
}

uint8_t Display_MenuMidiMonitorIsPaused(void)
{
    return midi_monitor_paused;
}

void Display_MenuMidiMonitorEnter(void)
{
    midi_monitor_paused = 0U;
    midi_monitor_scroll_offset = 0U;
    midi_monitor_last_drawn_clock_present = 0U;
    midi_monitor_last_refresh_tick = 0U;
    Display_MenuMidiMonitorResetCache();
}

void Display_MenuMidiMonitorTogglePause(void)
{
    midi_monitor_paused = (uint8_t)!midi_monitor_paused;
    if (!midi_monitor_paused)
        midi_monitor_scroll_offset = 0U;

    if (Display_MenuMidiMonitorIsActive())
    {
        Display_DrawFootbar();
        Display_MenuRefreshBodyOnly();
    }
}

void Display_MenuMidiMonitorClear(void)
{
    MidiMonitor_Clear();
    midi_monitor_scroll_offset = 0U;

    if (Display_MenuMidiMonitorIsActive())
    {
        Display_MenuMidiMonitorResetCache();
        Display_MenuRefreshBodyOnly();
    }
}

void Display_MenuMidiMonitorScroll(int8_t delta)
{
    MidiMonitorEntry_t entries[MIDI_MONITOR_ENTRY_CAPACITY];
    uint8_t entry_count;
    uint8_t max_scroll;
    int16_t next_scroll;

    if (delta == 0 || !Display_MenuMidiMonitorIsActive())
        return;

    entry_count = MidiMonitor_CopyEntries(entries, MIDI_MONITOR_ENTRY_CAPACITY);
    max_scroll = Display_MenuMidiMonitorGetMaxScroll(entry_count);

    if (!midi_monitor_paused)
    {
        midi_monitor_paused = 1U;
        Display_DrawFootbar();
    }

    /* MIDI monitor browsing is intentionally inverted relative to the normal
     * menu list so CW walks down toward larger line numbers and CCW returns
     * toward older/top entries. */
    next_scroll = (int16_t)midi_monitor_scroll_offset - (int16_t)delta;
    if (next_scroll < 0)
        next_scroll = 0;
    if (next_scroll > (int16_t)max_scroll)
        next_scroll = (int16_t)max_scroll;

    midi_monitor_scroll_offset = (uint8_t)next_scroll;
    Display_MenuRefreshBodyOnly();
}

void Display_MenuMidiMonitorService(void)
{
    uint8_t clock_present;
    uint32_t now;

    if (!Display_MenuMidiMonitorIsActive())
        return;

    clock_present = MidiClockIsExternalSignalPresent();

    if (clock_present == midi_monitor_last_drawn_clock_present)
        return;

    now = HAL_GetTick();
    if ((now - midi_monitor_last_refresh_tick) < MIDI_MONITOR_REFRESH_MS)
        return;

    Display_MenuRefreshBodyOnly();
}

void Display_DrawMenuMidiMonitor(void)
{
    MidiMonitorEntry_t entries[MIDI_MONITOR_ENTRY_CAPACITY];
    uint8_t entry_count = MidiMonitor_CopyEntries(entries, MIDI_MONITOR_ENTRY_CAPACITY);
    uint8_t clock_present = MidiClockIsExternalSignalPresent();
    uint8_t max_scroll = Display_MenuMidiMonitorGetMaxScroll(entry_count);
    uint8_t first_visible_index = 0U;
    uint8_t newest_visible_index = 0U;
    char line[MIDI_MONITOR_LINE_TEXT_CHARS + 1U];
    uint8_t force_redraw = midi_monitor_cache_valid ? 0U : 1U;

    if (midi_monitor_scroll_offset > max_scroll)
        midi_monitor_scroll_offset = max_scroll;

    if (entry_count > MIDI_MONITOR_VISIBLE_MESSAGE_COUNT)
        first_visible_index = (uint8_t)(entry_count - MIDI_MONITOR_VISIBLE_MESSAGE_COUNT - midi_monitor_scroll_offset);

    newest_visible_index = (entry_count == 0U)
        ? 0U
        : (uint8_t)(first_visible_index + ((entry_count - first_visible_index > MIDI_MONITOR_VISIBLE_MESSAGE_COUNT)
            ? (MIDI_MONITOR_VISIBLE_MESSAGE_COUNT - 1U)
            : (entry_count - first_visible_index - 1U)));

    if (force_redraw)
    {
        ST7796_DrawFilledRectangle(0U,
                                   MAIN_PRESET_TEXT_Y,
                                   ST7796_WIDTH,
                                   (uint16_t)(MAIN_FOOTBAR_Y - MAIN_PRESET_TEXT_Y),
                                   MIDI_MONITOR_BG_COLOUR);
    }

    (void)snprintf(line,
                   sizeof(line),
                   "CLK SIGNAL IN: %s",
                   clock_present ? "YES" : "NO");
    Display_MenuMidiMonitorDrawLineIfChanged(MIDI_MONITOR_CLOCK_Y,
                                             line,
                                             midi_monitor_cached_clock_line,
                                             force_redraw);

    (void)snprintf(line,
                   sizeof(line),
                   "%*sU: Ch Type     V1  V2",
                   (int)MIDI_MONITOR_HEADER_DATA_PAD_CHARS,
                   "");
    Display_MenuMidiMonitorDrawLineIfChanged(MIDI_MONITOR_COLUMNS_Y,
                                             line,
                                             midi_monitor_cached_column_line,
                                             force_redraw);

    for (uint8_t visible_index = 0U; visible_index < MIDI_MONITOR_VISIBLE_MESSAGE_COUNT; ++visible_index)
    {
        uint16_t row_y = (uint16_t)(MIDI_MONITOR_MESSAGES_Y + (visible_index * MIDI_MONITOR_FONT.height));
        uint8_t entry_index = (uint8_t)(first_visible_index + visible_index);
        char scroll_marker = ' ';
        uint8_t draw_bottom_indent = 0U;
        uint8_t line_force_redraw = force_redraw;

        if (entry_index >= entry_count)
        {
            Display_MenuMidiMonitorDrawLineIfChanged(row_y,
                                                     "",
                                                     midi_monitor_cached_message_lines[visible_index],
                                                     force_redraw);
            continue;
        }

        if (entry_index == first_visible_index && first_visible_index > 0U)
            scroll_marker = '^';
        else if (entry_index == newest_visible_index && newest_visible_index < (uint8_t)(entry_count - 1U))
            draw_bottom_indent = 1U;

        if (midi_monitor_cached_bottom_indent[visible_index] != draw_bottom_indent)
            line_force_redraw = 1U;

        Display_FormatMidiMonitorMessageLine(&entries[entry_index],
                                             (uint8_t)(entry_index + 1U),
                                             scroll_marker,
                                             line,
                                             sizeof(line));
        Display_MenuMidiMonitorDrawLineIfChanged(row_y,
                                                 line,
                                                 midi_monitor_cached_message_lines[visible_index],
                                                 line_force_redraw);

        if (draw_bottom_indent)
            Display_MenuMidiMonitorDrawBottomScrollIndent(row_y);

        midi_monitor_cached_bottom_indent[visible_index] = draw_bottom_indent;
    }

    midi_monitor_cache_valid = 1U;
    midi_monitor_last_drawn_clock_present = clock_present;
    midi_monitor_last_refresh_tick = HAL_GetTick();
}