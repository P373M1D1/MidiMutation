#ifndef MIDI_FUNCTIONS_H
#define MIDI_FUNCTIONS_H /* include guard for MIDI I/O and clock declarations */

/*
 * Two outgoing MIDI roles are used now:
 *   - USART2 TX acts as a soft-thru copy of whatever arrives on USART2 RX.
 *   - the board-startup layer owns one separate, controller-managed MIDI OUT UART whose
 *     Program Change, CC, and clock-only traffic is addressed by MIDI channel.
 *
 * Physical connection for each MIDI-out jack (5-pin DIN or TRS-A):
 *   UART_TX  → 220 Ω → MIDI OUT pin 5
 *   3.3 V    → 220 Ω → MIDI OUT pin 4
 *   GND               → MIDI OUT pin 2
 *
 * Usage:
 *   MidiInitInput();              // USART2 RX + TX soft-thru
 *   AppBoard_InitStartupPeripherals();
 *   MIDI_SendCC(channel, cc, value);
 */

#include <stdint.h>
#include "midi/midi_monitor.h"
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

typedef enum
{
    MIDI_CLOCK_RECOVERY_HINT_NONE = 0,
    MIDI_CLOCK_RECOVERY_HINT_PHASE_DELTA,
    MIDI_CLOCK_RECOVERY_HINT_BPM_DELTA,
} MidiClockRecoveryHint_t;

typedef enum
{
    MIDI_CLOCK_LOCK_QUALITY_NONE = 0,
    MIDI_CLOCK_LOCK_QUALITY_ACQUIRING,
    MIDI_CLOCK_LOCK_QUALITY_LOCKED,
} MidiClockLockQuality_t;

typedef enum
{
    MIDI_TRANSPORT_PHASE_SOURCE_NONE = 0,
    MIDI_TRANSPORT_PHASE_SOURCE_INTERNAL,
    MIDI_TRANSPORT_PHASE_SOURCE_EXTERNAL,
} MidiTransportPhaseSource_t;

typedef struct
{
    uint32_t tick_count;
    uint16_t tick_fraction_q16;
    MidiTransportPhaseSource_t source;
} MidiTransportPhaseSnapshot_t;

typedef struct
{
    uint16_t current_depth;
    uint16_t interval_peak_depth;
    uint16_t lifetime_peak_depth;
    uint32_t total_enqueued_count;
    uint32_t total_dropped_count;
    uint32_t interval_dropped_count;
    uint32_t interval_latency_average_us;
    uint32_t interval_latency_max_us;
    uint16_t interval_latency_sample_count;
} MidiInputRealtimeRxDiagnostics_t;

/**
 * @brief  Initialise MIDI input on USART2 and enable its soft-thru output.
 *         RX bytes are echoed on USART2 TX while the parser still filters
 *         sync traffic for the firmware's own clock handling.
 */
void MidiInitInput(void);

/**
 * @brief  Register the one UART handle used for all outgoing MIDI traffic.
 *         The board-startup layer remains responsible for configuring the UART and GPIO.
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
 * @brief  Drain timestamped realtime MIDI bytes captured by the USART2 IRQ.
 *         Transport interpretation runs here in foreground context using the
 *         original TIM2 capture timestamp from IRQ time.
 */
void MidiInput_ServiceRealtimeRx(void);

/**
 * @brief  Snapshot realtime RX queue diagnostics.
 *         Interval fields are measured since the previous snapshot.
 */
void MidiInput_TakeRealtimeRxDiagnostics(MidiInputRealtimeRxDiagnostics_t *diagnostics);

/**
 * @brief  Configure and start the internal MIDI clock output timer.
 * @param  bpm  Initial whole-number tempo in beats per minute.
 */
void MidiClockOutputInit(uint16_t bpm);

/**
 * @brief  Handle the shared TIM6/DAC IRQ for the internal MIDI clock.
 */
void MidiClockOutputIrqHandler(void);

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
 * @brief  Return 1 once the external clock estimator has enough timing data
 *         to operate meaningfully.
 */
uint8_t MidiClockIsEstimatorValid(void);

/**
 * @brief  Return the current external clock lock quality.
 */
MidiClockLockQuality_t MidiClockGetLockQuality(void);

/**
 * @brief  Return 1 once the external clock tempo is considered safe for
 *         downstream musical consumers to trust.
 */
uint8_t MidiClockIsPublicationReady(void);

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
 * @brief  Return the public external MIDI clock tempo.
 *         Prefers the recovered PLL tempo when it is valid and otherwise
 *         falls back to the stable in-range raw estimator tempo. This getter
 *         stays invalid until the external clock has produced a minimum
 *         settled observation window.
 * @param  bpm  Output pointer for the last valid external BPM.
 * @retval 1 if a valid external tempo is available, 0 otherwise.
 */
uint8_t MidiClockGetExternalBpm(uint16_t *bpm);

/**
 * @brief  Return the public external MIDI clock tempo in tenths.
 *         Prefers the recovered PLL tempo when it is valid and otherwise
 *         falls back to the stable in-range raw estimator tempo. This getter
 *         stays invalid until the external clock has produced a minimum
 *         settled observation window.
 * @param  bpm_x10  Output pointer for the last valid external BPM x10.
 * @retval 1 if a valid external tempo is available, 0 otherwise.
 */
uint8_t MidiClockGetExternalBpmX10(uint16_t *bpm_x10);

/**
 * @brief  Return the display-facing external MIDI clock tempo in tenths.
 *         Prefers the recovered PLL tempo when it is valid, and otherwise
 *         falls back to the latest measured estimator tempo so the UI can
 *         still show `LOW/HIGH` while external clock is present. This getter
 *         also waits for the minimum settled observation window.
 * @param  bpm_x10  Output pointer for the last measured BPM x10.
 * @retval 1 if a measured external tempo is available, 0 otherwise.
 */
uint8_t MidiClockGetMeasuredExternalBpmX10(uint16_t *bpm_x10);

/**
 * @brief  Return the current estimator window size in pulses.
 *         Intended for diagnostics so raw/source validity can be compared
 *         against the current external-clock observation window.
 * @retval Current external BPM estimator window in pulses.
 */
uint8_t MidiClockGetExternalBpmWindowPulses(void);

/**
 * @brief  Return the unfiltered external MIDI clock estimator tempo in tenths.
 *         Intended for diagnostics when comparing raw clock jitter against the
 *         recovered/public external tempo.
 * @param  bpm_x10  Output pointer for the raw estimator BPM x10.
 * @retval 1 if a valid raw estimator tempo is available, 0 otherwise.
 */
uint8_t MidiClockGetRawExternalBpmX10(uint16_t *bpm_x10);

/**
 * @brief  Report what kind of returned clock has been observed after a sync
 *         timeout while transport is still waiting for explicit rearm.
 * @retval MIDI_CLOCK_RECOVERY_HINT_NONE when no returned-clock verdict exists.
 */
MidiClockRecoveryHint_t MidiClockGetRecoveryHint(void);

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
 * @brief  Snapshot the continuous transport phase since the last Start or Continue.
 *         While externally synced the phase advances from the recovered clock
 *         estimator between incoming F8 pulses; otherwise it advances from the
 *         internal TIM6 pulse phase after handoff.
 * @param  phase  Output snapshot. `tick_fraction_q16` is the fractional part
 *                of the current tick in unsigned Q0.16 format.
 * @retval 1 when transport phase is anchored to a valid Start/Continue event,
 *         0 otherwise.
 */
uint8_t MidiTransportGetContinuousPhase(MidiTransportPhaseSnapshot_t *phase);

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
