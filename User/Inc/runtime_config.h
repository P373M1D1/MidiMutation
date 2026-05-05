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
#define RUNTIME_CONFIG_DEVICE_NAME_LENGTH             6U
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

typedef enum {
    RUNTIME_CONFIG_SYNC_STYLE_MIDI_CLOCK = 0,
    RUNTIME_CONFIG_SYNC_STYLE_TAP_TEMPO_CC,
} RuntimeConfigSyncStyle_t;

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

typedef struct {
    uint8_t startup_delay_seconds;
    uint8_t screensaver_timeout_minutes;
    RuntimeConfigSyncStyle_t sync_style;
    uint16_t backlight_brightness;
} RuntimeConfigGlobal_t;

typedef struct {
    RuntimeConfigBank_t banks[PRESET_BANK_COUNT];
    RuntimeConfigDevice_t devices[MIDI_DEVICE_COUNT];
    RuntimeConfigGlobal_t global;
} RuntimeConfig_t;

void RuntimeConfig_Init(void);

const RuntimeConfig_t *RuntimeConfig_Get(void);
RuntimeConfig_t *RuntimeConfig_GetMutable(void);

const RuntimeConfigBank_t *RuntimeConfig_GetBank(uint8_t bank_index);
RuntimeConfigBank_t *RuntimeConfig_GetMutableBank(uint8_t bank_index);

const RuntimeConfigFunctionButton_t *RuntimeConfig_GetFunctionButton(uint8_t bank_index);
RuntimeConfigFunctionButton_t *RuntimeConfig_GetMutableFunctionButton(uint8_t bank_index);

const RuntimeConfigDevice_t *RuntimeConfig_GetDevice(uint8_t device_index);
RuntimeConfigDevice_t *RuntimeConfig_GetMutableDevice(uint8_t device_index);

const RuntimeConfigGlobal_t *RuntimeConfig_GetGlobal(void);
RuntimeConfigGlobal_t *RuntimeConfig_GetMutableGlobal(void);

void RuntimeConfig_MarkDirty(void);
uint8_t RuntimeConfig_IsDirty(void);
void RuntimeConfig_ClearDirty(void);
uint8_t RuntimeConfig_SaveIfDirty(void);
void RuntimeConfig_ApplySnapshot(const RuntimeConfig_t *snapshot);

void RuntimeConfig_ResetToDefaults(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_CONFIG_H */