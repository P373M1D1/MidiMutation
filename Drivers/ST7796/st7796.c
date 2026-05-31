#include "st7796.h"
#include <stdlib.h>   /* abs() */
#include <string.h>

#define ST7796_DMA_MIN_TRANSFER_BYTES 32U
#define ST7796_FILL_CHUNK_PIXELS 128U
#define ST7796_IMAGE_CHUNK_PIXELS 128U
#define ST7796_GLYPH16_MAX_WIDTH 16U
#define ST7796_GLYPH16_MAX_HEIGHT 26U
#define ST7796_GLYPH32_MAX_WIDTH 32U
#define ST7796_GLYPH32_MAX_HEIGHT 64U

static void ST7796_SetAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

void ST7796_InitControlPins(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    HAL_GPIO_WritePin(ST7796_RST_GPIO_Port, ST7796_RST_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ST7796_CS_GPIO_Port, ST7796_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ST7796_DC_GPIO_Port, ST7796_DC_Pin, GPIO_PIN_SET);

    gpio_init.Pin = ST7796_RST_Pin;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ST7796_RST_GPIO_Port, &gpio_init);

    gpio_init.Pin = ST7796_CS_Pin | ST7796_DC_Pin;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ST7796_CS_GPIO_Port, &gpio_init);
}

#ifdef ST7796_USE_DMA
static void ST7796_TransmitBuffer(const uint8_t *buf, uint16_t size)
{
    if (size == 0U) {
        return;
    }

    if (size < ST7796_DMA_MIN_TRANSFER_BYTES) {
        HAL_SPI_Transmit(&ST7796_SPI_PORT, (uint8_t *)buf, size, HAL_MAX_DELAY);
        return;
    }

    if (HAL_SPI_Transmit_DMA(&ST7796_SPI_PORT, (uint8_t *)buf, size) != HAL_OK) {
        HAL_SPI_Transmit(&ST7796_SPI_PORT, (uint8_t *)buf, size, HAL_MAX_DELAY);
        return;
    }

    while (HAL_SPI_GetState(&ST7796_SPI_PORT) != HAL_SPI_STATE_READY) {
    }
}
#else
static void ST7796_TransmitBuffer(const uint8_t *buf, uint16_t size)
{
    if (size == 0U) {
        return;
    }

    HAL_SPI_Transmit(&ST7796_SPI_PORT, (uint8_t *)buf, size, HAL_MAX_DELAY);
}
#endif

static void ST7796_TransmitColorBurst(uint16_t color, uint32_t pixel_count)
{
    static uint8_t fill_chunk[ST7796_FILL_CHUNK_PIXELS * 2U];
    const uint8_t hi = (uint8_t)(color >> 8);
    const uint8_t lo = (uint8_t)(color & 0xFF);

    for (uint32_t idx = 0U; idx < ST7796_FILL_CHUNK_PIXELS; idx++) {
        fill_chunk[(idx * 2U)] = hi;
        fill_chunk[(idx * 2U) + 1U] = lo;
    }

    while (pixel_count > 0U) {
        const uint32_t chunk_pixels = (pixel_count > ST7796_FILL_CHUNK_PIXELS) ? ST7796_FILL_CHUNK_PIXELS : pixel_count;
        ST7796_TransmitBuffer(fill_chunk, (uint16_t)(chunk_pixels * 2U));
        pixel_count -= chunk_pixels;
    }
}

static void ST7796_TransmitImagePixels(const uint16_t *data, uint32_t pixel_count)
{
    static uint8_t image_chunk[ST7796_IMAGE_CHUNK_PIXELS * 2U];

    while (pixel_count > 0U) {
        const uint32_t chunk_pixels = (pixel_count > ST7796_IMAGE_CHUNK_PIXELS) ? ST7796_IMAGE_CHUNK_PIXELS : pixel_count;

        for (uint32_t idx = 0U; idx < chunk_pixels; idx++) {
            const uint16_t pixel = data[idx];
            image_chunk[(idx * 2U)] = (uint8_t)(pixel >> 8);
            image_chunk[(idx * 2U) + 1U] = (uint8_t)(pixel & 0xFF);
        }

        ST7796_TransmitBuffer(image_chunk, (uint16_t)(chunk_pixels * 2U));
        data += chunk_pixels;
        pixel_count -= chunk_pixels;
    }
}

