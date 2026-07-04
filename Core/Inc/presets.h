#ifndef PRESETS_H
#define PRESETS_H /* include guard for preset/bank declarations */

#include <stdbool.h>
#include <stdint.h>
#include "midi_devices.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Regular presets are grouped in banks of eight to match the numbered
 * preset footswitches. PRESET_COUNT remains the flat total used by the
 * current activation and persistence logic. */
#define PRESETS_PER_BANK     8U  /* number of numbered preset footswitch slots in each bank */
#define PRESET_BANK_COUNT    8U  /* number of banks compiled into the preset table */
#define PRESET_COUNT         (PRESETS_PER_BANK * PRESET_BANK_COUNT) /* total flat preset count across all banks */
#define PRESET_GLOBAL_BYPASS_INDEX PRESET_COUNT
#define PRESET_GLOBAL_MUTE_INDEX   (PRESET_COUNT + 1U)
#define PRESET_BANK_NAME_MAXLEN  16U /* maximum displayed character width reserved for a bank name */
#define PRESET_NAME_LENGTH   20U /* editable/displayed character count for preset names, excluding the trailing NUL */

/* Shared sentinel values used by preset data tables and activation logic. */
#define PRESET_PROGRAM_NONE       0xFFU /* sentinel meaning this device slot sends no Program Change; cannot be NULL because Program Change 0 is valid data */
#define PRESET_PROGRAM_UNUSED     PRESET_PROGRAM_NONE /* backward-compatible alias for older code and table entries */
#define PRESET_CC_CHANNEL_UNUSED  0xFFU /* sentinel meaning this CC slot is unused */
#define PRESET_CC_NUMBER_UNUSED   PRESET_PROGRAM_NONE /* sentinel CC number for an unused CC slot */
#define PRESET_CC_VALUE_UNUSED    0xFFU /* sentinel CC value for an unused CC slot */
#define PRESET_RELAY_OPEN         0U    /* relay state value for open/bypass */
#define PRESET_RELAY_CLOSED       1U    /* relay state value for closed/engaged */
#define RUNTIME_CONFIG_FUNCTION_BUTTON_NAME_LENGTH    6U
#define RUNTIME_CONFIG_FUNCTION_BUTTON_LABEL_LENGTH   6U
#define RUNTIME_CONFIG_FUNCTION_BUTTON_PROGRAM_COUNT  4U
#define RUNTIME_CONFIG_FUNCTION_BUTTON_CC_COUNT       4U

extern volatile uint8_t current_bank;

/**
 * @brief  Return the name of the given bank (0-based).
 *         Returns a fallback if out of range.
 */
const char *Presets_GetBankName(uint8_t bank);

/**
 * Number of device slots per preset.
 * Slot N maps directly to MidiDevices_Get(N) — no channel field needed here,
 * the channel lives in the device table.
 */
#define PRESET_DEVICE_SLOTS  MIDI_DEVICE_COUNT /* number of per-device program slots stored in each preset */

/**
 * @brief  Per-device MIDI data for one preset.
 *
 *  program  — Program Change number to send when this preset loads (0–127).
 *             PRESET_PROGRAM_NONE = do not send a Program Change.
 */
typedef struct {
    uint8_t program;
} PresetDevice_t;

/** Number of extra per-preset MIDI CC messages. */
#define PRESET_CC_SLOT_COUNT  8U /* number of extra CC messages stored in each preset */

/**
 * @brief  One per-preset MIDI CC message.
 *
 *  channel    — MIDI channel to send on (1-16).
 *               PRESET_CC_CHANNEL_UNUSED = unused slot.
 *  cc_number  — CC number to send (0-127).
 *               PRESET_CC_NUMBER_UNUSED = unused slot.
 *  value      — CC value to send (0-127).
 *               PRESET_CC_VALUE_UNUSED = unused slot.
 */
typedef struct {
    uint8_t channel;
    uint8_t cc_number;
    uint8_t value;
} PresetCCSlot_t;

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

/** Number of independent relay outputs. */
#define PRESET_RELAY_COUNT  2U /* number of relay outputs tracked per preset */

