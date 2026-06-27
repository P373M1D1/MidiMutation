#include "midi_functions.h"
#include "midi/midi_output.h"
#include "runtime_config.h"

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
#define MIDI_PRESET_RETRY_MAX_ATTEMPTS     3U

static uint8_t midi_channel_is_valid(uint8_t channel);
static uint8_t midi_output_send_bytes(const uint8_t *bytes, uint16_t length);
static uint8_t midi_output_send_tracked_bytes(const uint8_t *bytes,
                                              uint16_t length,
                                              uint32_t sequence);
static void Midi_MaybeSendFeedbackTaperCc(uint8_t channel,
                                          uint8_t cc_number,
                                          uint8_t threshold,
                                          uint8_t reduce);
static void Midi_ApplyFeedbackTaperForBypassedDevice(uint8_t device_index, uint8_t program);
static uint8_t Midi_SendPresetCCsInternal(const Preset_t *preset);
static uint8_t Midi_SendDeviceProgramSlotInternal(uint8_t device_index, uint8_t program);
static uint8_t Midi_SendDeviceProgramSlotTransitionInternal(uint8_t device_index,
                                                           uint8_t previous_program,
                                                           uint8_t program);
static uint8_t Midi_LoadPresetInternal(const Preset_t *preset,
                                       const Preset_t *previous_preset,
                                       uint8_t send_auto_transitions,
                                       uint8_t allow_retry_schedule,
                                       uint8_t urgent_retry);
static void Midi_ClearPendingPresetRetry(void);

static const Preset_t *midi_pending_retry_preset = NULL;
static uint8_t midi_pending_retry_attempts_remaining = 0U;
static uint8_t midi_pending_retry_urgent = 0U;
static uint32_t midi_producer_preset_retry_successes = 0U;
static uint32_t midi_producer_preset_retry_failures = 0U;
static uint32_t midi_producer_tap_tempo_drop_count = 0U;
static uint32_t midi_producer_feedback_taper_drop_count = 0U;

/**
 * Binds the public MIDI facade to the UART used for outbound transport.
 */
void MidiSetOutputUart(UART_HandleTypeDef *uart_handle)
{
    MidiOutput_SetUart(uart_handle);
}

static uint8_t midi_channel_is_valid(uint8_t channel)
{
    return (uint8_t)(channel >= MIDI_CHANNEL_FIRST && channel <= MIDI_CHANNEL_LAST);
}

/**
 * Services deferred UART4 MIDI output scheduling in the foreground.
 */
void MidiOutputSchedulerService(void)
{
    MidiOutput_ServiceScheduler();
}

void MidiProducerService(void)
{
    if (!midi_pending_retry_preset || midi_pending_retry_attempts_remaining == 0U)
        return;

    if (Midi_LoadPresetInternal(midi_pending_retry_preset, NULL, 0U, 0U, midi_pending_retry_urgent))
    {
        Midi_ClearPendingPresetRetry();
        if (midi_producer_preset_retry_successes < UINT32_MAX)
            midi_producer_preset_retry_successes++;
        return;
    }

    midi_pending_retry_attempts_remaining--;
    if (midi_pending_retry_attempts_remaining == 0U)
    {
        Midi_ClearPendingPresetRetry();
        if (midi_producer_preset_retry_failures < UINT32_MAX)
            midi_producer_preset_retry_failures++;
    }
}

void MidiProducer_NoteTapTempoDrop(void)
{
    if (midi_producer_tap_tempo_drop_count < UINT32_MAX)
        midi_producer_tap_tempo_drop_count++;
}

/**
 * Routes the timing-counter interrupt to the MIDI clock output backend.
 */
void MidiHandleTimingCounterIrq(void)
{
    MidiOutput_HandleTimingCounterIrq();
}

