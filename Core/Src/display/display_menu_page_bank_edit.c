#include <stdio.h>

#include "display_functions.h"
#include "display/display_internal.h"
#include "display/display_menu_page_bank_edit.h"
#include "display/display_menu_redraw_utils.h"
#include "display/display_menu_row_render.h"
#include "display/display_theme.h"
#include "runtime_config.h"
#include "st7796.h"

/* BANK_EDIT page renderer.
 *
 * This page mixes plain text rows and a bank reset action. Keeping that
 * formatting here avoids leaking bank-specific labels and wording into the
 * generic menu shell. */

#define MENU_BANK_INIT_TEXT "INIT BANK"

void Display_FormatBankEditValue(uint8_t item_index, char *buffer, size_t buffer_size)
{
    const RuntimeConfigBank_t *bank = RuntimeConfig_GetBank(display_state.menu_active_bank_index);

    if (!buffer || buffer_size == 0U || !bank)
        return;

    switch (item_index)
    {
    case 0U:
        (void)snprintf(buffer, buffer_size, "%s", bank->name);
        break;
    case 1U:
        (void)snprintf(buffer, buffer_size, "%s", bank->wet_dry_enabled ? "Yes" : "No");
        break;
    case 2U:
        (void)snprintf(buffer, buffer_size, "%2u bars", bank->midi_clock_bar_count);
        break;
    case 3U:
        buffer[0] = '\0';
        break;
    default:
        buffer[0] = '\0';
        break;
    }
}

void Display_DrawMenuBankEdit(void)
{
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MENU_BANK_EDIT_ITEM_COUNT,
                                                                   display_state.menu_bank_edit_selection_index);

    for (uint8_t row_index = 0U; row_index < MENU_VISIBLE_ROW_COUNT; ++row_index)
    {
        uint8_t item_index = (uint8_t)(first_visible_index + row_index);

        if (item_index >= MENU_BANK_EDIT_ITEM_COUNT)
        {
            Display_ClearStandardMenuRow(row_index);
            continue;
        }

        Display_DrawMenuBankEditItem(item_index);
    }
}

void Display_DrawMenuBankEditItem(uint8_t item_index)
{
    static const char * const menu_bank_edit_labels[MENU_BANK_EDIT_ITEM_COUNT] = {
        "Bank Name",
        "Wet / Dry",
        "Counter",
        MENU_BANK_INIT_TEXT,
    };
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MENU_BANK_EDIT_ITEM_COUNT,
                                                                   display_state.menu_bank_edit_selection_index);
    uint8_t row_index;
    const RuntimeConfigBank_t *bank = RuntimeConfig_GetBank(display_state.menu_active_bank_index);
    char value_text[24];

    if (item_index >= MENU_BANK_EDIT_ITEM_COUNT)
        return;

    if (item_index < first_visible_index || item_index >= (uint8_t)(first_visible_index + MENU_VISIBLE_ROW_COUNT))
        return;

    row_index = (uint8_t)(item_index - first_visible_index);

    if (item_index == 0U && (DisplayMenuTextField_t)display_state.menu_text_edit_field == DISPLAY_MENU_TEXT_FIELD_BANK_NAME)
    {
        Display_DrawMenuTextEditRowByIndex(row_index,
                                           menu_bank_edit_labels[item_index],
                                           bank ? bank->name : "",
                                           RUNTIME_CONFIG_BANK_NAME_LENGTH);
        return;
    }

    if (item_index == 3U)
    {
        /* INIT BANK is drawn as a centered alert badge to visually separate it
         * from ordinary editable rows and reduce accidental activation. */
        Display_DrawMenuCenteredBadgeRowByIndex(row_index,
                                                menu_bank_edit_labels[item_index],
                                                MAIN_ALERT_BADGE_TEXT_COLOUR,
                                                RED);
        return;
    }

    Display_FormatBankEditValue(item_index, value_text, sizeof(value_text));
    Display_DrawMenuRowByIndex(row_index,
                               menu_bank_edit_labels[item_index],
                               value_text,
                               (item_index == display_state.menu_bank_edit_selection_index) ? 1U : 0U);
}
