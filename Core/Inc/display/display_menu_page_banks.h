#ifndef DISPLAY_MENU_PAGE_BANKS_H
#define DISPLAY_MENU_PAGE_BANKS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Internal BANKS page helpers shared by redraw and page dispatch. */
/* bank_index is always the logical bank number; windowing into the visible
 * four-row menu is handled separately by redraw helpers before row painters run. */
void Display_FormatMenuBankLabel(uint8_t bank_index, char *buffer, size_t buffer_size);
void Display_DrawMenuBanks(void);
void Display_DrawMenuBankItem(uint8_t bank_index);
void Display_DrawMenuBankWindowRow(uint8_t row_index, uint8_t bank_index);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MENU_PAGE_BANKS_H */