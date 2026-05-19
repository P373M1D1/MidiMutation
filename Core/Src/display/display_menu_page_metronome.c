#include <stdio.h>

#include "display/display_internal.h"
#include "display/display_menu_page_metronome.h"
#include "display/display_menu_redraw_utils.h"
#include "display/display_menu_row_render.h"
#include "runtime_config.h"

static const char * const menu_metronome_labels[MENU_METRONOME_ITEM_COUNT] = {
    "Volume",
    "Pitch",
    "Beat",
    "Rhythm",
};

static const char *Display_GetMetronomePitchText(RuntimeConfigMetronomePitch_t pitch)
{
    switch (pitch)
    {
    case RUNTIME_CONFIG_METRONOME_PITCH_LOW:
        return "Low";
    case RUNTIME_CONFIG_METRONOME_PITCH_HIGH:
        return "High";
    case RUNTIME_CONFIG_METRONOME_PITCH_MID:
    default:
        return "Mid";
    }
}

static const char *Display_GetMetronomeRhythmText(RuntimeConfigMetronomeRhythm_t rhythm)
{
    switch (rhythm)
    {
    case RUNTIME_CONFIG_METRONOME_RHYTHM_FOUR_EIGHT:
        return "Eighths";
    case RUNTIME_CONFIG_METRONOME_RHYTHM_OFFBEAT:
        return "OffBeat";
    case RUNTIME_CONFIG_METRONOME_RHYTHM_TRIPLETS:
        return "Triplets";
    case RUNTIME_CONFIG_METRONOME_RHYTHM_SHUFFLE:
        return "Shuffle";
    case RUNTIME_CONFIG_METRONOME_RHYTHM_QUARTER_NOTES:
    default:
        return "Quarter Notes";
    }
}

void Display_FormatMetronomeMenuValue(uint8_t item_index, char *buffer, size_t buffer_size)
{
    const RuntimeConfigMetronome_t *metronome = RuntimeConfig_GetMetronome();

    if (!buffer || buffer_size == 0U || !metronome)
        return;

    switch (item_index)
    {
    case 0U:
        (void)snprintf(buffer, buffer_size, "%u", metronome->volume);
        break;
    case 1U:
        (void)snprintf(buffer,
                       buffer_size,
                       "%s",
                       Display_GetMetronomePitchText(metronome->pitch));
        break;
    case 2U:
        (void)snprintf(buffer, buffer_size, "%u/4", metronome->beats_per_bar);
        break;
    case 3U:
        (void)snprintf(buffer,
                       buffer_size,
                       "%s",
                       Display_GetMetronomeRhythmText(metronome->rhythm));
        break;
    default:
        buffer[0] = '\0';
        break;
    }
}

void Display_DrawMenuMetronome(void)
{
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MENU_METRONOME_ITEM_COUNT,
                                                                   display_state.menu_metronome_selection_index);

    for (uint8_t row_index = 0U; row_index < MENU_VISIBLE_ROW_COUNT; ++row_index)
    {
        uint8_t item_index = (uint8_t)(first_visible_index + row_index);

        if (item_index < MENU_METRONOME_ITEM_COUNT)
            Display_DrawMenuMetronomeItem(item_index);
        else
            Display_ClearStandardMenuRow(row_index);
    }
}

void Display_DrawMenuMetronomeItem(uint8_t item_index)
{
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MENU_METRONOME_ITEM_COUNT,
                                                                   display_state.menu_metronome_selection_index);
    uint8_t row_index;
    char value_text[20];

    if (item_index < first_visible_index || item_index >= (uint8_t)(first_visible_index + MENU_VISIBLE_ROW_COUNT))
        return;

    row_index = (uint8_t)(item_index - first_visible_index);
    Display_FormatMetronomeMenuValue(item_index, value_text, sizeof(value_text));
    Display_DrawMenuRowByIndex(row_index,
                               menu_metronome_labels[item_index],
                               value_text,
                               (item_index == display_state.menu_metronome_selection_index) ? 1U : 0U);
}