void MidiProducer_TakeDiagnostics(MidiProducerDiagnostics_t *diagnostics)
{
    if (!diagnostics)
        return;

    diagnostics->preset_retry_pending = (midi_pending_retry_preset != NULL) ? 1U : 0U;
    diagnostics->preset_retry_attempts_remaining = midi_pending_retry_attempts_remaining;
    diagnostics->preset_retry_successes = midi_producer_preset_retry_successes;
    diagnostics->preset_retry_failures = midi_producer_preset_retry_failures;
    diagnostics->tap_tempo_drop_count = midi_producer_tap_tempo_drop_count;
    diagnostics->feedback_taper_drop_count = midi_producer_feedback_taper_drop_count;

    midi_producer_preset_retry_successes = 0U;
    midi_producer_preset_retry_failures = 0U;
    midi_producer_tap_tempo_drop_count = 0U;
    midi_producer_feedback_taper_drop_count = 0U;
}

/**
 * Enables or disables timebend requests from ENC2.
 */
void MidiTimebendSetEncoderEnabled(uint8_t enabled)
{
    MidiOutput_TimebendSetEncoderEnabled(enabled);
}

/**
 * Enables or disables timebend requests from the expression pedal.
 */
void MidiTimebendSetExpressionEnabled(uint8_t enabled)
{
    MidiOutput_TimebendSetExpressionEnabled(enabled);
}

/**
 * Forwards an ENC2 timebend request delta into the shared outbound engine.
 */
void MidiTimebendRequestFromEncoder(int8_t delta)
{
    MidiOutput_TimebendInjectEncoderDelta(delta);
}

/**
 * Forwards an expression-pedal timebend request delta into the shared outbound engine.
 */
void MidiTimebendRequestFromExpression(int8_t delta)
{
    MidiOutput_TimebendInjectEncoderDelta(delta);
}

/**
 * Returns true when outbound timebend is currently engaged.
 */
uint8_t MidiTimebendIsEngaged(void)
{
    return MidiOutput_TimebendIsEngaged();
}

void MidiTimebendGetBacklogSnapshot(MidiTimebendBacklogSnapshot_t *snapshot)
{
    MidiOutputTimebendBacklogSnapshot_t output_snapshot;

    if (!snapshot)
        return;

    MidiOutput_GetTimebendBacklogSnapshot(&output_snapshot);
    snapshot->active = output_snapshot.active;
    snapshot->due_depth = output_snapshot.due_depth;
    snapshot->uart_clock_depth = output_snapshot.uart_clock_depth;
    snapshot->uart_message_depth = output_snapshot.uart_message_depth;
    snapshot->crossing_backlog_now = output_snapshot.crossing_backlog_now;
    snapshot->crossing_backlog_peak = output_snapshot.crossing_backlog_peak;
    snapshot->dropped_count = output_snapshot.dropped_count;
    snapshot->missed_emit_count = output_snapshot.missed_emit_count;
    snapshot->scheduling_jitter_est_us = output_snapshot.scheduling_jitter_est_us;
}

static uint8_t midi_output_send_bytes(const uint8_t *bytes, uint16_t length)
{
    return MidiOutput_QueueMessageBytes(bytes, length);
}

void MidiCancelPendingPresetRetry(void)
{
    Midi_ClearPendingPresetRetry();
}

static uint8_t midi_output_send_tracked_bytes(const uint8_t *bytes,
                                              uint16_t length,
                                              uint32_t sequence)
{
    return MidiOutput_QueueTrackedMessageBytes(bytes, length, sequence);
}

static void Midi_MaybeSendFeedbackTaperCc(uint8_t channel,
                                          uint8_t cc_number,
                                          uint8_t threshold,
                                          uint8_t reduce)
{
    uint8_t current_value = 0U;
    uint8_t tapered_value;

    if (cc_number == PRESET_CC_NUMBER_UNUSED || reduce == 0U)
        return;

    if (!MidiMonitor_TryGetLatestControlValueAnySource(channel,
                                                       cc_number,
                                                       &current_value))
    {
        return;
    }

    if (current_value <= threshold)
        return;

    tapered_value = (current_value > reduce) ? (uint8_t)(current_value - reduce) : 0U;
    if (tapered_value == current_value)
        return;

    if (!MIDI_SendCC(channel, cc_number, tapered_value)
     && midi_producer_feedback_taper_drop_count < UINT32_MAX)
    {
        midi_producer_feedback_taper_drop_count++;
    }
}

