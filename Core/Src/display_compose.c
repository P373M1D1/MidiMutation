#include "display_compose.h"
#include "st7796.h"
#include <string.h>

#define DISPLAY_COMPOSE_BUFFER_HEIGHT 49U

/* Dynamic display updates should compose into this shared scratch buffer and
 * reach the panel through one blit. Narrow blits are packed here before the
 * final DrawImage call so callers do not have to repeat that rule. */
static uint16_t display_compose_buffer[ST7796_WIDTH * DISPLAY_COMPOSE_BUFFER_HEIGHT];

static void DisplayCompose_CompactRows(uint16_t packed_width, uint16_t height)
{
    if (packed_width == 0U || packed_width >= ST7796_WIDTH)
        return;

    /* Compose callers write rows into a full-width scratch buffer. Before a
     * narrow blit, compact each row down so ST7796_DrawImage sees packed data. */
    for (uint16_t row = 1U; row < height; ++row)
    {
        memmove(&display_compose_buffer[(uint32_t)row * packed_width],
                &display_compose_buffer[(uint32_t)row * ST7796_WIDTH],
                (size_t)packed_width * sizeof(display_compose_buffer[0]));
    }
}

static void DisplayCompose_Char16(uint16_t clip_width,
                                  uint16_t clip_height,
                                  uint16_t x,
                                  uint16_t y,
                                  char ch,
                                  FontDef font,
                                  uint16_t colour,
                                  uint16_t background)
{
    uint32_t glyph_offset;

    /* The built-in font tables only cover printable ASCII. Unknown values are
     * rendered as '?' so the UI never reads from an invalid glyph slot. */
    if (ch < 32 || ch > 126)
        ch = '?';

    if (x >= clip_width || y >= clip_height)
        return;

    glyph_offset = (uint32_t)(ch - 32) * font.height;

    for (uint8_t row = 0U; row < font.height; ++row)
    {
        uint16_t target_y = (uint16_t)(y + row);
        uint16_t bitmap;
        uint32_t row_offset;

        if (target_y >= clip_height)
            break;

        bitmap = font.data[glyph_offset + row];
        row_offset = (uint32_t)target_y * ST7796_WIDTH;

        for (uint8_t column = 0U; column < font.width; ++column)
        {
            uint16_t target_x = (uint16_t)(x + column);

            if (target_x >= clip_width)
                break;

            display_compose_buffer[row_offset + target_x] = (bitmap & (uint16_t)(0x8000U >> column))
                ? colour
                : background;
        }
    }
}

static void DisplayCompose_Char16Transparent(uint16_t clip_width,
                                             uint16_t clip_height,
                                             uint16_t x,
                                             uint16_t y,
                                             char ch,
                                             FontDef font,
                                             uint16_t colour)
{
    uint32_t glyph_offset;

    if (ch < 32 || ch > 126)
        ch = '?';

    if (x >= clip_width || y >= clip_height)
        return;

    glyph_offset = (uint32_t)(ch - 32) * font.height;

    for (uint8_t row = 0U; row < font.height; ++row)
    {
        uint16_t target_y = (uint16_t)(y + row);
        uint16_t bitmap;
        uint32_t row_offset;

        if (target_y >= clip_height)
            break;

        bitmap = font.data[glyph_offset + row];
        row_offset = (uint32_t)target_y * ST7796_WIDTH;

        for (uint8_t column = 0U; column < font.width; ++column)
        {
            uint16_t target_x = (uint16_t)(x + column);

            if (target_x >= clip_width)
                break;

            if (bitmap & (uint16_t)(0x8000U >> column))
                display_compose_buffer[row_offset + target_x] = colour;
        }
    }
}

void DisplayCompose_Clear(uint16_t clip_width,
                          uint16_t clip_height,
                          uint16_t colour)
{
    DisplayCompose_FillRect(clip_width,
                            clip_height,
                            0U,
                            0U,
                            clip_width,
                            clip_height,
                            colour);
}

void DisplayCompose_FillRect(uint16_t clip_width,
                             uint16_t clip_height,
                             uint16_t x,
                             uint16_t y,
                             uint16_t w,
                             uint16_t h,
                             uint16_t colour)
{
    uint16_t x_end;
    uint16_t y_end;

    if (x >= clip_width || y >= clip_height || w == 0U || h == 0U)
        return;

    x_end = (uint16_t)(x + w);
    y_end = (uint16_t)(y + h);

    if (x_end > clip_width)
        x_end = clip_width;
    if (y_end > clip_height)
        y_end = clip_height;

    for (uint16_t row = y; row < y_end; ++row)
    {
        uint32_t row_offset = (uint32_t)row * ST7796_WIDTH;

        for (uint16_t column = x; column < x_end; ++column)
            display_compose_buffer[row_offset + column] = colour;
    }
}

void DisplayCompose_Char32(uint16_t clip_width,
                           uint16_t clip_height,
                           uint16_t x,
                           uint16_t y,
                           char ch,
                           FontDef32 font,
                           uint16_t colour,
                           uint16_t background)
{
    uint32_t glyph_offset;

    if (ch < 32 || ch > 126)
        ch = '?';

    if (x >= clip_width || y >= clip_height)
        return;

    glyph_offset = (uint32_t)(ch - 32) * font.height;

    for (uint8_t row = 0U; row < font.height; ++row)
    {
        uint16_t target_y = (uint16_t)(y + row);
        uint32_t bitmap;
        uint32_t row_offset;

        if (target_y >= clip_height)
            break;

        bitmap = font.data[glyph_offset + row];
        row_offset = (uint32_t)target_y * ST7796_WIDTH;

        for (uint8_t column = 0U; column < font.width; ++column)
        {
            uint16_t target_x = (uint16_t)(x + column);

            if (target_x >= clip_width)
                break;

            display_compose_buffer[row_offset + target_x] = (bitmap & (0x80000000UL >> column))
                ? colour
                : background;
        }
    }
}

