#include "midi_functions.h"
#include "midi/midi_output.h"

/* Public MIDI facade for device-level send helpers.
 *
 * The MIDI domain is split across focused modules:
 *   - midi_input.c owns USART2 RX/TX soft-thru and sync byte ingestion
 *   - midi_transport.c and midi_transport_state.c own external sync state
 *   - midi_clock.c owns TIM6 clock generation
 *   - midi_output.c owns queued UART4 output scheduling and the UART4 IRQ bridge
 * This file keeps only the public device/program/CC/preset helpers.
 *
 * To add a new device:
 *   1. Add a row to the device_table in midi_devices.c with its own channel.
 *   2. The preset_table in presets.c can then reference that device slot.
 *
 * MIDI standard: 31 250 baud, 8 data bits, no parity, 1 stop bit (8-N-1). */

#define MIDI_CHANNEL_FIRST                 1U
#define MIDI_CHANNEL_LAST                  16U
#define MIDI_CHANNEL_STATUS_MASK           0x0FU
#define MIDI_DATA_MASK                     0x7FU
#define MIDI_PROGRAM_CHANGE_STATUS         0xC0U
#define MIDI_CONTROL_CHANGE_STATUS         0xB0U

static uint8_t midi_channel_is_valid(uint8_t channel);
static void midi_output_send_bytes(const uint8_t *bytes, uint16_t length);

void MidiSetOutputUart(UART_HandleTypeDef *uart_handle)
{
    MidiOutput_SetUart(uart_handle);
}

static uint8_t midi_channel_is_valid(uint8_t channel)
{
    return (uint8_t)(channel >= MIDI_CHANNEL_FIRST && channel <= MIDI_CHANNEL_LAST);
}

void MidiOutputSchedulerService(void)
{
    MidiOutput_ServiceScheduler();
}

static void midi_output_send_bytes(const uint8_t *bytes, uint16_t length)
{
    (void)MidiOutput_QueueMessageBytes(bytes, length);
}

/* ── MIDI_SendProgramChange ──────────────────────────────────────────────────
 * Sends a 2-byte Program Change message:
 *   Byte 0:  0xC0 | (channel-1)   — status byte, upper nibble 0xC = Program Change
 *   Byte 1:  program & 0x7F       — program number (7-bit, 0–127)
 *
 * The 10 ms timeout is more than enough: at 31 250 baud two bytes take ~640 µs.
 * ─────────────────────────────────────────────────────────────────────────── */
void MIDI_SendProgramChange(uint8_t channel, uint8_t program)
{
    if (!midi_channel_is_valid(channel))
        return;

    uint8_t msg[2] = {
        (uint8_t)(MIDI_PROGRAM_CHANGE_STATUS | ((channel - MIDI_CHANNEL_FIRST) & MIDI_CHANNEL_STATUS_MASK)),  /* channel 1-16 → nibble 0-15 */
        (uint8_t)(program & MIDI_DATA_MASK),                                                 /* mask to 7-bit MIDI data range */
    };
    midi_output_send_bytes(msg, (uint16_t)sizeof(msg));
}

/* ── MIDI_SendCC ─────────────────────────────────────────────────────────────
 * Sends a 3-byte Control Change message:
 *   Byte 0:  0xB0 | (channel-1)   — status byte, 0xB = Control Change
 *   Byte 1:  cc_number & 0x7F     — which controller (e.g. 60 = engage/bypass)
 *   Byte 2:  value & 0x7F         — controller value  (e.g. 127 = on, 0 = off)
 *
 * See midi_devices.c for the CC numbers used by each pedal.
 * ─────────────────────────────────────────────────────────────────────────── */
void MIDI_SendCC(uint8_t channel, uint8_t cc_number, uint8_t value)
{
    if (!midi_channel_is_valid(channel))
        return;

    uint8_t msg[3] = {
        (uint8_t)(MIDI_CONTROL_CHANGE_STATUS | ((channel - MIDI_CHANNEL_FIRST) & MIDI_CHANNEL_STATUS_MASK)),
        (uint8_t)(cc_number & MIDI_DATA_MASK),
        (uint8_t)(value & MIDI_DATA_MASK),
    };
    midi_output_send_bytes(msg, (uint16_t)sizeof(msg));
}

void Midi_SendPresetCCs(const Preset_t *preset)
{
    if (!preset) return;

    for (uint8_t i = 0U; i < PRESET_CC_SLOT_COUNT; i++)
    {
        const PresetCCSlot_t *cc = &preset->cc[i];

        if (cc->channel == PRESET_CC_CHANNEL_UNUSED || cc->cc_number == PRESET_CC_NUMBER_UNUSED || cc->value == PRESET_CC_VALUE_UNUSED)
            continue;

        MIDI_SendCC(cc->channel, cc->cc_number, cc->value);
    }
}

void Midi_SendDeviceProgramSlot(uint8_t device_index, uint8_t program)
{
    const MidiDevice_t *dev = MidiDevices_Get(device_index);

    if (!dev)
        return;

    if (program == PRESET_PROGRAM_NONE)
    {
        if (dev->bypass.cc != PRESET_CC_NUMBER_UNUSED)
            MIDI_SendCC(dev->channel, dev->bypass.cc, dev->bypass.value);
        return;
    }

    MIDI_SendProgramChange(dev->channel, program);
    if (dev->engage.cc != PRESET_CC_NUMBER_UNUSED)
        MIDI_SendCC(dev->channel, dev->engage.cc, dev->engage.value);
}

void Midi_LoadPreset(const Preset_t *preset)
{
    if (!preset) return;

    /* Program changes are sent first so devices switch base patches before any
     * follow-up CCs try to tweak parameters on the newly selected preset. */
    for (uint8_t i = 0U; i < PRESET_DEVICE_SLOTS; i++)
    {
        Midi_SendDeviceProgramSlot(i, preset->prg[i].program);
    }

    Midi_SendPresetCCs(preset);
}