/**
 * @brief  One preset.
 *
 *  name      — display name, max 20 chars + NUL.
 *  prg[N]    — Program Change data for device N; index matches MidiDevices_Get(N).
 *              prg[0]..prg[7] map directly to device slots 0..7.
 *  cc[N]     — extra CC messages to send when this preset is activated.
 *  relay[N]  — state of relay N, independent of any MIDI device.
 *              PRESET_RELAY_OPEN = open/bypass,
 *              PRESET_RELAY_CLOSED = closed/engaged.
 */
typedef struct {
    char           name[PRESET_NAME_LENGTH + 1U];
    PresetDevice_t prg[PRESET_DEVICE_SLOTS];
    PresetCCSlot_t cc[PRESET_CC_SLOT_COUNT];
    uint8_t        relay[PRESET_RELAY_COUNT];
    RuntimeConfigFunctionButton_t function_button;
} Preset_t;

/**
 * @brief  Return a pointer to the preset at @p index.
 *         Returns a pointer to a blank preset if @p index is out of range.
 */
const Preset_t *Presets_Get(uint8_t index);

/**
 * @brief  Return a mutable pointer to the preset at @p index.
 *         Returns NULL if @p index is out of range.
 */
Preset_t *Presets_GetMutable(uint8_t index);
const RuntimeConfigFunctionButton_t *Presets_GetFunctionButton(uint8_t index);
RuntimeConfigFunctionButton_t *Presets_GetMutableFunctionButton(uint8_t index);
const RuntimeConfigFunctionButton_t *Presets_GetActiveFunctionButton(void);
uint8_t Presets_IsRandomPreset(const Preset_t *preset);
const Preset_t *Presets_GetGlobalBypassPreset(void);
Preset_t *Presets_GetMutableGlobalBypassPreset(void);
const Preset_t *Presets_GetGlobalMutePreset(void);
Preset_t *Presets_GetMutableGlobalMutePreset(void);
uint8_t Presets_IsGlobalBypassPreset(const Preset_t *preset);
uint8_t Presets_IsGlobalMutePreset(const Preset_t *preset);

/**
 * @brief  Reset the preset at @p index back to its compiled default data.
 */
void Presets_ResetPresetToDefaults(uint8_t index);

/**
 * @brief  Mark the runtime preset store dirty after an edit.
 */
void Presets_MarkDirty(void);

/**
 * @brief  Report whether the runtime preset store has unsaved edits.
 * @return 1 when a preset save is pending, 0 otherwise.
 */
uint8_t Presets_IsDirty(void);

/**
 * @brief  Return whether a persisted preset payload size can be loaded by this firmware.
 *         Used by the shared preset/config image loader so both halves accept
 *         the same legacy image shapes.
 */
uint8_t Presets_PersistentPayloadSizeIsSupported(uint32_t payload_size);
uint8_t Presets_RuntimeStoreLooksFactoryDefault(void);

/**
 * @brief  Save the runtime preset store to flash when dirty.
 * @return 1 if the store is now persisted or did not need saving, 0 on write failure.
 */
uint8_t Presets_SaveIfDirty(void);

/**
 * @brief  Return the number of presets defined in the table.
 */
uint8_t Presets_Count(void);

/**
 * @brief  Returns true if the given program number is used in more than one preset
 *         for the given device slot (ignores PRESET_PROGRAM_NONE).
 * @param  slot      Device slot index (0..PRESET_DEVICE_SLOTS-1).
 * @param  program   Program number to check (0..127).
 */
bool Presets_DeviceProgramIsShared(uint8_t slot, uint8_t program);

/**
 * @brief  Activate preset @p idx: update active state and send its MIDI payload.
 */
void App_ActivatePreset(uint8_t idx);

/**
 * @brief  Activate the random preset.
 */
void Presets_ActivateRandom(void);

/**
 * @brief  Activate the mute preset.
 */
void Presets_ActivateMute(void);

#ifdef __cplusplus
}
#endif

#endif /* PRESETS_H */