static void ST7796_TransmitImagePixelsSwapRB(const uint16_t *data, uint32_t pixel_count)
{
    static uint8_t image_chunk[ST7796_IMAGE_CHUNK_PIXELS * 2U];

    while (pixel_count > 0U) {
        const uint32_t chunk_pixels = (pixel_count > ST7796_IMAGE_CHUNK_PIXELS) ? ST7796_IMAGE_CHUNK_PIXELS : pixel_count;

        for (uint32_t idx = 0U; idx < chunk_pixels; idx++) {
            const uint16_t pixel = data[idx];
            const uint16_t swapped = (uint16_t)((pixel & 0x07E0U)
                                              | ((pixel & 0xF800U) >> 11)
                                              | ((pixel & 0x001FU) << 11));
            image_chunk[(idx * 2U)] = (uint8_t)(swapped >> 8);
            image_chunk[(idx * 2U) + 1U] = (uint8_t)(swapped & 0xFF);
        }

        ST7796_TransmitBuffer(image_chunk, (uint16_t)(chunk_pixels * 2U));
        data += chunk_pixels;
        pixel_count -= chunk_pixels;
    }
}

static void ST7796_WriteGlyph16Opaque(uint16_t x, uint16_t y,
                                      const uint16_t *glyph_rows,
                                      uint8_t width,
                                      uint8_t height,
                                      uint16_t color,
                                      uint16_t bgcolor)
{
    static uint8_t pixbuf[ST7796_GLYPH16_MAX_WIDTH * ST7796_GLYPH16_MAX_HEIGHT * 2U];
    const uint8_t hi_fg = (uint8_t)(color >> 8);
    const uint8_t lo_fg = (uint8_t)(color & 0xFF);
    const uint8_t hi_bg = (uint8_t)(bgcolor >> 8);
    const uint8_t lo_bg = (uint8_t)(bgcolor & 0xFF);
    uint32_t idx = 0U;

    if ((x + width > ST7796_WIDTH) || (y + height > ST7796_HEIGHT)) {
        return;
    }

    for (uint8_t row = 0U; row < height; row++) {
        const uint16_t bitmap = glyph_rows[row];
        for (uint8_t col = 0U; col < width; col++) {
            if (bitmap & (0x8000U >> col)) {
                pixbuf[idx++] = hi_fg;
                pixbuf[idx++] = lo_fg;
            } else {
                pixbuf[idx++] = hi_bg;
                pixbuf[idx++] = lo_bg;
            }
        }
    }

    ST7796_SetAddressWindow(x, y, (uint16_t)(x + width - 1U), (uint16_t)(y + height - 1U));
    ST7796_CS_Clr();
    ST7796_DC_Set();
    ST7796_TransmitBuffer(pixbuf, (uint16_t)(width * height * 2U));
    ST7796_CS_Set();
}

static void ST7796_WriteGlyph16Transparent(uint16_t x, uint16_t y,
                                           const uint16_t *glyph_rows,
                                           uint8_t width,
                                           uint8_t height,
                                           uint16_t color)
{
    if ((x + width > ST7796_WIDTH) || (y + height > ST7796_HEIGHT)) {
        return;
    }

    for (uint8_t row = 0U; row < height; row++) {
        const uint16_t bitmap = glyph_rows[row];
        uint8_t col = 0U;

        while (col < width) {
            while ((col < width) && ((bitmap & (0x8000U >> col)) == 0U)) {
                col++;
            }

            if (col >= width) {
                break;
            }

            const uint8_t run_start = col;
            while ((col < width) && (bitmap & (0x8000U >> col))) {
                col++;
            }

            ST7796_SetAddressWindow((uint16_t)(x + run_start),
                                    (uint16_t)(y + row),
                                    (uint16_t)(x + col - 1U),
                                    (uint16_t)(y + row));
            ST7796_CS_Clr();
            ST7796_DC_Set();
            ST7796_TransmitColorBurst(color, (uint32_t)(col - run_start));
            ST7796_CS_Set();
        }
    }
}

