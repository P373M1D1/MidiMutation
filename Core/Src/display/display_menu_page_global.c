#include <stdio.h>

#include "display_functions.h"
#include "display/display_internal.h"
#include "display/display_menu_page_global.h"
#include "display/display_menu_redraw_utils.h"
#include "display/display_menu_row_render.h"
#include "display/display_theme.h"
#include "display/display_value_helpers.h"
#include "runtime_config.h"
#include "st7796.h"

/* GLOBAL menu page renderer.
 *
 * This file formats the user-facing text for the global runtime settings but
 * does not change values itself; value adjustment lives in display_menu_value.c
 * so the formatting and mutation rules stay separated. */

static const char * const menu_global_labels[MENU_GLOBAL_ITEM_COUNT] = {
    "Startup Delay",
    "Screen Saver",
    "Sync Style",
    "Live ENC2",
    "Theme",
    "Brightness",
    "Feedback Taper",
    "Threshold",
    "Reduce",
    "Expression",
    "Factory Reset",
};

void Display_FormatGlobalMenuValue(uint8_t item_index, char *buffer, size_t buffer_size)
{
    const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();

    if (!buffer || buffer_size == 0U || !global)
        return;

    switch (item_index)
    {
    case 0U:
        (void)snprintf(buffer, buffer_size, "%u sec", global->startup_delay_seconds);
        break;
    case 1U:
        (void)snprintf(buffer, buffer_size, "%u min", global->screensaver_timeout_minutes);
        break;
    case 2U:
        (void)snprintf(buffer,
                       buffer_size,
                       "%s",
                       (global->sync_style == RUNTIME_CONFIG_SYNC_STYLE_TAP_TEMPO_CC) ? "Tap Tempo CC" : "MIDI clock");
        break;
    case 3U:
        switch (global->live_enc2_mode)
        {
        case RUNTIME_CONFIG_LIVE_ENC2_MODE_METRONOME:
            (void)snprintf(buffer, buffer_size, "%s", "Metronome");
            break;
        case RUNTIME_CONFIG_LIVE_ENC2_MODE_TIMEBEND:
            (void)snprintf(buffer, buffer_size, "%s", "Timebend");
            break;
        case RUNTIME_CONFIG_LIVE_ENC2_MODE_PRESET_BANK_SCROLL:
        default:
            (void)snprintf(buffer, buffer_size, "%s", "Preset/Bank");
            break;
        }
        break;
    case 4U:
        /* Theme names come from the registration table in display_theme.c, so
         * adding a theme there automatically updates the menu label here. */
        (void)snprintf(buffer,
                       buffer_size,
                       "%s",
                       Display_GetThemeName(global->display_mode));
        break;
    case 5U:
        (void)snprintf(buffer,
                       buffer_size,
                       "%u",
                       Display_GetGlobalBrightnessUiValue(global->backlight_brightness));
        break;
    case 6U:
        (void)snprintf(buffer,
                       buffer_size,
                       "%s",
                       global->feedback_taper_enabled ? "Enabled" : "Disabled");
        break;
    case 7U:
        (void)snprintf(buffer, buffer_size, "%u", global->feedback_taper_threshold);
        break;
    case 8U:
        (void)snprintf(buffer, buffer_size, "%u", global->feedback_taper_reduce);
        break;
    case 9U:
        (void)snprintf(buffer, buffer_size, "%s", "Open");
        break;
    case 10U:
        buffer[0] = '\0';
        break;
    default:
        buffer[0] = '\0';
        break;
    }
}

void Display_DrawMenuGlobal(void)
{
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MENU_GLOBAL_ITEM_COUNT,
                                                                   display_state.menu_global_selection_index);

    for (uint8_t row_index = 0U; row_index < MENU_VISIBLE_ROW_COUNT; ++row_index)
    {
        uint8_t item_index = (uint8_t)(first_visible_index + row_index);

        if (item_index < MENU_GLOBAL_ITEM_COUNT)
            Display_DrawMenuGlobalItem(item_index);
        else
            Display_ClearStandardMenuRow(row_index);
    }
}

void Display_DrawMenuGlobalItem(uint8_t item_index)
{
    uint8_t first_visible_index = Display_GetMenuFirstVisibleIndex(MENU_GLOBAL_ITEM_COUNT,
                                                                   display_state.menu_global_selection_index);
    uint8_t row_index;
    char value_text[20];

    if (item_index < first_visible_index || item_index >= (uint8_t)(first_visible_index + MENU_VISIBLE_ROW_COUNT))
        return;

    row_index = (uint8_t)(item_index - first_visible_index);

    if (item_index == (MENU_GLOBAL_ITEM_COUNT - 1U))
    {
        /* Factory reset is intentionally rendered as a badge instead of a value
         * row so it stands apart from ordinary editable global settings. */
        Display_DrawMenuCenteredBadgeRowByIndex(row_index,
                                                menu_global_labels[item_index],
                                                MAIN_ALERT_BADGE_TEXT_COLOUR,
                                                RED);
        return;
    }

    Display_FormatGlobalMenuValue(item_index, value_text, sizeof(value_text));
    Display_DrawMenuRowByIndex(row_index,
                               menu_global_labels[item_index],
                               value_text,
                               (item_index == display_state.menu_global_selection_index) ? 1U : 0U);
}
