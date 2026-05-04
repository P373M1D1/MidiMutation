#include "runtime_config.h"

#include <string.h>

#define RUNTIME_CONFIG_INVALID_BANK_NAME                "(bank?)"
#define RUNTIME_CONFIG_GLOBAL_STARTUP_DELAY_DEFAULT     1U
#define RUNTIME_CONFIG_GLOBAL_SCREENSAVER_MIN_DEFAULT   10U
#define RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_DEFAULT        2095U

#define RUNTIME_CONFIG_PROGRAM_MESSAGE_UNUSED \
    { .channel = PRESET_CC_CHANNEL_UNUSED, .program = PRESET_PROGRAM_NONE }

#define RUNTIME_CONFIG_CC_MESSAGE_UNUSED \
    { .channel = PRESET_CC_CHANNEL_UNUSED, .cc_number = PRESET_CC_NUMBER_UNUSED, .value = PRESET_CC_VALUE_UNUSED }

#define RUNTIME_CONFIG_PROGRAM_MESSAGE_LIST_EMPTY \
    { RUNTIME_CONFIG_PROGRAM_MESSAGE_UNUSED, RUNTIME_CONFIG_PROGRAM_MESSAGE_UNUSED, RUNTIME_CONFIG_PROGRAM_MESSAGE_UNUSED, RUNTIME_CONFIG_PROGRAM_MESSAGE_UNUSED }

#define RUNTIME_CONFIG_CC_MESSAGE_LIST_EMPTY \
    { RUNTIME_CONFIG_CC_MESSAGE_UNUSED, RUNTIME_CONFIG_CC_MESSAGE_UNUSED, RUNTIME_CONFIG_CC_MESSAGE_UNUSED, RUNTIME_CONFIG_CC_MESSAGE_UNUSED }

#define RUNTIME_CONFIG_DEVICE_CC_UNUSED \
    { .cc = PRESET_CC_NUMBER_UNUSED, .value = 0U }

#define RUNTIME_CONFIG_FUNCTION_BUTTON_DEFAULT \
    { \
        .name = "Vita", \
        .active_label = "undead", \
        .inactive_label = "dead", \
        .active_programs = RUNTIME_CONFIG_PROGRAM_MESSAGE_LIST_EMPTY, \
        .active_cc = RUNTIME_CONFIG_CC_MESSAGE_LIST_EMPTY, \
        .inactive_programs = RUNTIME_CONFIG_PROGRAM_MESSAGE_LIST_EMPTY, \
        .inactive_cc = RUNTIME_CONFIG_CC_MESSAGE_LIST_EMPTY, \
    }

#define RUNTIME_CONFIG_BANK_ENTRY(name_literal) \
    { \
        .name = name_literal, \
        .wet_dry_enabled = 0U, \
        .function_button = RUNTIME_CONFIG_FUNCTION_BUTTON_DEFAULT, \
    }

#define RUNTIME_CONFIG_DEVICE_ENTRY(name_literal, channel_value, active_cc_value, active_data_value, bypass_cc_value, bypass_data_value, tap_cc_value, tap_data_value, max_preset_value) \
    { \
        .name = name_literal, \
        .channel = channel_value, \
        .active = { .cc = active_cc_value, .value = active_data_value }, \
        .bypass = { .cc = bypass_cc_value, .value = bypass_data_value }, \
        .level = RUNTIME_CONFIG_DEVICE_CC_UNUSED, \
        .tap_tempo = { .cc = tap_cc_value, .value = tap_data_value }, \
        .max_preset = max_preset_value, \
    }

static const RuntimeConfigBank_t runtime_config_blank_bank = {
    .name = RUNTIME_CONFIG_INVALID_BANK_NAME,
    .wet_dry_enabled = 0U,
    .function_button = RUNTIME_CONFIG_FUNCTION_BUTTON_DEFAULT,
};

static const RuntimeConfigDevice_t runtime_config_blank_device = {
    .name = "",
    .channel = 0U,
    .active = RUNTIME_CONFIG_DEVICE_CC_UNUSED,
    .bypass = RUNTIME_CONFIG_DEVICE_CC_UNUSED,
    .level = RUNTIME_CONFIG_DEVICE_CC_UNUSED,
    .tap_tempo = RUNTIME_CONFIG_DEVICE_CC_UNUSED,
    .max_preset = 0U,
};