static void Midi_ApplyFeedbackTaperForBypassedDevice(uint8_t device_index, uint8_t program)
{
    const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();
    const RuntimeConfigDevice_t *device;

    if (program != PRESET_PROGRAM_NONE || !global || !global->feedback_taper_enabled)
        return;

    device = RuntimeConfig_GetDevice(device_index);
    if (!device)
        return;

    Midi_MaybeSendFeedbackTaperCc(device->channel,
                                  device->decay1.cc,
                                  global->feedback_taper_threshold,
                                  global->feedback_taper_reduce);

    if (device->decay2.cc != device->decay1.cc)
    {
        Midi_MaybeSendFeedbackTaperCc(device->channel,
                                      device->decay2.cc,
                                      global->feedback_taper_threshold,
                                      global->feedback_taper_reduce);
    }
}

/* ── MIDI_SendProgramChange ──────────────────────────────────────────────────
 * Sends a 2-byte Program Change message:
 *   Byte 0:  0xC0 | (channel-1)   — status byte, upper nibble 0xC = Program Change
 *   Byte 1:  program & 0x7F       — program number (7-bit, 0–127)
 *
 * Enqueue is non-blocking: returns immediately if the output queue is full.
 * ─────────────────────────────────────────────────────────────────────────── */
/**
 * Queues a MIDI Program Change for one channel.
 */
uint8_t MIDI_SendProgramChange(uint8_t channel, uint8_t program)
{
    if (!midi_channel_is_valid(channel))
        return 0U;

    uint8_t msg[2] = {
        (uint8_t)(MIDI_PROGRAM_CHANGE_STATUS | ((channel - MIDI_CHANNEL_FIRST) & MIDI_CHANNEL_STATUS_MASK)),  /* channel 1-16 → nibble 0-15 */
        (uint8_t)(program & MIDI_DATA_MASK),                                                 /* mask to 7-bit MIDI data range */
    };
    return midi_output_send_bytes(msg, (uint16_t)sizeof(msg));
}

uint8_t MIDI_SendProgramChangeTracked(uint8_t channel,
                                      uint8_t program,
                                      uint32_t sequence)
{
    if (!midi_channel_is_valid(channel) || sequence == 0U)
        return 0U;

    uint8_t msg[2] = {
        (uint8_t)(MIDI_PROGRAM_CHANGE_STATUS
                  | ((channel - MIDI_CHANNEL_FIRST) & MIDI_CHANNEL_STATUS_MASK)),
        (uint8_t)(program & MIDI_DATA_MASK),
    };
    return midi_output_send_tracked_bytes(msg, (uint16_t)sizeof(msg), sequence);
}

/* ── MIDI_SendCC ─────────────────────────────────────────────────────────────
 * Sends a 3-byte Control Change message:
 *   Byte 0:  0xB0 | (channel-1)   — status byte, 0xB = Control Change
 *   Byte 1:  cc_number & 0x7F     — which controller (e.g. 60 = engage/bypass)
 *   Byte 2:  value & 0x7F         — controller value  (e.g. 127 = on, 0 = off)
 *
 * See midi_devices.c for the CC numbers used by each pedal.
 * Enqueue is non-blocking: returns immediately if the output queue is full.
 * ─────────────────────────────────────────────────────────────────────────── */
/**
 * Queues a MIDI Control Change for one channel.
 */
uint8_t MIDI_SendCC(uint8_t channel, uint8_t cc_number, uint8_t value)
{
    if (!midi_channel_is_valid(channel))
        return 0U;

    uint8_t msg[3] = {
        (uint8_t)(MIDI_CONTROL_CHANGE_STATUS | ((channel - MIDI_CHANNEL_FIRST) & MIDI_CHANNEL_STATUS_MASK)),
        (uint8_t)(cc_number & MIDI_DATA_MASK),
        (uint8_t)(value & MIDI_DATA_MASK),
    };
    return midi_output_send_bytes(msg, (uint16_t)sizeof(msg));
}

