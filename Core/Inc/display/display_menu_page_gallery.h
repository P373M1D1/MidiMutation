#ifndef DISPLAY_MENU_PAGE_GALLERY_H
#define DISPLAY_MENU_PAGE_GALLERY_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Draws the gallery page body.
 *
 * When the gallery contains at least one compiled image the function fills the
 * entire 480×320 display with the image at the current gallery index and
 * overlays a small "N / total" counter in the bottom-right corner so the user
 * knows how many images are available.
 *
 * When no images have been compiled in yet, it draws a centred "No Images"
 * prompt using the standard menu-body background so the page still looks
 * intentional rather than blank.
 *
 * This function is the draw_body callback registered in the page-spec table
 * inside display_menu_pages.c; it should only be called through that table
 * or from Display_MenuRefresh so that layout state stays consistent.
 */
void Display_DrawMenuGallery(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MENU_PAGE_GALLERY_H */
