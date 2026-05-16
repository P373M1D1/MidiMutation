#ifndef MIDI_FUNCTIONS_H
#define MIDI_FUNCTIONS_H /* include guard for MIDI I/O and clock declarations */

/*
 * Two outgoing MIDI roles are used now:
 *   - USART2 TX acts as a soft-thru copy of whatever arrives on USART2 RX.
 *   - main.cpp owns one separate, controller-managed MIDI OUT UART whose
 *     Program Change, CC, and clock-only traffic is addressed by MIDI channel.
 *
 * Physical connection for each MIDI-out jack (5-pin DIN or TRS-A):
 *   UART_TX  → 220 Ω → MIDI OUT pin 5
 *   3.3 V    → 220 Ω → MIDI OUT pin 4
 *   GND               → MIDI OUT pin 2
 *
 * Usage:
 *   MidiInitInput();              // USART2 RX + TX soft-thru
 *   MX_MIDI_Output_UART_Init();
 *   MidiSetOutputUart(&huart4);
 *   MIDI_SendCC(channel, cc, value);
 */

#include <stdint.h>
#include "presets.h"
#include "midi_devices.h"
#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** MIDI standard UART baud rate. */
#define MIDI_BAUD_RATE  31250U /* standard MIDI UART baud rate */

/** MIDI clock uses 24 pulses per quarter note. */
#define MIDI_CLOCK_PULSES_PER_QUARTER_NOTE  24U /* MIDI clock resolution defined by the standard */

typedef enum
{
    MIDI_TRANSPORT_EVENT_NONE = 0,
    MIDI_TRANSPORT_EVENT_START,
    MIDI_TRANSPORT_EVENT_CONTINUE,
    MIDI_TRANSPORT_EVENT_STOP,
} MidiTransportEvent_t;

#define MIDI_MONITOR_ENTRY_CAPACITY 50U
#define MIDI_MONITOR_VALUE_UNUSED   0xFFU

typedef enum
{
    MIDI_MONITOR_SOURCE_UART2 = 2,
    MIDI_MONITOR_SOURCE_UART4 = 4,
} MidiMonitorSource_t;

typedef enum
{
    MIDI_MONITOR_MESSAGE_PROGRAM_CHANGE = 0,
    MIDI_MONITOR_MESSAGE_CONTROL_CHANGE,
    MIDI_MONITOR_MESSAGE_START,
    MIDI_MONITOR_MESSAGE_CONTINUE,
    MIDI_MONITOR_MESSAGE_STOP,
} MidiMonitorMessageType_t;

typedef struct
{
    uint8_t source_uart;
    uint8_t channel;
    uint8_t type;
    uint8_t value1;
    uint8_t value2;
} MidiMonitorEntry_t;

/**
 * @brief  Initialise MIDI input on USART2 and enable its soft-thru output.
 *         RX bytes are echoed on USART2 TX while the parser still filters
 *         sync traffic for the firmware's own clock handling.
 */
void MidiInitInput(void);

/**
 * @brief  Clear the retained MIDI monitor history.
 */
void MidiMonitor_Clear(void);

/**
 * @brief  Return a monotonically increasing revision number for monitor data.
 *         This changes whenever the retained message list is mutated.
 */
uint32_t MidiMonitor_GetRevision(void);

/**
 * @brief  Copy the retained MIDI monitor history into caller storage.
 * @param  dest      Destination array for copied entries.
 * @param  capacity  Number of entries dest can hold.
 * @retval Number of entries copied, ordered oldest to newest.
 */
uint8_t MidiMonitor_CopyEntries(MidiMonitorEntry_t *dest, uint8_t capacity);

/**
 * @brief  Register the one UART handle used for all outgoing MIDI traffic.
 *         main.cpp remains responsible for configuring the UART and GPIO.
 * @param  uart_handle  Initialised HAL UART handle for the shared MIDI out.
 */
void MidiSetOutputUart(UART_HandleTypeDef *uart_handle);

/**
 * @brief  Send a Program Change message on the shared MIDI output.
 * @param  channel  MIDI channel, 1–16.
 * @param  program  Program number, 0–127.
 */
void MIDI_SendProgramChange(uint8_t channel, uint8_t program);

/**
 * @brief  Send a Control Change (CC) message on the shared MIDI output.
 * @param  channel    MIDI channel, 1–16.
 * @param  cc_number  Controller number, 0–127.
 * @param  value      Controller value, 0–127.
 */
void MIDI_SendCC(uint8_t channel, uint8_t cc_number, uint8_t value);