/* ?????? Low-level helpers ????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */

static void ST7796_WriteCmd(uint8_t cmd)
{
    ST7796_CS_Clr();
    ST7796_DC_Clr();    /* command mode */
    HAL_SPI_Transmit(&ST7796_SPI_PORT, &cmd, 1, 100);
    ST7796_CS_Set();
}

static void ST7796_WriteData(uint8_t data)
{
    ST7796_CS_Clr();
    ST7796_DC_Set();    /* data mode */
    HAL_SPI_Transmit(&ST7796_SPI_PORT, &data, 1, 100);
    ST7796_CS_Set();
}

static void ST7796_WriteData16(uint16_t data)
{
    uint8_t buf[2] = { (uint8_t)(data >> 8), (uint8_t)(data & 0xFF) };
    ST7796_CS_Clr();
    ST7796_DC_Set();
    ST7796_TransmitBuffer(buf, 2U);
    ST7796_CS_Set();
}

/* Set the address window for subsequent RAMWR writes */
static void ST7796_SetAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    /* Column address */
    ST7796_WriteCmd(ST7796_CASET);
    ST7796_WriteData((x0 >> 8) & 0xFF);
    ST7796_WriteData(x0 & 0xFF);
    ST7796_WriteData((x1 >> 8) & 0xFF);
    ST7796_WriteData(x1 & 0xFF);

    /* Row address */
    ST7796_WriteCmd(ST7796_RASET);
    ST7796_WriteData((y0 >> 8) & 0xFF);
    ST7796_WriteData(y0 & 0xFF);
    ST7796_WriteData((y1 >> 8) & 0xFF);
    ST7796_WriteData(y1 & 0xFF);

    /* Write to RAM */
    ST7796_WriteCmd(ST7796_RAMWR);
}

/* ?????? Initialisation ????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */

