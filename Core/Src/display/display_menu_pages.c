#include <stdio.h>
#include <string.h>

#include "display/display_compose_helpers.h"
#include "display/display_layout.h"
#include "display/display_menu_page_bank_edit.h"
#include "display/display_menu_page_banks.h"
#include "display/display_menu_page_device_edit.h"
#include "display/display_menu_page_devices.h"
#include "display/display_menu_page_function_button.h"
#include "display/display_menu_page_global.h"
#include "display/display_menu_page_metronome.h"
#include "display/display_menu_page_midi_monitor.h"
#include "display/display_menu_page_user_theme.h"
#include "display/display_menu_pages.h"
#include "display/display_menu_redraw_utils.h"
#include "display/display_menu_row_render.h"
#include "display/display_strings.h"
#include "display/display_theme.h"

/* Menu page registry and shared menu chrome.
 *
 * Each menu page contributes a selection pointer, item count, body renderer,
 * and row renderer. This file is the switchboard that maps menu_page to those
 * functions and also owns shared header/footbar text used by multiple pages. */

typedef struct
{
    uint8_t *selection;
    uint8_t item_count;
    void (*draw_body)(void);
    void (*draw_item)(uint8_t item_index);
} DisplayMenuPageSpec_t;

static const char *Display_GetMidiMonitorFootbarLabel(uint8_t section_index)
{
    switch (section_index)
    {
    case 0U:
        return Display_MenuMidiMonitorIsPaused() ? "SCROLL / READ" : "SCROLL / STOP";
    case 1U:
        return "CLEAR";
    case 2U:
        return "EXIT";
    default:
        return "";
    }
}

static const char *Display_GetConfirmFootbarLabel(uint8_t section_index)
{
    switch (section_index)
    {
    case 0U:
        return MAIN_FOOTBAR_CONFIRM_LEFT_TEXT;
    case 1U:
        return MAIN_FOOTBAR_CONFIRM_CENTER_TEXT;
    case 2U:
        return MAIN_FOOTBAR_CONFIRM_RIGHT_TEXT;
    default:
        return "";
    }
}

static const char *Display_GetUserThemeEditFootbarLabel(uint8_t section_index)
{
    switch (section_index)
    {
    case 0U:
        return MAIN_FOOTBAR_MENU_USER_THEME_EDIT_LEFT_TEXT;
    case 1U:
        return MAIN_FOOTBAR_MENU_USER_THEME_EDIT_CENTER_TEXT;
    case 2U:
        return MAIN_FOOTBAR_MENU_USER_THEME_EDIT_RIGHT_TEXT;
    default:
        return "";
    }
}

static const char *Display_GetFootbarLabel(uint8_t section_index)
{
    if (display_state.menu_mode_active && !display_state.menu_preview_active)
    {
        if ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_MIDI_MONITOR)
            return Display_GetMidiMonitorFootbarLabel(section_index);

        if ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_USER_THEME
         && display_state.menu_user_theme_edit_active)
            return Display_GetUserThemeEditFootbarLabel(section_index);

        /* Menu footers take precedence over preset-edit/live-mode copy because
         * the soft-button hints should always describe the currently modal UI. */
        if (Display_MenuPageUsesConfirmFootbar((DisplayMenuPage_t)display_state.menu_page))
            return Display_GetConfirmFootbarLabel(section_index);

        switch (section_index)
        {
        case 0U:
            return MAIN_FOOTBAR_MENU_LEFT_TEXT;
        case 1U:
            if ((DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_USER_THEME)
                return MAIN_FOOTBAR_MENU_PREVIEW_CENTER_TEXT;
            return MAIN_FOOTBAR_MENU_CENTER_TEXT;
        case 2U:
            return MAIN_FOOTBAR_MENU_RIGHT_TEXT;
        default:
            return "";
        }
    }

    if (display_state.preset_edit_mode_active)
    {
        if (display_state.preset_init_confirm_active)
            return Display_GetConfirmFootbarLabel(section_index);

        switch (section_index)
        {
        case 0U:
            return MAIN_FOOTBAR_EDIT_LEFT_TEXT;
        case 1U:
            return MAIN_FOOTBAR_EDIT_CENTER_TEXT;
        case 2U:
            return MAIN_FOOTBAR_EDIT_RIGHT_TEXT;
        default:
            return "";
        }
    }

    switch (section_index)
    {
    case 0U:
        return MAIN_FOOTBAR_LEFT_TEXT;
    case 1U:
        return MAIN_FOOTBAR_CENTER_TEXT;
    case 2U:
        return MAIN_FOOTBAR_RIGHT_TEXT;
    default:
        return "";
    }
}

