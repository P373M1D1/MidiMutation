#ifndef DISPLAY_INTERNAL_H
#define DISPLAY_INTERNAL_H

/* Internal display-subsystem shared declarations.
 *
 * Scope:
 * - Used only by display implementation files during refactor phases.
 * - Not part of the stable application-facing API.
 * - Must not be included by non-display modules (main, presets, midi, etc.).
 *
 * This file intentionally starts minimal in Phase 1 and will be populated as
 * display_functions.c is decomposed into dedicated modules.
 */

#include <stddef.h>
#include <stdint.h>
#include "runtime_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Menu layout constants shared across display modules. */
#define MENU_ROOT_ITEM_COUNT            4U
#define MENU_VISIBLE_ROW_COUNT          4U
#define MENU_GLOBAL_ITEM_COUNT          6U
#define MENU_USER_THEME_ITEM_COUNT      RUNTIME_CONFIG_USER_THEME_FIELD_COUNT
#define MENU_BANK_EDIT_ITEM_COUNT       5U
#define MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT 3U
#define MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT
#define MENU_FUNCTION_BUTTON_MESSAGE_ROW_COUNT (RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT + RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT)
#define MENU_FUNCTION_BUTTON_CC_FIRST_INDEX (MENU_FUNCTION_BUTTON_MESSAGE_FIRST_INDEX + RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT)
#define MENU_FUNCTION_BUTTON_ITEM_COUNT (MENU_FUNCTION_BUTTON_TEXT_ITEM_COUNT + MENU_FUNCTION_BUTTON_MESSAGE_ROW_COUNT)
#define MENU_DEVICE_EDIT_ITEM_COUNT     8U

/* Shared mutable display state.
 *
 * This is still owned centrally by display_functions.c, but extracted display
 * modules read/write pieces of it through the extern below. If you add state,
 * keep it display-only and prefer grouping related flags together so redraw and
 * controller code stay readable. */
typedef struct DisplayState {
	uint8_t main_layout_dirty;
	uint8_t bpm_display_valid;
	uint8_t bpm_display_external;
	uint8_t bpm_display_sync_lost;
	uint16_t bpm_display_value_x10;
	uint32_t bpm_display_external_update_tick;
	char transport_barbeat_text[5];
	uint8_t main_info_first_slot;
	uint8_t preset_edit_mode_active;
	uint8_t preset_edit_cursor_index;
	uint8_t preset_name_edit_active;
	uint8_t preset_name_edit_cursor_index;
	uint8_t saving_popup_visible;
	uint8_t menu_mode_active;
	uint8_t preset_init_confirm_active;
	uint8_t menu_preview_active;
	uint8_t menu_root_selection_index;
	uint8_t menu_bank_selection_index;
	uint8_t menu_active_bank_index;
	uint8_t menu_bank_edit_selection_index;
	uint8_t menu_function_button_selection_index;
	uint8_t menu_device_selection_index;
	uint8_t menu_active_device_index;
	uint8_t menu_device_edit_selection_index;
	uint8_t menu_device_cc_field_index;
	uint8_t menu_device_cc_field_edit_active;
	uint8_t menu_global_selection_index;
	uint8_t menu_user_theme_selection_index;
	uint8_t menu_active_user_theme_mode;
	uint8_t menu_function_button_message_selection_index;
	uint8_t menu_function_button_message_field_index;
	uint8_t menu_function_button_message_field_edit_active;
	uint8_t menu_page;
	uint8_t menu_last_drawn_page;
	uint8_t menu_text_edit_field;
	uint8_t menu_text_edit_cursor_index;
	uint8_t menu_draw_state_valid;
} DisplayState;

/* Logical menu pages used by controller, renderer, and redraw modules.
 * The value itself is persisted nowhere, so these may be reordered if every
 * switch statement and page registry is updated together. */