void ST7796_Init(void)
{
    /* Ensure CS is deasserted and DC is in a known state before we start */
    ST7796_CS_Set();
    ST7796_DC_Set();

    /* Hardware reset: low 100ms, high 50ms */
    ST7796_RST_Clr();
    HAL_Delay(100);
    ST7796_RST_Set();
    HAL_Delay(50);

    /* Exit sleep */
    ST7796_WriteCmd(ST7796_SLPOUT);
    HAL_Delay(120);

    /* Memory access control */
    ST7796_SetRotation(ST7796_ROTATION);

    /* Pixel format: 16-bit RGB565 */
    ST7796_WriteCmd(ST7796_COLMOD);
    ST7796_WriteData(0x55);

    /* Unlock manufacturer command set */
    ST7796_WriteCmd(0xF0); ST7796_WriteData(0xC3);
    ST7796_WriteCmd(0xF0); ST7796_WriteData(0x96);

    /* Display inversion control */
    ST7796_WriteCmd(0xB4); ST7796_WriteData(0x02);

    /* Entry mode set */
    ST7796_WriteCmd(0xB7); ST7796_WriteData(0xC6);

    /* Power control 1 */
    ST7796_WriteCmd(0xC0); ST7796_WriteData(0xC0); ST7796_WriteData(0x00);

    /* Power control 2 */
    ST7796_WriteCmd(0xC1); ST7796_WriteData(0x13);

    /* Power control 3 */
    ST7796_WriteCmd(0xC2); ST7796_WriteData(0xA7);

    /* VCOM control */
    ST7796_WriteCmd(0xC5); ST7796_WriteData(0x21);

    /* Display output ctrl adjust */
    ST7796_WriteCmd(0xE8);
    ST7796_WriteData(0x40); ST7796_WriteData(0x8A); ST7796_WriteData(0x1B); ST7796_WriteData(0x1B);
    ST7796_WriteData(0x23); ST7796_WriteData(0x0A); ST7796_WriteData(0xAC); ST7796_WriteData(0x33);

    /* Positive gamma correction */
    ST7796_WriteCmd(0xE0);
    ST7796_WriteData(0xD2); ST7796_WriteData(0x05); ST7796_WriteData(0x08); ST7796_WriteData(0x06);
    ST7796_WriteData(0x05); ST7796_WriteData(0x02); ST7796_WriteData(0x2A); ST7796_WriteData(0x44);
    ST7796_WriteData(0x46); ST7796_WriteData(0x39); ST7796_WriteData(0x15); ST7796_WriteData(0x15);
    ST7796_WriteData(0x2D); ST7796_WriteData(0x32);

    /* Negative gamma correction */
    ST7796_WriteCmd(0xE1);
    ST7796_WriteData(0x96); ST7796_WriteData(0x08); ST7796_WriteData(0x0C); ST7796_WriteData(0x09);
    ST7796_WriteData(0x09); ST7796_WriteData(0x25); ST7796_WriteData(0x2E); ST7796_WriteData(0x43);
    ST7796_WriteData(0x42); ST7796_WriteData(0x35); ST7796_WriteData(0x11); ST7796_WriteData(0x11);
    ST7796_WriteData(0x28); ST7796_WriteData(0x2E);

    /* Lock manufacturer command set */
    ST7796_WriteCmd(0xF0); ST7796_WriteData(0x3C);
    ST7796_WriteCmd(0xF0); ST7796_WriteData(0x69);
    HAL_Delay(120);

    /* Display inversion on (required for correct colours on this module) */
    ST7796_WriteCmd(ST7796_INVON);

    /* Display on */
    ST7796_WriteCmd(ST7796_DISPON);
}

/* ?????? Rotation ??????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */

void ST7796_SetRotation(uint8_t rot)
{
    ST7796_WriteCmd(ST7796_MADCTL);
    switch (rot & 0x03) {
        case 0:  /* 0?? portrait */
            ST7796_WriteData(0x40 | 0x08);   /* MX | BGR */
            break;
        case 1:  /* 90?? landscape */
            ST7796_WriteData(0x20 | 0x08);   /* MV | BGR */
            break;
        case 2:  /* 180?? portrait */
            ST7796_WriteData(0x80 | 0x08);   /* MY | BGR */
            break;
        case 3:  /* 270?? landscape */
            ST7796_WriteData(0xE0 | 0x08);   /* MY + MX + MV | BGR */
            break;
    }
}

/* ?????? Colour inversion ???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */

void ST7796_InvertColors(uint8_t invert)
{
    ST7796_WriteCmd(invert ? ST7796_INVON : ST7796_INVOFF);
}

/* ?????? Drawing primitives ????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */

void ST7796_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= ST7796_WIDTH || y >= ST7796_HEIGHT) return;
    ST7796_SetAddressWindow(x, y, x, y);
    ST7796_WriteData16(color);
}

void ST7796_Fill(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    if (x0 >= ST7796_WIDTH || y0 >= ST7796_HEIGHT) return;
    if (x1 >= ST7796_WIDTH)  x1 = ST7796_WIDTH - 1;
    if (y1 >= ST7796_HEIGHT) y1 = ST7796_HEIGHT - 1;

    uint32_t count = (uint32_t)(x1 - x0 + 1) * (y1 - y0 + 1);

    ST7796_SetAddressWindow(x0, y0, x1, y1);

    ST7796_CS_Clr();
    ST7796_DC_Set();

    ST7796_TransmitColorBurst(color, count);

    ST7796_CS_Set();
}