void Display_DrawFootbar(void)
{
    uint8_t midi_monitor_chrome_active = (display_state.menu_mode_active
                                       && !display_state.menu_preview_active
                                       && (DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_MIDI_MONITOR) ? 1U : 0U;
    uint16_t footbar_background = midi_monitor_chrome_active ? BLACK : MAIN_FOOTBAR_COLOR;
    uint16_t footbar_text_colour = midi_monitor_chrome_active ? WHITE : MAIN_FOOTBAR_TEXT_COLOUR;

    Display_ComposeClear(ST7796_WIDTH,
                         MAIN_FOOTBAR_H,
                         footbar_background);

    for (uint8_t section_index = 0U; section_index < MAIN_FOOTBAR_SECTION_COUNT; ++section_index)
    {
        const char *text = Display_GetFootbarLabel(section_index);
        size_t text_len = strlen(text);
        uint16_t section_x = (uint16_t)(section_index * MAIN_FOOTBAR_SECTION_WIDTH);
        uint16_t text_w = (uint16_t)text_len * MAIN_FOOTBAR_FONT.width;
        uint16_t text_x = (uint16_t)(section_x + ((MAIN_FOOTBAR_SECTION_WIDTH - text_w) / 2U));
        uint16_t text_y = (uint16_t)((MAIN_FOOTBAR_H - MAIN_FOOTBAR_FONT.height) / 2U);

        if (text_len == 0U)
            continue;

        Display_ComposeString32Literal(ST7796_WIDTH,
                           MAIN_FOOTBAR_H,
                           text_x,
                           text_y,
                           text,
                           MAIN_FOOTBAR_FONT,
                           footbar_text_colour,
                           footbar_background);
    }

    Display_ComposeBlit(0U,
                        MAIN_FOOTBAR_Y,
                        ST7796_WIDTH,
                        MAIN_FOOTBAR_H);
}

static void Display_WriteCenteredPaddedText32WithBackground(uint16_t y,
                                                            const char *text,
                                                            uint8_t width_chars,
                                                            FontDef32 font,
                                                            uint16_t colour,
                                                            uint16_t background)
{
    char padded[MAIN_PRESET_TEXT_CHARS + 1U];
    uint16_t draw_w = (uint16_t)width_chars * font.width;
    uint16_t draw_x = (uint16_t)((ST7796_WIDTH - draw_w) / 2U);
    size_t max_chars = (size_t)width_chars;
    size_t text_len;
    size_t pad_left;

    if (!text || width_chars == 0U || width_chars > MAIN_PRESET_TEXT_CHARS)
        return;

    text_len = strnlen(text, max_chars);
    pad_left = (max_chars - text_len) / 2U;

    /* Fill the full target width with spaces first so centered prompts stay
     * visually stable even when the visible text length changes. */
    memset(padded, ' ', max_chars);
    memcpy(padded + pad_left, text, text_len);
    padded[max_chars] = '\0';

    Display_ComposeClear(draw_w,
                         font.height,
                         background);
    Display_ComposeString32(draw_w,
                            font.height,
                            0U,
                            0U,
                            padded,
                            font,
                            colour,
                            background);
    Display_ComposeBlit(draw_x,
                        y,
                        draw_w,
                        font.height);
}

static void Display_DrawMenuConfirmPrompt(const char *text)
{
    Display_WriteCenteredPaddedText32WithBackground(MAIN_BANK_TEXT_Y,
                                                    text,
                                                    MAIN_BANK_TEXT_CHARS,
                                                    MAIN_BANK_FONT,
                                                    RED,
                                                    DISPLAY_BG_COLOUR);
}

void Display_DrawPresetInitConfirmPrompt(void)
{
    Display_WriteCenteredPaddedText32WithBackground(MAIN_BANK_TEXT_Y,
                                                    MAIN_INFO_PRESET_INIT_CONFIRM_TEXT,
                                                    MAIN_BANK_TEXT_CHARS,
                                                    MAIN_BANK_FONT,
                                                    RED,
                                                    DISPLAY_BG_COLOUR);
}

static void Display_DrawMenuRootItem(uint8_t item_index)
{
    static const char * const menu_root_items[MENU_ROOT_ITEM_COUNT] = {
        "Banks",
        "Devices",
        "Global",
        "Metronome",
        "MIDI Monitor",
    };
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MENU_ROOT_ITEM_COUNT,
                                                                   display_state.menu_root_selection_index);
    uint8_t row_index;

    if (item_index >= MENU_ROOT_ITEM_COUNT)
        return;

    if (item_index < first_visible_index || item_index >= (uint8_t)(first_visible_index + MENU_VISIBLE_ROW_COUNT))
        return;

    row_index = (uint8_t)(item_index - first_visible_index);

    Display_DrawMenuRowByIndex(row_index,
                               menu_root_items[item_index],
                               "",
                               (item_index == display_state.menu_root_selection_index) ? 1U : 0U);
}

