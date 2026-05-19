#ifndef RUNTIME_CONFIG_H
#define RUNTIME_CONFIG_H

#include <stdint.h>
#include <stddef.h>
#include "presets.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RUNTIME_CONFIG_BANK_NAME_LENGTH               PRESET_BANK_NAME_MAXLEN
#define RUNTIME_CONFIG_FUNCTION_BUTTON_NAME_LENGTH    6U
#define RUNTIME_CONFIG_FUNCTION_BUTTON_LABEL_LENGTH   6U
#define RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT  4U
#define RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT       4U
#define RUNTIME_CONFIG_DEVICE_NAME_LENGTH             4U
#define RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_DEFAULT   4U
#define RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MIN       1U
#define RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MAX       64U
#define RUNTIME_CONFIG_GLOBAL_STARTUP_DELAY_MIN       0U
#define RUNTIME_CONFIG_GLOBAL_STARTUP_DELAY_MAX       60U
#define RUNTIME_CONFIG_GLOBAL_SCREENSAVER_MIN         1U
#define RUNTIME_CONFIG_GLOBAL_SCREENSAVER_MAX         60U
#define RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_UI_MAX       100U
#define RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MIN      1606U /* legacy 100/255 mapped into 12-bit DAC space */
#define RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_RAW_MAX      4095U
#define RUNTIME_CONFIG_USER_THEME_COUNT               3U
#define RUNTIME_CONFIG_METRONOME_VOLUME_MAX           100U
#define RUNTIME_CONFIG_METRONOME_BEATS_PER_BAR_MIN    3U
#define RUNTIME_CONFIG_METRONOME_BEATS_PER_BAR_MAX    8U

typedef enum {
    RUNTIME_CONFIG_SYNC_STYLE_MIDI_CLOCK = 0,
    RUNTIME_CONFIG_SYNC_STYLE_TAP_TEMPO_CC,
} RuntimeConfigSyncStyle_t;

typedef enum {
    RUNTIME_CONFIG_METRONOME_PITCH_LOW = 0,
    RUNTIME_CONFIG_METRONOME_PITCH_MID,
    RUNTIME_CONFIG_METRONOME_PITCH_HIGH,
} RuntimeConfigMetronomePitch_t;

typedef enum {
    RUNTIME_CONFIG_METRONOME_RHYTHM_QUARTER_NOTES = 0,
    RUNTIME_CONFIG_METRONOME_RHYTHM_OFFBEAT,
    RUNTIME_CONFIG_METRONOME_RHYTHM_TRIPLETS,
    RUNTIME_CONFIG_METRONOME_RHYTHM_SHUFFLE,
} RuntimeConfigMetronomeRhythm_t;

/* Display modes are persisted as raw enum values in RuntimeConfig_t and are
 * used as direct indices into display_theme.c. Add new themes before COUNT.
 * If you remove or reorder a theme, migrate old persisted ids when loading
 * older store versions so saved configs continue to point at the right mode. */
typedef enum {
    RUNTIME_CONFIG_DISPLAY_MODE_DARK = 0,
    RUNTIME_CONFIG_DISPLAY_MODE_BRIGHT,
    RUNTIME_CONFIG_DISPLAY_MODE_USER,
    RUNTIME_CONFIG_DISPLAY_MODE_BLUESCREEN,
    RUNTIME_CONFIG_DISPLAY_MODE_TRIPPING,
    RUNTIME_CONFIG_DISPLAY_MODE_USER2,
    RUNTIME_CONFIG_DISPLAY_MODE_USER3,
    RUNTIME_CONFIG_DISPLAY_MODE_C64,
    RUNTIME_CONFIG_DISPLAY_MODE_BIOS,
    /* Must stay last so bounds checks and theme table sizing remain correct. */
    RUNTIME_CONFIG_DISPLAY_MODE_COUNT,
} RuntimeConfigDisplayMode_t;