void ST7796_FillScreen(uint16_t color)
{
    ST7796_Fill(0, 0, ST7796_WIDTH - 1, ST7796_HEIGHT - 1, color);
}

void ST7796_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    int16_t dx = (int16_t)(x1 > x0 ? x1 - x0 : x0 - x1);
    int16_t dy = (int16_t)(y1 > y0 ? y1 - y0 : y0 - y1);
    int16_t sx = x0 < x1 ? 1 : -1;
    int16_t sy = y0 < y1 ? 1 : -1;
    int16_t err = dx - dy;
    int16_t x = (int16_t)x0;
    int16_t y = (int16_t)y0;

    while (1) {
        ST7796_DrawPixel((uint16_t)x, (uint16_t)y, color);
        if (x == (int16_t)x1 && y == (int16_t)y1) break;
        int16_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 <  dx) { err += dx; y += sy; }
    }
}

void ST7796_DrawRectangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    ST7796_DrawLine(x0, y0, x1, y0, color);
    ST7796_DrawLine(x1, y0, x1, y1, color);
    ST7796_DrawLine(x1, y1, x0, y1, color);
    ST7796_DrawLine(x0, y1, x0, y0, color);
}

void ST7796_DrawFilledRectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    ST7796_Fill(x, y, x + w - 1, y + h - 1, color);
}

void ST7796_DrawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
    int16_t x = 0, y = r;
    int16_t d = 3 - 2 * r;

    while (y >= x) {
        ST7796_DrawPixel((uint16_t)(x0 + x), (uint16_t)(y0 + y), color);
        ST7796_DrawPixel((uint16_t)(x0 - x), (uint16_t)(y0 + y), color);
        ST7796_DrawPixel((uint16_t)(x0 + x), (uint16_t)(y0 - y), color);
        ST7796_DrawPixel((uint16_t)(x0 - x), (uint16_t)(y0 - y), color);
        ST7796_DrawPixel((uint16_t)(x0 + y), (uint16_t)(y0 + x), color);
        ST7796_DrawPixel((uint16_t)(x0 - y), (uint16_t)(y0 + x), color);
        ST7796_DrawPixel((uint16_t)(x0 + y), (uint16_t)(y0 - x), color);
        ST7796_DrawPixel((uint16_t)(x0 - y), (uint16_t)(y0 - x), color);
        x++;
        if (d < 0) d += 4 * x + 6;
        else       { d += 4 * (x - y) + 10; y--; }
    }
}

void ST7796_DrawFilledCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
    int16_t x = 0, y = r;
    int16_t d = 3 - 2 * r;

    while (y >= x) {
        ST7796_DrawLine((uint16_t)(x0 - x), (uint16_t)(y0 + y),
                        (uint16_t)(x0 + x), (uint16_t)(y0 + y), color);
        ST7796_DrawLine((uint16_t)(x0 - x), (uint16_t)(y0 - y),
                        (uint16_t)(x0 + x), (uint16_t)(y0 - y), color);
        ST7796_DrawLine((uint16_t)(x0 - y), (uint16_t)(y0 + x),
                        (uint16_t)(x0 + y), (uint16_t)(y0 + x), color);
        ST7796_DrawLine((uint16_t)(x0 - y), (uint16_t)(y0 - x),
                        (uint16_t)(x0 + y), (uint16_t)(y0 - x), color);
        x++;
        if (d < 0) d += 4 * x + 6;
        else       { d += 4 * (x - y) + 10; y--; }
    }
}

/* ?????? Image / text ??????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */

void ST7796_DrawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data)
{
    if (x + w > ST7796_WIDTH || y + h > ST7796_HEIGHT) return;

    ST7796_SetAddressWindow(x, y, x + w - 1, y + h - 1);

    ST7796_CS_Clr();
    ST7796_DC_Set();

    ST7796_TransmitImagePixels(data, (uint32_t)w * h);

    ST7796_CS_Set();
}