static void Display_DrawMenuRoot(void)
{
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MENU_ROOT_ITEM_COUNT,
                                                                   display_state.menu_root_selection_index);

    for (uint8_t row_index = 0U; row_index < MENU_VISIBLE_ROW_COUNT; ++row_index)
    {
        uint8_t item_index = (uint8_t)(first_visible_index + row_index);

        if (item_index < MENU_ROOT_ITEM_COUNT)
            Display_DrawMenuRootItem(item_index);
        else
            Display_ClearStandardMenuRow(row_index);
    }
}

static void Display_DrawMenuBankInitConfirm(void)
{
    char confirm_text[24];

    (void)snprintf(confirm_text,
                   sizeof(confirm_text),
                   MENU_BANK_INIT_CONFIRM_FORMAT,
                   (uint8_t)(display_state.menu_active_bank_index + 1U));
    Display_DrawMenuConfirmPrompt(confirm_text);
}

static void Display_DrawMenuDeviceInitConfirm(void)
{
    char confirm_text[20];

    (void)snprintf(confirm_text,
                   sizeof(confirm_text),
                   MENU_DEVICE_INIT_CONFIRM_FORMAT,
                   (uint8_t)(display_state.menu_active_device_index + 1U));
    Display_DrawMenuConfirmPrompt(confirm_text);
}

static void Display_DrawMenuFactoryResetConfirm(void)
{
    Display_DrawMenuConfirmPrompt(MENU_FACTORY_RESET_CONFIRM_TEXT);
}

