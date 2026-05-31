#include <stdio.h>
#include <string.h>

#include "display/display_internal.h"
#include "display/display_menu_page_gallery.h"
#include "display/display_theme.h"
#include "gallery_images.h"
#include "st7796.h"
#include "st7796_rgb565_colors.h"

#define TFT_W 480U
#define TFT_H 320U

/* Gallery page renderer.
 *
 * This module has exactly one job: display the compiled gallery image that
 * display_state.menu_gallery_index points to, centered on the 480×320 screen.
 * All navigation logic (index clamping, ENC2 scroll, back-navigation) lives in
 * display_menu_controller.c so the renderer stays stateless and swappable.
 *
 * When no images are compiled in, a "No Images" prompt is drawn instead so the
 * page always shows something intentional. */

/** Pixel dimensions of each gallery slot (must match the gen script). */
#define GALLERY_W GALLERY_IMAGE_WIDTH
#define GALLERY_H GALLERY_IMAGE_HEIGHT

/** Centre position for reduced-size gallery assets. */
#define GALLERY_X ((uint16_t)((TFT_W - GALLERY_W) / 2U))
#define GALLERY_Y ((uint16_t)((TFT_H - GALLERY_H) / 2U))

/**
 * Draws a centred "No Images" message on the full screen.
 *
 * Called only when gallery_image_count == 0.  Uses a plain black background
 * with white text so it reads clearly regardless of the current theme.
 *
 * If you add images and the message still appears, regenerate gallery_images.c
 * with tools/gen_gallery.py and rebuild.
 */
static void Display_DrawGalleryEmpty(void)
{
    const FontDef32 *font = Display_GetThemeInfoFont();
    const char *line1 = "No Images";
    const char *line2 = "Drop files into gallery/";
    const char *line3 = "and run gen_gallery.py";
    uint16_t line1_w = (uint16_t)(strlen(line1) * font->width);
    uint16_t line2_w = (uint16_t)(strlen(line2) * font->width);
    uint16_t line3_w = (uint16_t)(strlen(line3) * font->width);
    uint16_t line_h  = font->height;

    ST7796_DrawFilledRectangle(0U, 0U, TFT_W, TFT_H, BLACK);

    ST7796_WriteString32((uint16_t)((TFT_W - line1_w) / 2U),
                         (uint16_t)((TFT_H / 2U) - line_h - (line_h / 2U)),
                         line1,
                         *font,
                         WHITE,
                         BLACK);

    ST7796_WriteString32((uint16_t)((TFT_W - line2_w) / 2U),
                         (uint16_t)(TFT_H / 2U),
                         line2,
                         *font,
                         0x7BEFu, /* mid-grey */
                         BLACK);

    ST7796_WriteString32((uint16_t)((TFT_W - line3_w) / 2U),
                         (uint16_t)((TFT_H / 2U) + line_h + (line_h / 2U)),
                         line3,
                         *font,
                         0x7BEFu,
                         BLACK);
}

/**
 * Overlays a small "N / total" counter badge in the bottom-right corner.
 *
 * Drawn after the image so it is always readable.  The badge uses a
 * semi-opaque black rectangle behind the text — implemented as a solid
 * black fill since blending is not available on this driver.
 */
static void Display_DrawGalleryCounter(uint8_t index, uint8_t total)
{
    char buf[12];
    const FontDef32 *font = Display_GetThemeInfoFont();
    uint16_t text_h = font->height;

    (void)snprintf(buf, sizeof(buf), "%u / %u", (unsigned)(index + 1U), (unsigned)total);

    uint16_t text_w = (uint16_t)(strlen(buf) * font->width);
    uint16_t pad    = 6U;
    uint16_t badge_w = (uint16_t)(text_w + 2U * pad);
    uint16_t badge_h = (uint16_t)(text_h + 2U * pad);
    uint16_t badge_x = (uint16_t)(TFT_W - badge_w - 4U);
    uint16_t badge_y = (uint16_t)(TFT_H - badge_h - 4U);

    ST7796_DrawFilledRectangle(badge_x, badge_y, badge_w, badge_h, BLACK);
    ST7796_WriteString32((uint16_t)(badge_x + pad),
                         (uint16_t)(badge_y + pad),
                         buf,
                         *font,
                         WHITE,
                         BLACK);
}

/**
 * Gallery body renderer.
 *
 * Called by the page-spec dispatch table when the gallery page needs to be
 * drawn from scratch.  Does not modify display_state; the caller is responsible
 * for setting menu_gallery_index to a valid value before calling this.
 */
void Display_DrawMenuGallery(void)
{
    if (gallery_image_count == 0U)
    {
        Display_DrawGalleryEmpty();
        return;
    }

    uint8_t idx = display_state.menu_gallery_index;

    if (idx >= gallery_image_count)
        idx = 0U;

    ST7796_DrawFilledRectangle(0U, 0U, TFT_W, TFT_H, BLACK);
    ST7796_DrawImage(GALLERY_X, GALLERY_Y, GALLERY_W, GALLERY_H, gallery_images[idx].data);
    Display_DrawGalleryCounter(idx, gallery_image_count);
}