void ST7796_DrawImageSwapRB(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data)
{
    if (x + w > ST7796_WIDTH || y + h > ST7796_HEIGHT) return;

    ST7796_SetAddressWindow(x, y, x + w - 1, y + h - 1);

    ST7796_CS_Clr();
    ST7796_DC_Set();

    ST7796_TransmitImagePixelsSwapRB(data, (uint32_t)w * h);

    ST7796_CS_Set();
}

void ST7796_FadeIn(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                   const uint16_t *data, uint8_t steps, uint16_t step_delay_ms)
{
    if (x + w > ST7796_WIDTH || y + h > ST7796_HEIGHT) return;
    uint32_t count = (uint32_t)w * h;

    for (uint8_t step = 1; step <= steps; step++) {
        ST7796_SetAddressWindow(x, y, x + w - 1, y + h - 1);
        ST7796_CS_Clr();
        ST7796_DC_Set();

        for (uint32_t i = 0; i < count; i++) {
            /* Undo the byte-swap applied during image conversion to get
               standard RGB565: R[15:11] G[10:5] B[4:0] */
           uint16_t rgb = (uint16_t)(((data[i] & 0xFF) << 8) | (data[i] >> 8));
           uint8_t r = (rgb >> 11) & 0x1F;
           uint8_t g = (rgb >> 5)  & 0x3F;
          uint8_t b =  rgb        & 0x1F;
          r = (uint8_t)((uint32_t)r * step / steps);
          g = (uint8_t)((uint32_t)g * step / steps);
          b = (uint8_t)((uint32_t)b * step / steps);
           uint16_t dimmed = ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
           /* Re-apply byte-swap to match the format DrawImage uses */
           uint8_t buf[2] = { (uint8_t)(dimmed & 0xFF), (uint8_t)(dimmed >> 8) };
          ST7796_TransmitBuffer(buf, 2U);
      }

        ST7796_CS_Set();
        if (step_delay_ms > 0) HAL_Delay(step_delay_ms);
    }
}

void ST7796_WriteChar(uint16_t x, uint16_t y, char ch, FontDef font, uint16_t color, uint16_t bgcolor)
{
    if (ch < 32 || ch > 126) ch = '?';

    const uint32_t offset = (uint32_t)(ch - 32) * font.height;
    ST7796_WriteGlyph16Opaque(x, y, &font.data[offset], font.width, font.height, color, bgcolor);
}

void ST7796_WriteString(uint16_t x, uint16_t y, const char *str, FontDef font, uint16_t color, uint16_t bgcolor)
{
    uint16_t cx = x;
    while (*str) {
        if (cx + font.width > ST7796_WIDTH) {
            cx = x;
            y += font.height;
            if (y + font.height > ST7796_HEIGHT) break;
        }
        ST7796_WriteChar(cx, y, *str, font, color, bgcolor);
        cx += font.width;
        str++;
    }
}

void ST7796_WriteStringTransparent(uint16_t x, uint16_t y, const char *str, FontDef font, uint16_t color)
{
    uint16_t cx = x;
    while (*str) {
        if (cx + font.width > ST7796_WIDTH) {
            cx = x;
            y += font.height;
            if (y + font.height > ST7796_HEIGHT) break;
        }
        char ch = *str;
        if (ch < 32 || ch > 126) ch = '?';
        const uint32_t offset = (uint32_t)(ch - 32) * font.height;
        ST7796_WriteGlyph16Transparent(cx, y, &font.data[offset], font.width, font.height, color);
        cx += font.width;
        str++;
    }
}

/* ?????? 32-bit wide font rendering (for fonts wider than 16 px, e.g. Consolas 24x40) ?????? */