static const char *Display_GetMenuHeaderTextForPage(DisplayMenuPage_t page, char *buffer, size_t buffer_size)
{
    switch (page)
    {
    case DISPLAY_MENU_PAGE_BANKS:
        return "BANKS";
    case DISPLAY_MENU_PAGE_BANK_EDIT:
        (void)snprintf(buffer, buffer_size, "BANK %u", (uint8_t)(display_state.menu_active_bank_index + 1U));
        return buffer;
    case DISPLAY_MENU_PAGE_BANK_INIT_CONFIRM:
        return "CONFIRM";
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES:
        return "FUNC BTN";
    case DISPLAY_MENU_PAGE_DEVICES:
        return "DEVICES";
    case DISPLAY_MENU_PAGE_DEVICE_EDIT:
        (void)snprintf(buffer, buffer_size, "DEVICE %u", (uint8_t)(display_state.menu_active_device_index + 1U));
        return buffer;
    case DISPLAY_MENU_PAGE_DEVICE_INIT_CONFIRM:
    case DISPLAY_MENU_PAGE_FACTORY_RESET_CONFIRM:
        return "CONFIRM";
    case DISPLAY_MENU_PAGE_GLOBAL:
        return "GLOBAL";
    case DISPLAY_MENU_PAGE_METRONOME:
        return "METRONOME";
    case DISPLAY_MENU_PAGE_MIDI_MONITOR:
        return "MIDI MONITOR";
    case DISPLAY_MENU_PAGE_USER_THEME:
        return Display_GetThemeName((RuntimeConfigDisplayMode_t)display_state.menu_active_user_theme_mode);
    case DISPLAY_MENU_PAGE_ROOT:
    default:
        return "MENU";
    }
}

uint8_t Display_MenuPageUsesConfirmFootbar(DisplayMenuPage_t page)
{
    return (page == DISPLAY_MENU_PAGE_BANK_INIT_CONFIRM
         || page == DISPLAY_MENU_PAGE_DEVICE_INIT_CONFIRM
         || page == DISPLAY_MENU_PAGE_FACTORY_RESET_CONFIRM) ? 1U : 0U;
}

uint8_t Display_MenuPageUsesFreeformBody(DisplayMenuPage_t page)
{
    return (page == DISPLAY_MENU_PAGE_BANK_INIT_CONFIRM
         || page == DISPLAY_MENU_PAGE_DEVICE_INIT_CONFIRM
            || page == DISPLAY_MENU_PAGE_FACTORY_RESET_CONFIRM
            || page == DISPLAY_MENU_PAGE_MIDI_MONITOR) ? 1U : 0U;
}

uint8_t Display_MenuHeaderChanged(DisplayMenuPage_t previous_page, DisplayMenuPage_t current_page)
{
        char previous_text[16];
        char current_text[16];
    const char *previous_header = Display_GetMenuHeaderTextForPage(previous_page, previous_text, sizeof(previous_text));
    const char *current_header = Display_GetMenuHeaderTextForPage(current_page, current_text, sizeof(current_text));

    return (strcmp(previous_header, current_header) != 0) ? 1U : 0U;
}

static uint8_t Display_GetModeHeaderWidthChars(const char *text)
{
    size_t text_len = strlen(text);

    if ((display_state.menu_mode_active && !display_state.menu_preview_active)
     || display_state.preset_init_confirm_active)
    {
        uint8_t width_chars = (uint8_t)(text_len + MAIN_MODE_HEADER_MENU_PAD_CHARS);

        if (width_chars > MAIN_MODE_HEADER_MAX_TEXT_CHARS)
            width_chars = MAIN_MODE_HEADER_MAX_TEXT_CHARS;

        return width_chars;
    }

    return display_state.preset_edit_mode_active ? MAIN_MODE_HEADER_EDIT_TEXT_CHARS : MAIN_MODE_HEADER_TEXT_CHARS;
}

static const char *Display_GetCurrentHeaderText(void)
{
    static char menu_header_text[16];

    if (display_state.menu_mode_active && !display_state.menu_preview_active)
        return Display_GetMenuHeaderTextForPage((DisplayMenuPage_t)display_state.menu_page,
                                                menu_header_text,
                                                sizeof(menu_header_text));

    if (display_state.preset_init_confirm_active)
        return "CONFIRM";

    return display_state.preset_edit_mode_active ? "EDIT" : "LIVE";
}