uint8_t MIDI_SendCCTracked(uint8_t channel,
                           uint8_t cc_number,
                           uint8_t value,
                           uint32_t sequence)
{
    if (!midi_channel_is_valid(channel) || sequence == 0U)
        return 0U;

    uint8_t msg[3] = {
        (uint8_t)(MIDI_CONTROL_CHANGE_STATUS
                  | ((channel - MIDI_CHANNEL_FIRST) & MIDI_CHANNEL_STATUS_MASK)),
        (uint8_t)(cc_number & MIDI_DATA_MASK),
        (uint8_t)(value & MIDI_DATA_MASK),
    };
    return midi_output_send_tracked_bytes(msg, (uint16_t)sizeof(msg), sequence);
}

/**
 * Sends all programmed CC values from one preset.
 */
static uint8_t Midi_SendPresetCCsInternal(const Preset_t *preset)
{
    uint8_t all_sent = 1U;

    if (!preset)
        return 0U;

    for (uint8_t i = 0U; i < PRESET_CC_SLOT_COUNT; i++)
    {
        const PresetCCSlot_t *cc = &preset->cc[i];

        if (cc->channel == PRESET_CC_CHANNEL_UNUSED || cc->cc_number == PRESET_CC_NUMBER_UNUSED || cc->value == PRESET_CC_VALUE_UNUSED)
            continue;

        if (!MIDI_SendCC(cc->channel, cc->cc_number, cc->value))
            all_sent = 0U;
    }

    return all_sent;
}

static uint8_t Midi_PresetProgramIsActive(uint8_t program)
{
    return (program == PRESET_PROGRAM_NONE) ? 0U : 1U;
}

static uint8_t Midi_SendDeviceAutoCcMessages(const PresetCCSlot_t *messages)
{
    uint8_t all_sent = 1U;

    if (!messages)
        return 1U;

    for (uint8_t index = 0U; index < RUNTIME_CONFIG_DEVICE_AUTO_CC_COUNT; ++index)
    {
        const PresetCCSlot_t *message = &messages[index];

        if (message->channel == PRESET_CC_CHANNEL_UNUSED
         || message->cc_number == PRESET_CC_NUMBER_UNUSED
         || message->value == PRESET_CC_VALUE_UNUSED)
        {
            continue;
        }

        if (!MIDI_SendCC(message->channel, message->cc_number, message->value))
            all_sent = 0U;
    }

    return all_sent;
}

static uint8_t Midi_SendDeviceStateTransitionAutos(uint8_t device_index,
                                                   uint8_t previous_program,
                                                   uint8_t program)
{
    const RuntimeConfigDevice_t *device;
    uint8_t previous_active;
    uint8_t next_active;

    if (device_index >= MIDI_DEVICE_COUNT)
        return 1U;

    previous_active = Midi_PresetProgramIsActive(previous_program);
    next_active = Midi_PresetProgramIsActive(program);
    if (previous_active == next_active)
        return 1U;

    device = RuntimeConfig_GetDevice(device_index);
    if (!device)
        return 1U;

    return Midi_SendDeviceAutoCcMessages(next_active ? device->active_auto_cc
                                                     : device->bypass_auto_cc);
}

void Midi_SendPresetCCs(const Preset_t *preset)
{
    (void)Midi_SendPresetCCsInternal(preset);
}

/**
 * Sends the program or bypass state for one configured device slot.
 */
static uint8_t Midi_SendDeviceProgramSlotInternal(uint8_t device_index, uint8_t program)
{
    const MidiDevice_t *dev = MidiDevices_Get(device_index);
    uint8_t all_sent = 1U;

    if (!dev)
        return 0U;

    if (program == PRESET_PROGRAM_NONE)
    {
        if (dev->bypass.cc != PRESET_CC_NUMBER_UNUSED)
            all_sent = MIDI_SendCC(dev->channel, dev->bypass.cc, dev->bypass.value);
        return all_sent;
    }

    all_sent = MIDI_SendProgramChange(dev->channel, program);
    if (dev->engage.cc != PRESET_CC_NUMBER_UNUSED)
        all_sent = (uint8_t)(MIDI_SendCC(dev->channel, dev->engage.cc, dev->engage.value) && all_sent);

    return all_sent;
}