void ST7796_WriteChar32(uint16_t x, uint16_t y, char ch, FontDef32 font, uint16_t color, uint16_t bgcolor)
{
    if (ch < 32 || ch > 126) ch = '?';
    uint32_t offset = (uint32_t)(ch - 32) * font.height;

    uint8_t hi_fg = (uint8_t)(color   >> 8), lo_fg = (uint8_t)(color   & 0xFF);
    uint8_t hi_bg = (uint8_t)(bgcolor >> 8), lo_bg = (uint8_t)(bgcolor & 0xFF);

    /* Build pixel buffer: width ?? height pixels, 2 bytes each */
    static uint8_t pixbuf[ST7796_GLYPH32_MAX_WIDTH * ST7796_GLYPH32_MAX_HEIGHT * 2U];
    uint32_t idx = 0;
    for (uint8_t row = 0; row < font.height; row++) {
        uint32_t bitmap = font.data[offset + row];
        for (uint8_t col = 0; col < font.width; col++) {
            if (bitmap & (0x80000000UL >> col)) {
                pixbuf[idx++] = hi_fg;
                pixbuf[idx++] = lo_fg;
            } else {
                pixbuf[idx++] = hi_bg;
                pixbuf[idx++] = lo_bg;
            }
        }
    }

    /* Set window once, then blast all pixels in a single SPI transfer */
    ST7796_SetAddressWindow(x, y, (uint16_t)(x + font.width - 1U), (uint16_t)(y + font.height - 1U));
    ST7796_CS_Clr();
    ST7796_DC_Set();
    ST7796_TransmitBuffer(pixbuf, (uint16_t)(font.width * font.height * 2U));
    ST7796_CS_Set();
}

void ST7796_WriteString32(uint16_t x, uint16_t y, const char *str, FontDef32 font, uint16_t color, uint16_t bgcolor)
{
    uint16_t cx = x;
    while (*str) {
        if (cx + font.width > ST7796_WIDTH) {
            cx = x;
            y += font.height;
            if (y + font.height > ST7796_HEIGHT) break;
        }
        ST7796_WriteChar32(cx, y, *str, font, color, bgcolor);
        cx += font.width;
        str++;
    }
}

/* ?????? Self-test ???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */

void ST7796_Test(void)
{
    /* Colour bars */
    uint16_t bar_w = ST7796_WIDTH / 7;
    uint16_t colors[] = { RED, GREEN, BLUE_RYB,
                          CYAN, MAGENTA, YELLOW, WHITE };
    for (int i = 0; i < 7; i++) {
        ST7796_DrawFilledRectangle((uint16_t)(i * bar_w), 0, bar_w, ST7796_HEIGHT, colors[i]);
    }
    HAL_Delay(1000);

    /* Solid fills */
    ST7796_FillScreen(BLACK);
    HAL_Delay(300);
    ST7796_FillScreen(RED);
    HAL_Delay(300);
    ST7796_FillScreen(GREEN);
    HAL_Delay(300);
    ST7796_FillScreen(BLUE_RYB);
    HAL_Delay(300);
    ST7796_FillScreen(BLACK);

    /* Primitives */
    ST7796_DrawRectangle(10, 10, ST7796_WIDTH - 10, ST7796_HEIGHT - 10, WHITE);
    ST7796_DrawFilledRectangle(20, 20, 80, 60, RED);
    ST7796_DrawCircle(ST7796_WIDTH / 2, ST7796_HEIGHT / 2, 60, YELLOW);
    ST7796_DrawFilledCircle(ST7796_WIDTH / 2, ST7796_HEIGHT / 2, 30, CYAN);
    ST7796_DrawLine(0, 0, ST7796_WIDTH - 1, ST7796_HEIGHT - 1, GREEN);
    ST7796_DrawLine(ST7796_WIDTH - 1, 0, 0, ST7796_HEIGHT - 1, GREEN);

    /* Text */
    ST7796_WriteString(10, ST7796_HEIGHT - 30, "ST7796 OK", Font_7x10, WHITE, BLACK);
}

