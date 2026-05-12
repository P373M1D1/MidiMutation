#ifndef DISPLAY_MENU_PAGE_BANK_EDIT_H
#define DISPLAY_MENU_PAGE_BANK_EDIT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Internal BANK_EDIT page helpers shared by redraw and page dispatch. */
void Display_FormatBankEditValue(uint8_t item_index, char *buffer, size_t buffer_size);
void Display_DrawMenuBankEdit(void);
void Display_DrawMenuBankEditItem(uint8_t item_index);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MENU_PAGE_BANK_EDIT_H */