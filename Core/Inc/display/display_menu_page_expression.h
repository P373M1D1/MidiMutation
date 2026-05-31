#ifndef DISPLAY_MENU_PAGE_EXPRESSION_H
#define DISPLAY_MENU_PAGE_EXPRESSION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Display_DrawMenuExpression(void);
void Display_DrawMenuExpressionItem(uint8_t item_index);
void Display_FormatExpressionValue(uint8_t item_index, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MENU_PAGE_EXPRESSION_H */