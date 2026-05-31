#include "display/display_palette_registry.h"

#include <stddef.h>
#include <string.h>

#include "st7796_rgb565_colors.h"

typedef struct
{
    const char *name;
    uint16_t value;
} DisplayPaletteEntry_t;

static const DisplayPaletteEntry_t display_palette_entries[] = {
#include "display/display_palette_registry.inc"
};

enum
{
    DISPLAY_PALETTE_RAW_COUNT = sizeof(display_palette_entries) / sizeof(display_palette_entries[0]),
    DISPLAY_PALETTE_FAMILY_COUNT = 10U,
    DISPLAY_PALETTE_FAMILY_MARKER_COUNT = DISPLAY_PALETTE_FAMILY_COUNT - 1U,
};

static const char * const display_palette_family_markers[DISPLAY_PALETTE_FAMILY_MARKER_COUNT] = {
    "OXBLOOD",
    "MANGO_TANGO",
    "GOLD_FUSION",
    "MINDARO",
    "TEA_GREEN",
    "ZOMP",
    "PALE_CERULEAN",
    "ULTRAMARINE",
    "DARK_PURPLE",
};

static uint16_t display_palette_browse_indices[DISPLAY_PALETTE_RAW_COUNT];
static uint16_t display_palette_family_browse_starts[DISPLAY_PALETTE_FAMILY_COUNT + 1U];
static uint16_t display_palette_browse_count = 0U;
static uint8_t display_palette_browse_initialized = 0U;

static uint32_t DisplayPalette_GetBrightness(uint16_t value)
{
    uint32_t red = (uint32_t)((value >> 11) & 0x1FU) * 255U / 31U;
    uint32_t green = (uint32_t)((value >> 5) & 0x3FU) * 255U / 63U;
    uint32_t blue = (uint32_t)(value & 0x1FU) * 255U / 31U;

    return (red * 299U) + (green * 587U) + (blue * 114U);
}

static uint16_t DisplayPalette_FindRawIndexByName(const char *name, uint16_t search_start)
{
    for (uint16_t index = search_start; index < DISPLAY_PALETTE_RAW_COUNT; ++index)
    {
        if (strcmp(display_palette_entries[index].name, name) == 0)
            return index;
    }

    return DISPLAY_PALETTE_RAW_COUNT;
}

static uint8_t DisplayPalette_ValueAlreadyAdded(uint16_t value)
{
    for (uint16_t index = 0U; index < display_palette_browse_count; ++index)
    {
        if (display_palette_entries[display_palette_browse_indices[index]].value == value)
            return 1U;
    }

    return 0U;
}

static uint8_t DisplayPalette_BrowseEntryComesFirst(uint16_t left_raw_index, uint16_t right_raw_index)
{
    uint32_t left_brightness = DisplayPalette_GetBrightness(display_palette_entries[left_raw_index].value);
    uint32_t right_brightness = DisplayPalette_GetBrightness(display_palette_entries[right_raw_index].value);

    if (left_brightness != right_brightness)
        return (left_brightness < right_brightness) ? 1U : 0U;

    return (left_raw_index < right_raw_index) ? 1U : 0U;
}

static void DisplayPalette_SortBrowseSegment(uint16_t start_index, uint16_t end_index)
{
    for (uint16_t sort_index = (uint16_t)(start_index + 1U); sort_index < end_index; ++sort_index)
    {
        uint16_t candidate_raw_index = display_palette_browse_indices[sort_index];
        uint16_t insert_index = sort_index;

        while (insert_index > start_index
            && DisplayPalette_BrowseEntryComesFirst(candidate_raw_index,
                                                    display_palette_browse_indices[insert_index - 1U]))
        {
            display_palette_browse_indices[insert_index] = display_palette_browse_indices[insert_index - 1U];
            --insert_index;
        }

        display_palette_browse_indices[insert_index] = candidate_raw_index;
    }
}

static void DisplayPalette_EnsureBrowseOrder(void)
{
    uint16_t family_start_indices[DISPLAY_PALETTE_FAMILY_COUNT + 1U];
    uint16_t search_start = 1U;

    if (display_palette_browse_initialized)
        return;

    family_start_indices[0] = 0U;
    for (uint16_t marker_index = 0U; marker_index < DISPLAY_PALETTE_FAMILY_MARKER_COUNT; ++marker_index)
    {
        uint16_t family_start = DisplayPalette_FindRawIndexByName(display_palette_family_markers[marker_index],
                                                                  search_start);

        if (family_start == DISPLAY_PALETTE_RAW_COUNT)
            family_start = DISPLAY_PALETTE_RAW_COUNT;

        family_start_indices[marker_index + 1U] = family_start;
        search_start = (family_start < DISPLAY_PALETTE_RAW_COUNT) ? (uint16_t)(family_start + 1U) : DISPLAY_PALETTE_RAW_COUNT;
    }
    family_start_indices[DISPLAY_PALETTE_FAMILY_COUNT] = DISPLAY_PALETTE_RAW_COUNT;

    display_palette_browse_count = 0U;
    for (uint16_t family_index = 0U; family_index < DISPLAY_PALETTE_FAMILY_COUNT; ++family_index)
    {
        uint16_t family_browse_start = display_palette_browse_count;
        uint16_t family_raw_start = family_start_indices[family_index];
        uint16_t family_raw_end = family_start_indices[family_index + 1U];

        display_palette_family_browse_starts[family_index] = family_browse_start;

        for (uint16_t raw_index = family_raw_start; raw_index < family_raw_end; ++raw_index)
        {
            uint16_t value = display_palette_entries[raw_index].value;

            if (DisplayPalette_ValueAlreadyAdded(value))
                continue;

            display_palette_browse_indices[display_palette_browse_count] = raw_index;
            ++display_palette_browse_count;
        }

        DisplayPalette_SortBrowseSegment(family_browse_start, display_palette_browse_count);
    }

    display_palette_family_browse_starts[DISPLAY_PALETTE_FAMILY_COUNT] = display_palette_browse_count;

    display_palette_browse_initialized = 1U;
}