typedef struct {
    uint8_t channel;
    uint8_t program;
} RuntimeConfigProgramMessage_t;

typedef struct {
    char name[RUNTIME_CONFIG_FUNCTION_BUTTON_NAME_LENGTH + 1U];
    char active_label[RUNTIME_CONFIG_FUNCTION_BUTTON_LABEL_LENGTH + 1U];
    char inactive_label[RUNTIME_CONFIG_FUNCTION_BUTTON_LABEL_LENGTH + 1U];
    RuntimeConfigProgramMessage_t active_programs[RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT];
    PresetCCSlot_t active_cc[RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT];
    RuntimeConfigProgramMessage_t inactive_programs[RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT];
    PresetCCSlot_t inactive_cc[RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT];
} RuntimeConfigFunctionButton_t;

typedef struct {
    char name[RUNTIME_CONFIG_BANK_NAME_LENGTH + 1U];
    uint8_t wet_dry_enabled;
    RuntimeConfigFunctionButton_t function_button;
    uint8_t midi_clock_bar_count;
} RuntimeConfigBank_t;

typedef struct {
    char name[RUNTIME_CONFIG_DEVICE_NAME_LENGTH + 1U];
    uint8_t channel;
    MidiCC_t active;
    MidiCC_t bypass;
    MidiCC_t level;
    MidiCC_t tap_tempo;
    uint8_t max_preset;
} RuntimeConfigDevice_t;

typedef enum {
    RUNTIME_CONFIG_USER_THEME_FIELD_DISPLAY_BG = 0,
    RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_FOOTBAR,
    RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_FOOTBAR_TEXT,
    RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_INFO_TEXT,
    RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_INFO_EDIT_CURSOR_TEXT,
    RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_INFO_EDIT_CURSOR_BG,
    RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_INFO_EDIT_CURSOR_SHARED_BG,
    RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_SAVING_POPUP_BG,
    RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_SAVING_POPUP_TEXT,
    RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_SAVING_POPUP_BORDER,
    RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_MODE_HEADER,
    RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_MODE_HEADER_EDIT,
    RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_MODE_HEADER_EDIT_BG,
    RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_PRESET,
    RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_BANK,
    RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_BANK_WET_DRY,
    RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_SPECIAL_FUNCTION_BUTTON_ACTIVE,
    RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_SPECIAL_FUNCTION_BUTTON_INACTIVE,
    RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_SPECIAL_FUNCTION_BUTTON_ACTIVE_BG,
    RUNTIME_CONFIG_USER_THEME_FIELD_MAIN_ALERT_BADGE_TEXT,
    RUNTIME_CONFIG_USER_THEME_FIELD_BPM_INTERNAL,
    RUNTIME_CONFIG_USER_THEME_FIELD_EXT_BPM,
    RUNTIME_CONFIG_USER_THEME_FIELD_COUNT,
} RuntimeConfigUserThemeField_t;

typedef struct {
    uint16_t display_bg_colour;
    uint16_t main_footbar_color;
    uint16_t main_footbar_text_colour;
    uint16_t main_info_text_colour;
    uint16_t main_info_edit_cursor_text_colour;
    uint16_t main_info_edit_cursor_bg_colour;
    uint16_t main_info_edit_cursor_shared_bg_colour;
    uint16_t main_saving_popup_bg_colour;
    uint16_t main_saving_popup_text_colour;
    uint16_t main_saving_popup_border_colour;
    uint16_t main_mode_header_colour;
    uint16_t main_mode_header_edit_colour;
    uint16_t main_mode_header_edit_bg_colour;
    uint16_t main_preset_colour;
    uint16_t main_bank_colour;
    uint16_t main_bank_wet_dry_colour;
    uint16_t main_special_function_button_active_colour;
    uint16_t main_special_function_button_inactive_colour;
    uint16_t main_special_function_button_active_bg;
    uint16_t main_alert_badge_text_colour;
    uint16_t bpm_internal_colour;
    uint16_t ext_bpm_colour;
} RuntimeConfigUserTheme_t;