static uint8_t Midi_SendDeviceProgramSlotTransitionInternal(uint8_t device_index,
                                                           uint8_t previous_program,
                                                           uint8_t program)
{
    uint8_t all_sent = Midi_SendDeviceProgramSlotInternal(device_index, program);

    if (!Midi_SendDeviceStateTransitionAutos(device_index, previous_program, program))
        all_sent = 0U;

    return all_sent;
}

void Midi_SendDeviceProgramSlot(uint8_t device_index, uint8_t program)
{
    (void)Midi_SendDeviceProgramSlotInternal(device_index, program);
}

void Midi_SendDeviceProgramSlotTransition(uint8_t device_index,
                                          uint8_t previous_program,
                                          uint8_t program)
{
    (void)Midi_SendDeviceProgramSlotTransitionInternal(device_index, previous_program, program);
}

/**
 * Sends the full preset state to every configured MIDI device.
 */
static uint8_t Midi_LoadPresetInternal(const Preset_t *preset,
                                       const Preset_t *previous_preset,
                                       uint8_t send_auto_transitions,
                                       uint8_t allow_retry_schedule,
                                       uint8_t urgent_retry)
{
    uint8_t all_sent = 1U;

    if (!preset)
        return 0U;

    /* Program changes are sent first so devices switch base patches before any
     * follow-up CCs try to tweak parameters on the newly selected preset. */
    for (uint8_t i = 0U; i < PRESET_DEVICE_SLOTS; i++)
    {
        uint8_t program = preset->prg[i].program;
        uint8_t previous_program = previous_preset ? previous_preset->prg[i].program : PRESET_PROGRAM_NONE;

        if (send_auto_transitions)
        {
            if (!Midi_SendDeviceProgramSlotTransitionInternal(i, previous_program, program))
                all_sent = 0U;
        }
        else if (!Midi_SendDeviceProgramSlotInternal(i, program))
        {
            all_sent = 0U;
        }
        Midi_ApplyFeedbackTaperForBypassedDevice(i, program);
    }

    if (!Midi_SendPresetCCsInternal(preset))
        all_sent = 0U;

    if (!all_sent && allow_retry_schedule)
    {
        midi_pending_retry_preset = preset;
        midi_pending_retry_attempts_remaining = MIDI_PRESET_RETRY_MAX_ATTEMPTS;
        midi_pending_retry_urgent = urgent_retry ? 1U : 0U;
    }

    return all_sent;
}

void Midi_LoadPreset(const Preset_t *preset)
{
    if (!preset)
        return;

    Midi_ClearPendingPresetRetry();
    (void)Midi_LoadPresetInternal(preset, NULL, 0U, 1U, 0U);
}

void Midi_LoadPresetUrgent(const Preset_t *preset)
{
    if (!preset)
        return;

    Midi_ClearPendingPresetRetry();

    (void)Midi_LoadPresetInternal(preset, NULL, 0U, 1U, 1U);
}

void Midi_LoadPresetTransition(const Preset_t *preset, const Preset_t *previous_preset)
{
    if (!preset)
        return;

    Midi_ClearPendingPresetRetry();
    (void)Midi_LoadPresetInternal(preset, previous_preset, 1U, 1U, 0U);
}

void Midi_LoadPresetTransitionUrgent(const Preset_t *preset, const Preset_t *previous_preset)
{
    if (!preset)
        return;

    Midi_ClearPendingPresetRetry();
    (void)Midi_LoadPresetInternal(preset, previous_preset, 1U, 1U, 1U);
}

static void Midi_ClearPendingPresetRetry(void)
{
    midi_pending_retry_preset = NULL;
    midi_pending_retry_attempts_remaining = 0U;
    midi_pending_retry_urgent = 0U;
}