void Display_DrawMainModeHeader(void)
{
    const char *header_text = Display_GetCurrentHeaderText();
    uint8_t midi_monitor_chrome_active = (display_state.menu_mode_active
                                       && !display_state.menu_preview_active
                                       && (DisplayMenuPage_t)display_state.menu_page == DISPLAY_MENU_PAGE_MIDI_MONITOR) ? 1U : 0U;
    uint8_t header_width_chars = Display_GetModeHeaderWidthChars(header_text);
    uint16_t clear_w = (uint16_t)(MAIN_MODE_HEADER_MAX_TEXT_CHARS * MAIN_MODE_HEADER_FONT.width);
    uint16_t clear_x = (uint16_t)((ST7796_WIDTH - clear_w) / 2U);
    uint16_t text_w = (uint16_t)(header_width_chars * MAIN_MODE_HEADER_FONT.width);
    uint16_t text_x = (uint16_t)((clear_w - text_w) / 2U);
    uint16_t foreground = midi_monitor_chrome_active
        ? WHITE
        : ((display_state.preset_edit_mode_active
         || (display_state.menu_mode_active && !display_state.menu_preview_active))
        ? MAIN_MODE_HEADER_EDIT_COLOUR
        : MAIN_MODE_HEADER_COLOUR);
    uint16_t background = midi_monitor_chrome_active
        ? BLACK
        : ((display_state.preset_edit_mode_active
         || (display_state.menu_mode_active && !display_state.menu_preview_active))
        ? MAIN_MODE_HEADER_EDIT_BG_COLOUR
        : DISPLAY_BG_COLOUR);
    uint16_t clear_background = midi_monitor_chrome_active ? BLACK : DISPLAY_BG_COLOUR;
    char padded[MAIN_MODE_HEADER_MAX_TEXT_CHARS + 1U];
    size_t text_len = strnlen(header_text, header_width_chars);
    size_t pad_left = ((size_t)header_width_chars - text_len) / 2U;

    memset(padded, ' ', header_width_chars);
    memcpy(padded + pad_left, header_text, text_len);
    padded[header_width_chars] = '\0';

    Display_ComposeClear(clear_w,
                         MAIN_MODE_HEADER_FONT.height,
                         clear_background);
    Display_ComposeString32(clear_w,
                            MAIN_MODE_HEADER_FONT.height,
                            text_x,
                            0U,
                            padded,
                            MAIN_MODE_HEADER_FONT,
                            foreground,
                            background);
    Display_ComposeBlit(clear_x,
                        MAIN_MODE_HEADER_TEXT_Y,
                        clear_w,
                        MAIN_MODE_HEADER_FONT.height);
}

void Display_ClearMenuBody(void)
{
    uint16_t clear_y = MAIN_PRESET_TEXT_Y;

    for (uint8_t row_index = 0U; row_index < MENU_VISIBLE_ROW_COUNT; ++row_index)
    {
        uint16_t row_y = Display_GetMenuRowYByIndex(row_index);

        if (row_y > clear_y)
        {
            ST7796_DrawFilledRectangle(0U,
                                       clear_y,
                                       ST7796_WIDTH,
                                       (uint16_t)(row_y - clear_y),
                                       Display_GetBackgroundColour());
        }

        Display_ClearStandardMenuRow(row_index);
        clear_y = (uint16_t)(row_y + MAIN_INFO_FONT.height);
    }

    if (clear_y < MAIN_FOOTBAR_Y)
    {
        ST7796_DrawFilledRectangle(0U,
                                   clear_y,
                                   ST7796_WIDTH,
                                   (uint16_t)(MAIN_FOOTBAR_Y - clear_y),
                                   Display_GetBackgroundColour());
    }
}

