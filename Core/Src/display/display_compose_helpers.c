#include <stdio.h>
#include <string.h>

#include "display/display_compose_helpers.h"
#include "display/display_internal.h"
#include "display/display_layout.h"
#include "display/display_row_compose.h"
#include "display_compose.h"

/* Shared compose wrappers for extracted display modules.
 *
 * These helpers expose a narrow drawing surface so split-out display files can
 * clear, fill, render text, and blit row buffers without depending on static
 * functions that used to live in display_functions.c. Keep this layer focused
 * on reusable drawing primitives rather than page-specific policy. */

uint16_t Display_GetBackgroundColour(void)
{
    return DISPLAY_BG_COLOUR;
}

void Display_ComposeClear(uint16_t clip_width,
                          uint16_t clip_height,
                          uint16_t colour)
{
    DisplayCompose_Clear(clip_width,
                         clip_height,
                         colour);
}

void Display_ComposeFillRect(uint16_t clip_width,
                             uint16_t clip_height,
                             uint16_t x,
                             uint16_t y,
                             uint16_t w,
                             uint16_t h,
                             uint16_t colour)
{
    DisplayCompose_FillRect(clip_width,
                            clip_height,
                            x,
                            y,
                            w,
                            h,
                            colour);
}

void Display_ComposeChar32(uint16_t clip_width,
                           uint16_t clip_height,
                           uint16_t x,
                           uint16_t y,
                           char ch,
                           FontDef32 font,
                           uint16_t colour,
                           uint16_t background)
{
    DisplayCompose_Char32(clip_width,
                          clip_height,
                          x,
                          y,
                          ch,
                          font,
                          colour,
                          background);
}

void Display_ComposeString16(uint16_t clip_width,
                             uint16_t clip_height,
                             uint16_t x,
                             uint16_t y,
                             const char *text,
                             FontDef font,
                             uint16_t colour,
                             uint16_t background)
{
    DisplayCompose_String16(clip_width,
                            clip_height,
                            x,
                            y,
                            text,
                            font,
                            colour,
                            background);
}

void Display_ComposeString32(uint16_t clip_width,
                             uint16_t clip_height,
                             uint16_t x,
                             uint16_t y,
                             const char *text,
                             FontDef32 font,
                             uint16_t colour,
                             uint16_t background)
{
    DisplayCompose_String32(clip_width,
                            clip_height,
                            x,
                            y,
                            text,
                            font,
                            colour,
                            background);
}

void Display_ComposeString32Literal(uint16_t clip_width,
                                    uint16_t clip_height,
                                    uint16_t x,
                                    uint16_t y,
                                    const char *text,
                                    FontDef32 font,
                                    uint16_t colour,
                                    uint16_t background)
{
    /* Kept as a named wrapper so call sites that conceptually render constant
     * UI copy remain obvious after the compose layer was extracted. */
    DisplayCompose_String32(clip_width,
                            clip_height,
                            x,
                            y,
                            text,
                            font,
                            colour,
                            background);
}

void Display_ComposeBlit(uint16_t x,
                         uint16_t y,
                         uint16_t width,
                         uint16_t height)
{
    DisplayCompose_Blit(x,
                        y,
                        width,
                        height);
}

void Display_FormatMenuOptionalField(char *buffer,
                                     size_t buffer_size,
                                     uint8_t value,
                                     uint8_t unused_value,
                                     uint8_t digits,
                                     uint8_t highlighted)
{
    char field_text[5];

    if (!buffer || buffer_size == 0U || digits == 0U || digits >= sizeof(field_text))
        return;

    if (value == unused_value)
    {
        /* Optional numeric fields render as dashes instead of blanks so the UI
         * communicates "unused" rather than "missing redraw". */
        memset(field_text, '-', digits);
        field_text[digits] = '\0';
    }
    else
    {
        (void)snprintf(field_text, sizeof(field_text), "%*u", digits, value);
    }

    (void)highlighted;
    (void)snprintf(buffer, buffer_size, "%s", field_text);
}

static void Display_MenuRowComposeFillRect(uint16_t x,
                                           uint16_t y,
                                           uint16_t w,
                                           uint16_t h,
                                           uint16_t colour)
{
    Display_ComposeFillRect(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            x,
                            y,
                            w,
                            h,
                            colour);
}

static void Display_MenuRowComposeString32(uint16_t x,
                                           uint16_t y,
                                           const char *text,
                                           FontDef32 font,
                                           uint16_t colour,
                                           uint16_t background)
{
    Display_ComposeString32(ST7796_WIDTH,
                            MAIN_INFO_FONT_CELL_HEIGHT,
                            x,
                            y,
                            text,
                            font,
                            colour,
                            background);
}

void Display_MenuRowComposeClear(uint16_t colour)
{
    Display_ComposeClear(ST7796_WIDTH,
                         MAIN_INFO_FONT_CELL_HEIGHT,
                         colour);
}

void Display_MenuRowComposeTextSegment32(uint16_t x,
                                         const char *text,
                                         uint16_t foreground,
                                         uint16_t background)
{
    size_t text_length = text ? strlen(text) : 0U;

    if (text_length == 0U)
        return;

    Display_MenuRowComposeFillRect(x,
                                   0U,
                                   (uint16_t)(text_length * MAIN_INFO_FONT.width),
                                   MAIN_INFO_FONT.height,
                                   background);
    Display_MenuRowComposeString32(x,
                                   0U,
                                   text,
                                   MAIN_INFO_FONT,
                                   foreground,
                                   background);
}

uint16_t Display_MenuRowComposeValueSegment32(uint16_t x,
                                              const char *text,
                                              uint8_t highlighted)
{
    size_t text_length = text ? strlen(text) : 0U;

    if (text_length == 0U)
        return x;

    /* Return the next X position so dense row builders can chain segments left
     * to right without repeating width calculations at each call site. */
    Display_MenuRowComposeTextSegment32(x,
                                        text,
                                        highlighted ? MAIN_INFO_EDIT_CURSOR_TEXT_COLOUR : MAIN_INFO_TEXT_COLOUR,
                                        highlighted ? MAIN_INFO_EDIT_CURSOR_BG_COLOUR : DISPLAY_BG_COLOUR);

    return (uint16_t)(x + ((uint16_t)text_length * MAIN_INFO_FONT.width));
}

void Display_MenuRowComposeBlit(uint16_t row_y)
{
    Display_ComposeBlit(0U,
                        row_y,
                        ST7796_WIDTH,
                        MAIN_INFO_FONT_CELL_HEIGHT);
}