static const RuntimeConfig_t runtime_config_defaults = {
    .banks = {
        RUNTIME_CONFIG_BANK_ENTRY("[Strain I]"),
        RUNTIME_CONFIG_BANK_ENTRY("[Strain II]"),
        RUNTIME_CONFIG_BANK_ENTRY("[Strain III]"),
        RUNTIME_CONFIG_BANK_ENTRY("[Strain IV]"),
        RUNTIME_CONFIG_BANK_ENTRY("[Strain V]"),
        RUNTIME_CONFIG_BANK_ENTRY("[Strain VI]"),
        RUNTIME_CONFIG_BANK_ENTRY("[Strain VII]"),
        RUNTIME_CONFIG_BANK_ENTRY("[Strain VIII]"),
    },
    .devices = {
        RUNTIME_CONFIG_DEVICE_ENTRY("", 1U, 60U, 127U, 60U, 0U, 35U, 64U, 35U),
        RUNTIME_CONFIG_DEVICE_ENTRY("", 2U, 60U, 127U, 60U, 0U, 35U, 64U, 35U),
        RUNTIME_CONFIG_DEVICE_ENTRY("", 3U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, 127U),
        RUNTIME_CONFIG_DEVICE_ENTRY("", 4U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, 127U),
        RUNTIME_CONFIG_DEVICE_ENTRY("", 5U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, 127U),
        RUNTIME_CONFIG_DEVICE_ENTRY("", 6U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, 127U),
        RUNTIME_CONFIG_DEVICE_ENTRY("", 7U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, 127U),
        RUNTIME_CONFIG_DEVICE_ENTRY("", 8U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, PRESET_CC_NUMBER_UNUSED, 0U, 127U),
    },
    .global = {
        .startup_delay_seconds = RUNTIME_CONFIG_GLOBAL_STARTUP_DELAY_DEFAULT,
        .screensaver_timeout_minutes = RUNTIME_CONFIG_GLOBAL_SCREENSAVER_MIN_DEFAULT,
        .sync_style = RUNTIME_CONFIG_SYNC_STYLE_MIDI_CLOCK,
        .backlight_brightness = RUNTIME_CONFIG_GLOBAL_BRIGHTNESS_DEFAULT,
    },
};

static RuntimeConfig_t runtime_config_store;
static uint8_t runtime_config_initialized = 0U;

static void RuntimeConfig_EnsureInitialized(void)
{
    if (runtime_config_initialized)
        return;

    memcpy(&runtime_config_store, &runtime_config_defaults, sizeof(runtime_config_store));
    runtime_config_initialized = 1U;
}

const RuntimeConfig_t *RuntimeConfig_Get(void)
{
    RuntimeConfig_EnsureInitialized();
    return &runtime_config_store;
}

RuntimeConfig_t *RuntimeConfig_GetMutable(void)
{
    RuntimeConfig_EnsureInitialized();
    return &runtime_config_store;
}

const RuntimeConfigBank_t *RuntimeConfig_GetBank(uint8_t bank_index)
{
    RuntimeConfig_EnsureInitialized();

    if (bank_index >= PRESET_BANK_COUNT)
        return &runtime_config_blank_bank;

    return &runtime_config_store.banks[bank_index];
}

RuntimeConfigBank_t *RuntimeConfig_GetMutableBank(uint8_t bank_index)
{
    RuntimeConfig_EnsureInitialized();

    if (bank_index >= PRESET_BANK_COUNT)
        return NULL;

    return &runtime_config_store.banks[bank_index];
}

const RuntimeConfigFunctionButton_t *RuntimeConfig_GetFunctionButton(uint8_t bank_index)
{
    return &RuntimeConfig_GetBank(bank_index)->function_button;
}

RuntimeConfigFunctionButton_t *RuntimeConfig_GetMutableFunctionButton(uint8_t bank_index)
{
    RuntimeConfigBank_t *bank = RuntimeConfig_GetMutableBank(bank_index);

    if (!bank)
        return NULL;

    return &bank->function_button;
}

const RuntimeConfigDevice_t *RuntimeConfig_GetDevice(uint8_t device_index)
{
    RuntimeConfig_EnsureInitialized();

    if (device_index >= MIDI_DEVICE_COUNT)
        return &runtime_config_blank_device;

    return &runtime_config_store.devices[device_index];
}

RuntimeConfigDevice_t *RuntimeConfig_GetMutableDevice(uint8_t device_index)
{
    RuntimeConfig_EnsureInitialized();

    if (device_index >= MIDI_DEVICE_COUNT)
        return NULL;

    return &runtime_config_store.devices[device_index];
}

const RuntimeConfigGlobal_t *RuntimeConfig_GetGlobal(void)
{
    RuntimeConfig_EnsureInitialized();
    return &runtime_config_store.global;
}

RuntimeConfigGlobal_t *RuntimeConfig_GetMutableGlobal(void)
{
    RuntimeConfig_EnsureInitialized();
    return &runtime_config_store.global;
}

void RuntimeConfig_ResetToDefaults(void)
{
    memcpy(&runtime_config_store, &runtime_config_defaults, sizeof(runtime_config_store));
    runtime_config_initialized = 1U;
}