static uint16_t DisplayPalette_GetFamilyIndexForBrowseIndex(uint16_t browse_index)
{
    DisplayPalette_EnsureBrowseOrder();

    for (uint16_t family_index = 0U; family_index < DISPLAY_PALETTE_FAMILY_COUNT; ++family_index)
    {
        if (browse_index < display_palette_family_browse_starts[family_index + 1U])
            return family_index;
    }

    return (DISPLAY_PALETTE_FAMILY_COUNT > 0U) ? (DISPLAY_PALETTE_FAMILY_COUNT - 1U) : 0U;
}

uint16_t DisplayPalette_GetCount(void)
{
    DisplayPalette_EnsureBrowseOrder();

    return display_palette_browse_count;
}

const char *DisplayPalette_GetName(uint16_t index)
{
    DisplayPalette_EnsureBrowseOrder();

    if (index >= DisplayPalette_GetCount())
        return "";

    return display_palette_entries[display_palette_browse_indices[index]].name;
}

uint16_t DisplayPalette_GetValue(uint16_t index)
{
    DisplayPalette_EnsureBrowseOrder();

    if (index >= DisplayPalette_GetCount())
        return 0U;

    return display_palette_entries[display_palette_browse_indices[index]].value;
}

uint16_t DisplayPalette_FindIndexByValue(uint16_t value)
{
    DisplayPalette_EnsureBrowseOrder();

    for (uint16_t index = 0U; index < DisplayPalette_GetCount(); ++index)
    {
        if (display_palette_entries[display_palette_browse_indices[index]].value == value)
            return index;
    }

    return 0U;
}

uint16_t DisplayPalette_StepIndex(uint16_t current_index, int8_t delta)
{
    uint16_t count = DisplayPalette_GetCount();
    int32_t next_index;

    if (count == 0U)
        return 0U;

    if (current_index >= count)
        current_index = 0U;

    if (delta == 0)
        return current_index;

    next_index = (int32_t)current_index + (int32_t)delta;
    while (next_index < 0)
        next_index += (int32_t)count;
    while (next_index >= (int32_t)count)
        next_index -= (int32_t)count;

    return (uint16_t)next_index;
}

uint16_t DisplayPalette_StepHueIndex(uint16_t current_index, int8_t delta)
{
    uint16_t count = DisplayPalette_GetCount();
    uint16_t family_index;
    uint16_t family_start;
    uint16_t family_length;
    uint16_t family_offset;
    uint16_t steps_remaining;

    if (count == 0U)
        return 0U;

    if (current_index >= count)
        current_index = 0U;

    if (delta == 0)
        return current_index;

    family_index = DisplayPalette_GetFamilyIndexForBrowseIndex(current_index);
    family_start = display_palette_family_browse_starts[family_index];
    family_length = (uint16_t)(display_palette_family_browse_starts[family_index + 1U] - family_start);
    family_offset = (uint16_t)(current_index - family_start);
    steps_remaining = (uint16_t)((delta > 0) ? delta : -delta);

    while (steps_remaining > 0U)
    {
        uint16_t next_family_index = family_index;
        uint16_t next_family_start;
        uint16_t next_family_length;

        if (delta > 0)
            next_family_index = (uint16_t)((family_index + 1U) % DISPLAY_PALETTE_FAMILY_COUNT);
        else if (family_index == 0U)
            next_family_index = (uint16_t)(DISPLAY_PALETTE_FAMILY_COUNT - 1U);
        else
            next_family_index = (uint16_t)(family_index - 1U);

        next_family_start = display_palette_family_browse_starts[next_family_index];
        next_family_length = (uint16_t)(display_palette_family_browse_starts[next_family_index + 1U] - next_family_start);

        if (family_length <= 1U || next_family_length <= 1U)
            family_offset = 0U;
        else
            family_offset = (uint16_t)((((uint32_t)family_offset * (uint32_t)(next_family_length - 1U))
                                       + (uint32_t)((family_length - 1U) / 2U))
                                       / (uint32_t)(family_length - 1U));

        family_index = next_family_index;
        family_start = next_family_start;
        family_length = next_family_length;
        --steps_remaining;
    }

    return (uint16_t)(family_start + family_offset);
}

uint16_t DisplayPalette_StepBrightnessIndex(uint16_t current_index, int8_t delta)
{
    uint16_t count = DisplayPalette_GetCount();
    uint16_t family_index;
    uint16_t family_start;
    uint16_t family_end;
    int32_t next_index;

    if (count == 0U)
        return 0U;

    if (current_index >= count)
        current_index = 0U;

    if (delta == 0)
        return current_index;

    family_index = DisplayPalette_GetFamilyIndexForBrowseIndex(current_index);
    family_start = display_palette_family_browse_starts[family_index];
    family_end = display_palette_family_browse_starts[family_index + 1U];
    next_index = (int32_t)current_index + (int32_t)delta;

    if (next_index < (int32_t)family_start)
        next_index = (int32_t)family_start;
    else if (next_index >= (int32_t)family_end)
        next_index = (int32_t)(family_end - 1U);

    return (uint16_t)next_index;
}