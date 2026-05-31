#include <stdio.h>

#include "display_functions.h"
#include "display/display_internal.h"
#include "display/display_menu_page_banks.h"
#include "display/display_menu_redraw_utils.h"
#include "display/display_menu_row_render.h"
#include "runtime_config.h"

/* BANKS list-page renderer.
 *
 * This file formats the visible bank list rows and nothing more. Selection and
 * scrolling rules live elsewhere; this module just turns a bank index into the
 * label/value pair shown in the shared four-row menu window. */

#define MENU_BANK_LABEL_PREFIX "Bank "

void Display_FormatMenuBankLabel(uint8_t bank_index, char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0U)
        return;

    (void)snprintf(buffer, buffer_size, MENU_BANK_LABEL_PREFIX "%u", (uint8_t)(bank_index + 1U));
}

void Display_DrawMenuBankWindowRow(uint8_t row_index, uint8_t bank_index)
{
    char label_text[12];
    const RuntimeConfigBank_t *bank;

    if (row_index >= MENU_VISIBLE_ROW_COUNT)
        return;

    if (bank_index >= PRESET_BANK_COUNT)
    {
        /* When the final BANKS window has fewer than four entries, scrub the
         * leftover row so previous page content cannot leak through. */
        Display_ClearStandardMenuRow(row_index);
        return;
    }

    bank = RuntimeConfig_GetBank(bank_index);
    Display_FormatMenuBankLabel(bank_index, label_text, sizeof(label_text));
    Display_DrawMenuRowByIndex(row_index,
                               label_text,
                               bank ? bank->name : "",
                               (bank_index == display_state.menu_bank_selection_index) ? 1U : 0U);
}

void Display_DrawMenuBankItem(uint8_t bank_index)
{
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(PRESET_BANK_COUNT,
                                                                   display_state.menu_bank_selection_index);
    uint8_t row_index;
    char label_text[12];
    const RuntimeConfigBank_t *bank;

    if (bank_index < first_visible_index || bank_index >= (uint8_t)(first_visible_index + MENU_VISIBLE_ROW_COUNT))
        return;

    /* Single-item redraws reuse the same window math as full-page draws so the
     * row index stays stable when redraw code repaints just one changed bank. */
    row_index = (uint8_t)(bank_index - first_visible_index);
    bank = RuntimeConfig_GetBank(bank_index);
    Display_FormatMenuBankLabel(bank_index, label_text, sizeof(label_text));
    Display_DrawMenuRowByIndex(row_index,
                               label_text,
                               bank ? bank->name : "",
                               (bank_index == display_state.menu_bank_selection_index) ? 1U : 0U);
}

void Display_DrawMenuBanks(void)
{
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(PRESET_BANK_COUNT,
                                                                   display_state.menu_bank_selection_index);

    for (uint8_t row_index = 0U; row_index < MENU_VISIBLE_ROW_COUNT; ++row_index)
    {
        uint8_t bank_index = (uint8_t)(first_visible_index + row_index);

        if (bank_index >= PRESET_BANK_COUNT)
        {
            Display_ClearStandardMenuRow(row_index);
            continue;
        }

        Display_DrawMenuBankItem(bank_index);
    }
}
