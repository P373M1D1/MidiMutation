#ifndef DISPLAY_MENU_PAGE_METRONOME_H
#define DISPLAY_MENU_PAGE_METRONOME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Display_FormatMetronomeMenuValue(uint8_t item_index, char *buffer, size_t buffer_size);
void Display_DrawMenuMetronome(void);
void Display_DrawMenuMetronomeItem(uint8_t item_index);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MENU_PAGE_METRONOME_H */