typedef struct {
    uint8_t volume;
    RuntimeConfigMetronomePitch_t pitch;
    uint8_t beats_per_bar;
    RuntimeConfigMetronomeRhythm_t rhythm;
} RuntimeConfigMetronome_t;

typedef struct {
    uint8_t startup_delay_seconds;
    uint8_t screensaver_timeout_minutes;
    RuntimeConfigSyncStyle_t sync_style;
    RuntimeConfigDisplayMode_t display_mode;
    uint16_t backlight_brightness;
} RuntimeConfigGlobal_t;

typedef struct {
    RuntimeConfigBank_t banks[PRESET_BANK_COUNT];
    RuntimeConfigDevice_t devices[MIDI_DEVICE_COUNT];
    RuntimeConfigGlobal_t global;
    RuntimeConfigMetronome_t metronome;
    RuntimeConfigUserTheme_t user_themes[RUNTIME_CONFIG_USER_THEME_COUNT];
} RuntimeConfig_t;

void RuntimeConfig_Init(void);

const RuntimeConfig_t *RuntimeConfig_Get(void);
RuntimeConfig_t *RuntimeConfig_GetMutable(void);

const RuntimeConfigBank_t *RuntimeConfig_GetBank(uint8_t bank_index);
RuntimeConfigBank_t *RuntimeConfig_GetMutableBank(uint8_t bank_index);
void RuntimeConfig_ResetBankToDefaults(uint8_t bank_index);

const RuntimeConfigFunctionButton_t *RuntimeConfig_GetFunctionButton(uint8_t bank_index);
RuntimeConfigFunctionButton_t *RuntimeConfig_GetMutableFunctionButton(uint8_t bank_index);

const RuntimeConfigDevice_t *RuntimeConfig_GetDevice(uint8_t device_index);
RuntimeConfigDevice_t *RuntimeConfig_GetMutableDevice(uint8_t device_index);

const RuntimeConfigGlobal_t *RuntimeConfig_GetGlobal(void);
RuntimeConfigGlobal_t *RuntimeConfig_GetMutableGlobal(void);
const RuntimeConfigMetronome_t *RuntimeConfig_GetMetronome(void);
RuntimeConfigMetronome_t *RuntimeConfig_GetMutableMetronome(void);
uint8_t RuntimeConfig_TryGetUserThemeIndex(RuntimeConfigDisplayMode_t display_mode, uint8_t *theme_index);
const RuntimeConfigUserTheme_t *RuntimeConfig_GetUserTheme(RuntimeConfigDisplayMode_t display_mode);
RuntimeConfigUserTheme_t *RuntimeConfig_GetMutableUserTheme(RuntimeConfigDisplayMode_t display_mode);
uint16_t RuntimeConfig_GetUserThemeColour(const RuntimeConfigUserTheme_t *user_theme,
                                          RuntimeConfigUserThemeField_t field);
uint8_t RuntimeConfig_SetUserThemeColour(RuntimeConfigUserTheme_t *user_theme,
                                         RuntimeConfigUserThemeField_t field,
                                         uint16_t colour);
RuntimeConfigDisplayMode_t RuntimeConfig_NormalizeDisplayMode(uint8_t display_mode);
RuntimeConfigDisplayMode_t RuntimeConfig_StepDisplayMode(RuntimeConfigDisplayMode_t display_mode, int8_t delta);

void RuntimeConfig_MarkDirty(void);
uint8_t RuntimeConfig_IsDirty(void);
void RuntimeConfig_ClearDirty(void);
uint8_t RuntimeConfig_SaveIfDirty(void);
void RuntimeConfig_FormatPersistentStoreStatusText(char *buffer, size_t buffer_size);
void RuntimeConfig_ApplySnapshot(const RuntimeConfig_t *snapshot);

void RuntimeConfig_ResetToDefaults(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_CONFIG_H */