typedef enum {
	DISPLAY_MENU_PAGE_ROOT = 0,
	DISPLAY_MENU_PAGE_BANKS,
	DISPLAY_MENU_PAGE_BANK_EDIT,
	DISPLAY_MENU_PAGE_BANK_INIT_CONFIRM,
	DISPLAY_MENU_PAGE_FUNCTION_BUTTON,
	DISPLAY_MENU_PAGE_FUNCTION_BUTTON_ACTIVE_MESSAGES,
	DISPLAY_MENU_PAGE_FUNCTION_BUTTON_INACTIVE_MESSAGES,
	DISPLAY_MENU_PAGE_DEVICES,
	DISPLAY_MENU_PAGE_DEVICE_EDIT,
	DISPLAY_MENU_PAGE_DEVICE_INIT_CONFIRM,
	DISPLAY_MENU_PAGE_FACTORY_RESET_CONFIRM,
	DISPLAY_MENU_PAGE_GLOBAL,
	DISPLAY_MENU_PAGE_MIDI_MONITOR,
	DISPLAY_MENU_PAGE_USER_THEME,
} DisplayMenuPage_t;

/* Editable text-field identities used by the menu text-edit helper path. */
typedef enum {
	DISPLAY_MENU_TEXT_FIELD_NONE = 0,
	DISPLAY_MENU_TEXT_FIELD_BANK_NAME,
	DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_NAME,
	DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_ACTIVE_LABEL,
	DISPLAY_MENU_TEXT_FIELD_FUNCTION_BUTTON_INACTIVE_LABEL,
	DISPLAY_MENU_TEXT_FIELD_DEVICE_NAME,
} DisplayMenuTextField_t;

extern DisplayState display_state;

/* Internal helper used by the screensaver module to suppress stale layout redraws. */
void Display_ClearMainLayoutDirty(void);
void Display_DrawFootbar(void);

/* Internal backlight helper used by the display controller to reapply the
 * current configured brightness after runtime setting changes. */
uint16_t Display_BacklightGetConfiguredBrightness(void);
uint16_t Display_GetBackgroundColour(void);

/* Internal menu text-edit helpers shared by menu controller and renderer. */
DisplayMenuTextField_t Display_GetMenuTextFieldForSelection(void);
uint8_t Display_GetMenuTextFieldLength(DisplayMenuTextField_t field);
size_t Display_GetMenuTextFieldCapacity(DisplayMenuTextField_t field);
/* Returns the live mutable string behind the current text field selection; the
 * menu editor writes through this pointer directly before marking config dirty. */
char *Display_GetMenuTextFieldPointer(DisplayMenuTextField_t field);
void Display_LoadMenuTextCells(const char *source, uint8_t cell_count, char *cells);
void Display_StoreMenuTextCells(char *destination,
			       size_t destination_size,
			       const char *cells,
			       uint8_t cell_count);
int16_t Display_FindMenuTextCharsetIndex(char ch);
void Display_MenuTextEditEnter(DisplayMenuTextField_t field);
uint8_t Display_MenuAdjustTextCharacter(int8_t delta);
uint8_t Display_MenuSubEditorIsActive(void);
uint8_t Display_MenuTextEditIsActive(void);
uint8_t Display_MenuTextEditMoveCursor(int8_t delta);

/* Internal redraw hooks shared by menu helper modules. */
void Display_MenuRedrawCurrentItem(void);
/* Current-value redraw is narrower than current-item redraw and should be used
 * when only the right-hand value or sub-editor state changed. */
void Display_MenuRedrawCurrentValue(void);
void Display_MenuRedrawSelectionChange(DisplayMenuPage_t page, uint8_t previous_selection);
void Display_MenuRedrawCurrentPageRows(void);
void Display_MenuRefreshBodyOnly(void);
void Display_RedrawMenuSelectionItem(DisplayMenuPage_t page, uint8_t item_index, uint8_t selected);
void Display_RedrawMenuCurrentValueItem(DisplayMenuPage_t page, uint8_t item_index);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_INTERNAL_H */