static DisplayMenuPageSpec_t Display_GetMenuPageSpec(DisplayMenuPage_t page)
{
    switch (page)
    {
    case DISPLAY_MENU_PAGE_ROOT:
        return (DisplayMenuPageSpec_t){ &display_state.menu_root_selection_index, MENU_ROOT_ITEM_COUNT, Display_DrawMenuRoot, Display_DrawMenuRootItem };
    case DISPLAY_MENU_PAGE_BANKS:
        return (DisplayMenuPageSpec_t){ &display_state.menu_bank_selection_index, PRESET_BANK_COUNT, Display_DrawMenuBanks, Display_DrawMenuBankItem };
    case DISPLAY_MENU_PAGE_BANK_EDIT:
        return (DisplayMenuPageSpec_t){ &display_state.menu_bank_edit_selection_index, MENU_BANK_EDIT_ITEM_COUNT, Display_DrawMenuBankEdit, Display_DrawMenuBankEditItem };
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES:
    case DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES:
        return (DisplayMenuPageSpec_t){ &display_state.menu_function_button_selection_index, MENU_FUNCTION_BUTTON_ITEM_COUNT, Display_DrawMenuFunctionButton, Display_DrawMenuFunctionButtonItem };
    case DISPLAY_MENU_PAGE_DEVICES:
        return (DisplayMenuPageSpec_t){ &display_state.menu_device_selection_index, MIDI_DEVICE_COUNT, Display_DrawMenuDevices, Display_DrawMenuDeviceItem };
    case DISPLAY_MENU_PAGE_DEVICE_EDIT:
        return (DisplayMenuPageSpec_t){ &display_state.menu_device_edit_selection_index, MENU_DEVICE_EDIT_ITEM_COUNT, Display_DrawMenuDeviceEdit, Display_DrawMenuDeviceEditItem };
    case DISPLAY_MENU_PAGE_DEVICE_INIT_CONFIRM:
        return (DisplayMenuPageSpec_t){ NULL, 0U, Display_DrawMenuDeviceInitConfirm, NULL };
    case DISPLAY_MENU_PAGE_BANK_INIT_CONFIRM:
        return (DisplayMenuPageSpec_t){ NULL, 0U, Display_DrawMenuBankInitConfirm, NULL };
    case DISPLAY_MENU_PAGE_FACTORY_RESET_CONFIRM:
        return (DisplayMenuPageSpec_t){ NULL, 0U, Display_DrawMenuFactoryResetConfirm, NULL };
    case DISPLAY_MENU_PAGE_GLOBAL:
        return (DisplayMenuPageSpec_t){ &display_state.menu_global_selection_index, MENU_GLOBAL_ITEM_COUNT, Display_DrawMenuGlobal, Display_DrawMenuGlobalItem };
    case DISPLAY_MENU_PAGE_METRONOME:
        return (DisplayMenuPageSpec_t){ &display_state.menu_metronome_selection_index, MENU_METRONOME_ITEM_COUNT, Display_DrawMenuMetronome, Display_DrawMenuMetronomeItem };
    case DISPLAY_MENU_PAGE_MIDI_MONITOR:
        return (DisplayMenuPageSpec_t){ NULL, 0U, Display_DrawMenuMidiMonitor, NULL };
    case DISPLAY_MENU_PAGE_USER_THEME:
        return (DisplayMenuPageSpec_t){ &display_state.menu_user_theme_selection_index, MENU_USER_THEME_ITEM_COUNT, Display_DrawMenuUserTheme, Display_DrawMenuUserThemeItem };
    default:
        return (DisplayMenuPageSpec_t){ NULL, 0U, NULL, NULL };
    }
}

void Display_DrawCurrentMenuPageBody(void)
{
    DisplayMenuPageSpec_t page_spec = Display_GetMenuPageSpec((DisplayMenuPage_t)display_state.menu_page);

    if (page_spec.draw_body)
        page_spec.draw_body();
    else
        Display_DrawMenuRoot();
}

void Display_DrawMenuPageItem(DisplayMenuPage_t page, uint8_t item_index)
{
    DisplayMenuPageSpec_t page_spec = Display_GetMenuPageSpec(page);

    if (page_spec.draw_item)
        page_spec.draw_item(item_index);
}

uint8_t *Display_GetMenuPageSelectionPointer(DisplayMenuPage_t page)
{
    return Display_GetMenuPageSpec(page).selection;
}

uint8_t Display_GetMenuPageItemCount(DisplayMenuPage_t page)
{
    return Display_GetMenuPageSpec(page).item_count;
}