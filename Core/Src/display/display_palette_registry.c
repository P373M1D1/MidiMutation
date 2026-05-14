#include "display/display_palette_registry.h"

#include <stddef.h>

#include "st7796_rgb565_colors.h"

typedef struct
{
    const char *name;
    uint16_t value;
} DisplayPaletteEntry_t;

static const DisplayPaletteEntry_t display_palette_entries[] = {
#include "display/display_palette_registry.inc"
};

uint16_t DisplayPalette_GetCount(void)
{
    return (uint16_t)(sizeof(display_palette_entries) / sizeof(display_palette_entries[0]));
}

const char *DisplayPalette_GetName(uint16_t index)
{
    if (index >= DisplayPalette_GetCount())
        return "";

    return display_palette_entries[index].name;
}

uint16_t DisplayPalette_GetValue(uint16_t index)
{
    if (index >= DisplayPalette_GetCount())
        return 0U;

    return display_palette_entries[index].value;
}

uint16_t DisplayPalette_FindIndexByValue(uint16_t value)
{
    for (uint16_t index = 0U; index < DisplayPalette_GetCount(); ++index)
    {
        if (display_palette_entries[index].value == value)
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

    next_index = (int32_t)current_index + (int32_t)delta;
    while (next_index < 0)
        next_index += (int32_t)count;
    while (next_index >= (int32_t)count)
        next_index -= (int32_t)count;

    return (uint16_t)next_index;
}