/**
 * @brief  Apply one preset program slot to a device.
 *         If program == PRESET_PROGRAM_NONE ("---"), sends the device bypass CC.
 *         Otherwise sends Program Change, then the device active/engage CC.
 * @param  device_index  Device slot index (0..MIDI_DEVICE_COUNT-1).
 * @param  program       Program number (0..127) or PRESET_PROGRAM_NONE.
 */
void Midi_SendDeviceProgramSlot(uint8_t device_index, uint8_t program);

/**
 * @brief  Consume one received MIDI byte from the dedicated MIDI input UART.
 *         Filters the input to sync traffic only: MIDI clock/transport and
 *         MIDI timecode quarter-frame bytes.
 * @param  byte  Raw MIDI byte from the UART receive register.
 */
void MidiReceive(uint8_t byte);

/**
 * @brief  Advance one internal MIDI-clock timer pulse.
 *         Sends an internal MIDI clock byte when no external clock is active.
 * @retval 1 when this pulse completed a quarter note, 0 otherwise.
 */
uint8_t MidiClockHandleInternalPulse(void);

/**
 * @brief  Return 1 while external MIDI transport is considered running.
 */
uint8_t MidiTransportIsRunning(void);

/**
 * @brief  Return 1 when external MIDI sync disappeared without a Stop event.
 */
uint8_t MidiClockIsSyncLost(void);

/**
 * @brief  Return 1 after an explicit MIDI Stop until Start/Continue or
 *         switching back to internal tempo clears that transport-stop latch.
 */
uint8_t MidiTransportStopLatched(void);

/**
 * @brief  Clear any external MIDI sync state and return to internal tempo.
 */
void MidiClockUseInternalTempo(void);

/**
 * @brief  Return the TIM6 ARR period value for one internal MIDI clock pulse.
 * @param  bpm  Whole-number tempo in beats per minute.
 */
uint32_t MidiClockOutputTimerPeriodForBpm(uint16_t bpm);

/**
 * @brief  Retune the internal MIDI clock output timer to the given BPM.
 * @param  bpm  Whole-number tempo in beats per minute.
 */
void MidiClockOutputSetTempoBpm(uint16_t bpm);

/**
 * @brief  Return the currently measured external MIDI clock tempo.
 * @param  bpm  Output pointer for the last valid measured BPM.
 * @retval 1 if a valid external tempo is available, 0 otherwise.
 */
uint8_t MidiClockGetExternalBpm(uint16_t *bpm);

/**
 * @brief  Return the currently measured external MIDI clock tempo in tenths.
 * @param  bpm_x10  Output pointer for the last valid measured BPM x10.
 * @retval 1 if a valid external tempo is available, 0 otherwise.
 */
uint8_t MidiClockGetExternalBpmX10(uint16_t *bpm_x10);

/**
 * @brief  Return 1 when external MIDI clock pulses are currently present.
 *         Unlike MidiTransportIsRunning(), this does not require a Start or
 *         Continue transport event.
 */
uint8_t MidiClockIsExternalSignalPresent(void);

/**
 * @brief  Get external transport bar/beat position tracked from MIDI clock.
 *         Position is anchored to 1.1 on Start/Continue and advances every
 *         quarter note (24 MIDI clock pulses), cycling 1.1 .. 4.4.
 * @param  bar   Output pointer for bar number (1..4).
 * @param  beat  Output pointer for beat number (1..4).
 * @retval 1 when a valid Start/Continue anchor exists, 0 otherwise.
 */
uint8_t MidiClockGetBarBeat(uint8_t *bar, uint8_t *beat);

/**
 * @brief  Re-arm the UART4 message scheduler once a safe gap opens between
 *         outgoing MIDI clock bytes.
 */
void MidiOutputSchedulerService(void);

/**
 * @brief  Emit a once-per-second clock diagnostic summary on the debug UART.
 *         Intended for loopback testing with UART4 MIDI OUT patched into MIDI IN.
 */
void MidiClockDiagnosticService(void);

/**
 * @brief  Return and clear the last latched transport event.
 * @retval MIDI_TRANSPORT_EVENT_NONE if no new Start/Continue/Stop arrived
 *         since the previous call.
 */
MidiTransportEvent_t MidiTransportConsumeEvent(void);

/**
 * @brief  Send Program Changes for all devices in a preset.
 *         Skips any device slot where program == 0xFF.
 */
void Midi_LoadPreset(const Preset_t *preset);

/**
 * @brief  Send all valid CC messages from a preset.
 *         Skips any CC slot with an unused channel, number, or value.
 */
void Midi_SendPresetCCs(const Preset_t *preset);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_FUNCTIONS_H */