static void DisplayCompose_Char32Transparent(uint16_t clip_width,
                                             uint16_t clip_height,
                                             uint16_t x,
                                             uint16_t y,
                                             char ch,
                                             FontDef32 font,
                                             uint16_t colour)
{
    uint32_t glyph_offset;

    if (ch < 32 || ch > 126)
        ch = '?';

    if (x >= clip_width || y >= clip_height)
        return;

    glyph_offset = (uint32_t)(ch - 32) * font.height;

    for (uint8_t row = 0U; row < font.height; ++row)
    {
        uint16_t target_y = (uint16_t)(y + row);
        uint32_t bitmap;
        uint32_t row_offset;

        if (target_y >= clip_height)
            break;

        bitmap = font.data[glyph_offset + row];
        row_offset = (uint32_t)target_y * ST7796_WIDTH;

        for (uint8_t column = 0U; column < font.width; ++column)
        {
            uint16_t target_x = (uint16_t)(x + column);

            if (target_x >= clip_width)
                break;

            if (bitmap & (0x80000000UL >> column))
                display_compose_buffer[row_offset + target_x] = colour;
        }
    }
}

void DisplayCompose_String32(uint16_t clip_width,
                             uint16_t clip_height,
                             uint16_t x,
                             uint16_t y,
                             const char *text,
                             FontDef32 font,
                             uint16_t colour,
                             uint16_t background)
{
    uint16_t draw_x = x;

    if (!text)
        return;

    while (*text != '\0')
    {
        if (draw_x >= clip_width)
            break;

        DisplayCompose_Char32(clip_width,
                              clip_height,
                              draw_x,
                              y,
                              *text,
                              font,
                              colour,
                              background);
        draw_x = (uint16_t)(draw_x + font.width);
        ++text;
    }
}

void DisplayCompose_String16(uint16_t clip_width,
                             uint16_t clip_height,
                             uint16_t x,
                             uint16_t y,
                             const char *text,
                             FontDef font,
                             uint16_t colour,
                             uint16_t background)
{
    uint16_t draw_x = x;

    if (!text)
        return;

    while (*text != '\0')
    {
        if (draw_x >= clip_width)
            break;

        DisplayCompose_Char16(clip_width,
                              clip_height,
                              draw_x,
                              y,
                              *text,
                              font,
                              colour,
                              background);
        draw_x = (uint16_t)(draw_x + font.width);
        ++text;
    }
}

void DisplayCompose_String16Transparent(uint16_t clip_width,
                                        uint16_t clip_height,
                                        uint16_t x,
                                        uint16_t y,
                                        const char *text,
                                        FontDef font,
                                        uint16_t colour)
{
    uint16_t draw_x = x;

    if (!text)
        return;

    while (*text != '\0')
    {
        if (draw_x >= clip_width)
            break;

        DisplayCompose_Char16Transparent(clip_width,
                                         clip_height,
                                         draw_x,
                                         y,
                                         *text,
                                         font,
                                         colour);
        draw_x = (uint16_t)(draw_x + font.width);
        ++text;
    }
}

void DisplayCompose_String32Transparent(uint16_t clip_width,
                                        uint16_t clip_height,
                                        uint16_t x,
                                        uint16_t y,
                                        const char *text,
                                        FontDef32 font,
                                        uint16_t colour)
{
    uint16_t draw_x = x;

    if (!text)
        return;

    while (*text != '\0')
    {
        if (draw_x >= clip_width)
            break;

        DisplayCompose_Char32Transparent(clip_width,
                                         clip_height,
                                         draw_x,
                                         y,
                                         *text,
                                         font,
                                         colour);
        draw_x = (uint16_t)(draw_x + font.width);
        ++text;
    }
}

void DisplayCompose_LoadImageSwapRBRegion(uint16_t clip_width,
                                          uint16_t clip_height,
                                          uint16_t src_x,
                                          uint16_t src_y,
                                          const uint16_t *image,
                                          uint16_t image_width,
                                          uint16_t image_height)
{
    if (!image || clip_width == 0U || clip_height == 0U)
        return;

    if (src_x >= image_width || src_y >= image_height)
        return;

    if ((uint32_t)src_x + clip_width > image_width)
        clip_width = (uint16_t)(image_width - src_x);
    if ((uint32_t)src_y + clip_height > image_height)
        clip_height = (uint16_t)(image_height - src_y);

    for (uint16_t row = 0U; row < clip_height; ++row)
    {
        uint32_t dst_offset = (uint32_t)row * ST7796_WIDTH;
        uint32_t src_offset = ((uint32_t)(src_y + row) * image_width) + src_x;

        for (uint16_t column = 0U; column < clip_width; ++column)
        {
            uint16_t pixel = image[src_offset + column];
            uint16_t rgb565 = (uint16_t)((pixel & 0x07E0U)
                                       | ((pixel & 0xF800U) >> 11)
                                       | ((pixel & 0x001FU) << 11));

            display_compose_buffer[dst_offset + column] = rgb565;
        }
    }
}

void DisplayCompose_Blit(uint16_t x,
                         uint16_t y,
                         uint16_t width,
                         uint16_t height)
{
    if (width == 0U || height == 0U)
        return;

    /* Packing happens at the last moment so upstream compose helpers can keep
     * using simple full-width row offsets while drawing into the scratch buffer. */
    DisplayCompose_CompactRows(width, height);
    ST7796_DrawImage(x,
                     y,
                     width,
                     height,
                     display_compose_buffer);
}