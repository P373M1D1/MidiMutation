#include "midi_functions.h"
#include "midi/midi_monitor.h"
#include "midi/midi_output.h"
#include "app/app_state.h"
#include "bpm_functions.h"
#include "led_functions.h"
#include "runtime_config.h"
#include <stdio.h>

void Error_Handler(void);

/* ── midi_functions.c ────────────────────────────────────────────────────────
 *
 * MIDI input/thru plus shared-UART MIDI TX driver.
 *
 * USART2 is full-duplex: RX is the dedicated MIDI input and TX acts as a
 * soft-thru copy of the raw incoming byte stream. main.cpp separately owns
 * the controller-managed MIDI-out UART and registers that handle here so
 * preset messages and generated/forwarded clock share one smart output.
 * Devices stay distinct on the smart output by MIDI channel, not by UART.
 *
 * To add a new device:
 *   1. Add a row to the device_table in midi_devices.c with its own channel.
 *   2. The preset_table in presets.c can then reference that device slot.
 *
 * MIDI standard: 31 250 baud, 8 data bits, no parity, 1 stop bit (8-N-1).
 * Input sync still arrives on USART2 RX and is handled separately below.
 * ─────────────────────────────────────────────────────────────────────────── */

static UART_HandleTypeDef midi_input_uart;
static TIM_HandleTypeDef midi_clock_output_timer;

/* MIDI wire-format and UART settings. Keep these values visible because they
 * come directly from the MIDI spec or from how this firmware represents BPM. */
#define MIDI_TX_TIMEOUT_MS                 10U          /* UART transmit timeout for short MIDI messages */
#define MIDI_UART_IRQ_PREEMPT_PRIORITY     2U           /* USART2 IRQ priority: above UI work, below critical timers */
#define MIDI_UART_IRQ_SUBPRIORITY          1U           /* secondary ordering for the MIDI input IRQ */
#define MIDI_CHANNEL_FIRST                 1U           /* MIDI channels are encoded as 1..16 in public APIs */
#define MIDI_CHANNEL_LAST                  16U          /* highest valid MIDI channel number */
#define MIDI_CHANNEL_STATUS_MASK           0x0FU        /* low nibble of channel voice status bytes */
#define MIDI_DATA_MASK                     0x7FU        /* MIDI data bytes are always 7-bit */
#define MIDI_STATUS_BIT                    0x80U        /* distinguishes status bytes from data bytes */
#define MIDI_PROGRAM_CHANGE_STATUS         0xC0U        /* status nibble for Program Change */
#define MIDI_CONTROL_CHANGE_STATUS         0xB0U        /* status nibble for Control Change */
#define MIDI_TIMECODE_QUARTER_FRAME        0xF1U        /* system-common quarter-frame timecode status */
#define MIDI_REALTIME_CLOCK                0xF8U        /* realtime MIDI clock pulse */
#define MIDI_REALTIME_START                0xFAU        /* realtime transport start */
#define MIDI_REALTIME_CONTINUE             0xFBU        /* realtime transport continue */
#define MIDI_REALTIME_STOP                 0xFCU        /* realtime transport stop */
#define MIDI_REALTIME_STATUS_FIRST         0xF8U        /* first status value in the realtime-byte range */
#define MIDI_UNUSED_SLOT                   0xFFU        /* sentinel meaning "do not send anything" */
#define MIDI_TIMER_WRAP_VALUE              UINT32_MAX   /* TIM2 free-running 32-bit wrap value */
#define MIDI_CLOCK_US_PER_MS               1000U        /* unit conversion used for timeout math */
#define MIDI_CLOCK_US_PER_MINUTE_X10       600000000ULL /* 60 s/min expressed in microseconds and tenths of BPM */
#define MIDI_CLOCK_BPM_X10_MIN             200U         /* reject external BPM below 20.0 */
#define MIDI_CLOCK_BPM_X10_MAX             2400U        /* reject external BPM above 240.0 */
#define MIDI_BPM_X10_ROUNDING_OFFSET       5U           /* convert x10 BPM to integer BPM with round-half-up */

#define MIDI_CLOCK_BPM_WINDOW_PULSES 96U               /* average over four quarter notes at 24 ppqn */
#define MIDI_BARBEAT_BEATS_PER_BAR 4U                  /* quarter-note beats per bar */
#define MIDI_CLOCK_LOST_TIMEOUT_MIN_MS 250U            /* never declare sync lost faster than this */
#define MIDI_CLOCK_LOST_TIMEOUT_PAD_MS 20U             /* extra slack on top of the computed timeout */
#define MIDI_CLOCK_LOST_TIMEOUT_PULSES 4U              /* allow roughly four missing clock pulses before loss */
#define MIDI_THRU_BUFFER_SIZE 64U                      /* ring buffer size for non-blocking software thru */
#define MIDI_CLOCK_DIAGNOSTICS_ENABLED 1U              /* prints interval stats on USART3 so MIDI OUT can be checked with a loopback cable */
#define MIDI_CLOCK_LOOPBACK_MONITOR_ONLY 1U            /* measure looped-back UART4 clock on USART2 without letting it discipline the output engine */
#define MIDI_CLOCK_DIAGNOSTIC_REPORT_MS 1000U          /* report one rolling diagnostic line per second */
#define MIDI_CLOCK_OUTPUT_TIMER_TICK_HZ 100000U        /* TIM6 counter rate used for internal MIDI clock output */
#define MIDI_CLOCK_OUTPUT_COUNTS_PER_MINUTE (MIDI_CLOCK_OUTPUT_TIMER_TICK_HZ * 60U) /* one minute of TIM6 counts at the configured output tick rate */
#define MIDI_CLOCK_OUTPUT_TIMER_PRESCALER_DIVISOR 960U /* 96 MHz APB1 timer clock divided down to 100 kHz for TIM6 */
#define MIDI_CLOCK_OUTPUT_IRQ_PREEMPT_PRIORITY 2U      /* MIDI clock timer must preempt UI/input work but not dedicated UART4 TXE service */
#define MIDI_CLOCK_OUTPUT_IRQ_SUBPRIORITY 0U           /* no secondary ordering needed for TIM6 */

/* Clock-tracking fields are written from the USART2 IRQ path and read from
 * foreground code, so the shared timing state stays in this file and uses
 * volatile where foreground code can observe asynchronous updates. */
static uint8_t            midi_thru_buffer[MIDI_THRU_BUFFER_SIZE];
static uint8_t            midi_thru_head = 0U;
static uint8_t            midi_thru_tail = 0U;
static uint8_t            midi_internal_clock_pulse_count = 0U;
static uint8_t            midi_clock_pulse_count = 0U;
static volatile uint32_t  midi_clock_last_pulse_us = 0U;
static volatile uint32_t  midi_clock_diag_interval_sum_us = 0U;
static volatile uint32_t  midi_clock_diag_interval_min_us = UINT32_MAX;
static volatile uint32_t  midi_clock_diag_interval_max_us = 0U;
static volatile uint16_t  midi_clock_diag_interval_count = 0U;
static uint32_t           midi_clock_pulse_intervals_us[MIDI_CLOCK_BPM_WINDOW_PULSES];
static volatile uint32_t  midi_clock_pulse_interval_sum_us = 0U;
static volatile uint8_t   midi_clock_pulse_interval_count = 0U;
static uint8_t            midi_clock_pulse_interval_index = 0U;
static volatile uint32_t  midi_clock_external_activity_timeout_us = 0U;
static volatile uint16_t  midi_clock_external_bpm_x10 = 0U;
static volatile uint8_t   midi_clock_external_bpm_valid = 0U;
static volatile uint8_t   midi_barbeat_valid = 0U;
static volatile uint8_t   midi_barbeat_bar = 1U;
static volatile uint8_t   midi_barbeat_beat = 1U;
static volatile uint8_t   midi_clock_sync_lost = 0U;
static volatile uint8_t   midi_transport_running = 0U;
static volatile uint8_t   midi_transport_stop_latched = 0U;
static volatile MidiTransportEvent_t midi_transport_event = MIDI_TRANSPORT_EVENT_NONE;
static uint8_t            midi_input_expect_timecode_data = 0U;
static void               midi_clock_reset_sync(void);
static void               midi_clock_get_timing_snapshot(uint32_t *last_pulse_us,
                                                         uint32_t *pulse_interval_sum_us,
                                                         uint8_t *pulse_interval_count);
static uint32_t           midi_clock_compute_activity_timeout_us(uint32_t pulse_interval_sum_us,
                                                                 uint8_t pulse_interval_count);
static void               midi_clock_update_sync_state(void);
static void               midi_uart_apply_standard_config(UART_HandleTypeDef *uart_handle,
                                                          USART_TypeDef *instance,
                                                          uint32_t mode);
static uint8_t            midi_channel_is_valid(uint8_t channel);
static void               midi_input_queue_thru_byte(uint8_t byte);
static void               midi_input_service_thru_tx(void);
static uint8_t            midi_clock_external_is_active(void);
static void               midi_clock_diagnostics_note_interval(uint32_t interval_us);
static void               midi_output_send_bytes(const uint8_t *bytes, uint16_t length);
static void               midi_output_send_realtime_byte(uint8_t byte);
static uint8_t            midi_input_is_sync_byte(uint8_t byte);
static uint8_t            midi_clock_get_bars_per_cycle(void);
#if !MIDI_CLOCK_LOOPBACK_MONITOR_ONLY
static uint32_t           midi_clock_output_counts_for_pulse_interval_us(uint32_t pulse_interval_us);
static void               midi_clock_output_apply_pulse_counts(uint32_t pulse_counts);
static void               midi_clock_output_reset_phase(void);
static void               midi_clock_output_track_external_pulse(uint32_t interval_us);
#else
static void               midi_clock_output_apply_pulse_counts(uint32_t pulse_counts);
#endif

void MidiClockOutputInit(uint16_t bpm)
{
    __HAL_RCC_TIM6_CLK_ENABLE();
    midi_clock_output_timer.Instance = TIM6;
    midi_clock_output_timer.Init.Prescaler = MIDI_CLOCK_OUTPUT_TIMER_PRESCALER_DIVISOR - 1U;
    midi_clock_output_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    midi_clock_output_timer.Init.Period = MidiClockOutputTimerPeriodForBpm(bpm);
    midi_clock_output_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&midi_clock_output_timer) != HAL_OK)
        Error_Handler();

    __HAL_TIM_CLEAR_FLAG(&midi_clock_output_timer, TIM_FLAG_UPDATE);
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn,
                         MIDI_CLOCK_OUTPUT_IRQ_PREEMPT_PRIORITY,
                         MIDI_CLOCK_OUTPUT_IRQ_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);

    if (HAL_TIM_Base_Start_IT(&midi_clock_output_timer) != HAL_OK)
        Error_Handler();
}

void MidiClockOutputIrqHandler(void)
{
    if (TIM6->SR & TIM_SR_UIF)
    {
        TIM6->SR = ~TIM_SR_UIF;
        if (MidiClockHandleInternalPulse() && !MidiClockIsExternalSignalPresent())
            LED_BeatPulse();
    }

    if (DAC->SR & (DAC_SR_DMAUDR1 | DAC_SR_DMAUDR2))
        DAC->SR |= (DAC_SR_DMAUDR1 | DAC_SR_DMAUDR2);
}

static uint8_t midi_clock_get_bars_per_cycle(void)
{
    const RuntimeConfigBank_t *bank = RuntimeConfig_GetBank(current_bank);
    uint8_t bars_per_cycle = bank->midi_clock_bar_count;

    if (bars_per_cycle < RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MIN
     || bars_per_cycle > RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_MAX)
        return RUNTIME_CONFIG_MIDI_CLOCK_BAR_COUNT_DEFAULT;

    return bars_per_cycle;
}

static void midi_uart_apply_standard_config(UART_HandleTypeDef *uart_handle,
                                            USART_TypeDef *instance,
                                            uint32_t mode)
{
    /* TX and RX paths share the same 31.25 kbaud, 8-N-1 framing; only the
     * enabled direction differs between dedicated output ports and MIDI input. */
    uart_handle->Instance          = instance;
    uart_handle->Init.BaudRate     = MIDI_BAUD_RATE;
    uart_handle->Init.WordLength   = UART_WORDLENGTH_8B;
    uart_handle->Init.StopBits     = UART_STOPBITS_1;
    uart_handle->Init.Parity       = UART_PARITY_NONE;
    uart_handle->Init.Mode         = mode;
    uart_handle->Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    uart_handle->Init.OverSampling = UART_OVERSAMPLING_16;
}

void MidiSetOutputUart(UART_HandleTypeDef *uart_handle)
{
    MidiOutput_SetUart(uart_handle);
}

void MidiInitInput(void)
{
    midi_uart_apply_standard_config(&midi_input_uart, USART2, UART_MODE_TX_RX);
    if (HAL_UART_Init(&midi_input_uart) != HAL_OK)
    {
        Error_Handler();
    }

    midi_thru_head = 0U;
    midi_thru_tail = 0U;
    MidiMonitor_Init();
    HAL_NVIC_SetPriority(USART2_IRQn, MIDI_UART_IRQ_PREEMPT_PRIORITY, MIDI_UART_IRQ_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    __HAL_UART_ENABLE_IT(&midi_input_uart, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&midi_input_uart, UART_IT_ERR);
    __HAL_UART_DISABLE_IT(&midi_input_uart, UART_IT_TXE);
    midi_clock_reset_sync();
    midi_transport_stop_latched = 0U;
}

static uint8_t midi_channel_is_valid(uint8_t channel)
{
    return (uint8_t)(channel >= MIDI_CHANNEL_FIRST && channel <= MIDI_CHANNEL_LAST);
}

static uint32_t midi_clock_compute_activity_timeout_us(uint32_t pulse_interval_sum_us,
                                                       uint8_t pulse_interval_count)
{
    /* No clock history yet: fall back to a conservative fixed timeout. Once
     * we have intervals, scale the timeout with the measured tempo so fast and
     * slow songs both get a sensible sync-loss window. */
    uint32_t timeout_us = (uint32_t)MIDI_CLOCK_LOST_TIMEOUT_MIN_MS * MIDI_CLOCK_US_PER_MS;

    if (pulse_interval_count > 0U)
    {
        uint32_t average_pulse_us = pulse_interval_sum_us / (uint32_t)pulse_interval_count;
        timeout_us = average_pulse_us * MIDI_CLOCK_LOST_TIMEOUT_PULSES;
        timeout_us += (uint32_t)MIDI_CLOCK_LOST_TIMEOUT_PAD_MS * MIDI_CLOCK_US_PER_MS;

        uint32_t min_timeout_us = (uint32_t)MIDI_CLOCK_LOST_TIMEOUT_MIN_MS * MIDI_CLOCK_US_PER_MS;
        if (timeout_us < min_timeout_us)
            timeout_us = min_timeout_us;
    }

    return timeout_us;
}

static void midi_input_queue_thru_byte(uint8_t byte)
{
    uint8_t next_head = (uint8_t)((midi_thru_head + 1U) % MIDI_THRU_BUFFER_SIZE);

    if (next_head == midi_thru_tail)
    {
        /* Soft-thru must never stall the receive IRQ; if the buffer ever fills,
         * drop the newest byte rather than blocking clock capture. */
        return;
    }

    midi_thru_buffer[midi_thru_head] = byte;
    midi_thru_head = next_head;
    __HAL_UART_ENABLE_IT(&midi_input_uart, UART_IT_TXE);
}

static void midi_input_service_thru_tx(void)
{
    /* TXE fires whenever the data register can accept another byte. Drain the
     * ring buffer one byte at a time so the IRQ-driven thru path never blocks
     * foreground code or the receive side. */
    if (midi_thru_tail == midi_thru_head)
    {
        __HAL_UART_DISABLE_IT(&midi_input_uart, UART_IT_TXE);
        return;
    }

    midi_input_uart.Instance->DR = midi_thru_buffer[midi_thru_tail];
    midi_thru_tail = (uint8_t)((midi_thru_tail + 1U) % MIDI_THRU_BUFFER_SIZE);

    if (midi_thru_tail == midi_thru_head)
        __HAL_UART_DISABLE_IT(&midi_input_uart, UART_IT_TXE);
}

static uint8_t midi_clock_external_is_active(void)
{
    uint32_t last_pulse_us = midi_clock_last_pulse_us;

    /* While transport is explicitly running we treat external sync as active
     * even if the UI has not asked for a fresh BPM sample yet. */
    if (midi_transport_running)
        return 1U;

    if (last_pulse_us == 0U)
        return 0U;

    return (uint8_t)((TIM2->CNT - last_pulse_us) <= midi_clock_external_activity_timeout_us);
}

static void midi_clock_diagnostics_note_interval(uint32_t interval_us)
{
#if MIDI_CLOCK_DIAGNOSTICS_ENABLED
    if (interval_us == 0U)
        return;

    if (midi_clock_diag_interval_count == 0U || interval_us < midi_clock_diag_interval_min_us)
        midi_clock_diag_interval_min_us = interval_us;

    if (interval_us > midi_clock_diag_interval_max_us)
        midi_clock_diag_interval_max_us = interval_us;

    midi_clock_diag_interval_sum_us += interval_us;
    if (midi_clock_diag_interval_count < UINT16_MAX)
        midi_clock_diag_interval_count++;
#else
    (void)interval_us;
#endif
}

void MidiOutputSchedulerService(void)
{
    MidiOutput_ServiceScheduler();
}

static void midi_output_send_bytes(const uint8_t *bytes, uint16_t length)
{
    (void)MidiOutput_QueueMessageBytes(bytes, length);
}

static void midi_output_send_realtime_byte(uint8_t byte)
{
    (void)MidiOutput_QueueRealtimeByte(byte);
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

static void midi_clock_reset_sync(void)
{
    /* Clear both the moving-average window and the transport-facing flags so a
     * new START/CONTINUE/clock stream begins with clean timing history. */
    midi_clock_pulse_count = 0U;
    midi_clock_last_pulse_us = 0U;
    midi_clock_pulse_interval_sum_us = 0U;
    midi_clock_pulse_interval_count = 0U;
    midi_clock_pulse_interval_index = 0U;
    midi_clock_external_activity_timeout_us =
        (uint32_t)MIDI_CLOCK_LOST_TIMEOUT_MIN_MS * MIDI_CLOCK_US_PER_MS;
    for (uint8_t i = 0U; i < MIDI_CLOCK_BPM_WINDOW_PULSES; i++)
    {
        midi_clock_pulse_intervals_us[i] = 0U;
    }
    midi_clock_external_bpm_x10 = 0U;
    midi_clock_external_bpm_valid = 0U;
    midi_barbeat_valid = 0U;
    midi_barbeat_bar = 1U;
    midi_barbeat_beat = 1U;
    midi_clock_sync_lost = 0U;
}

static void midi_clock_get_timing_snapshot(uint32_t *last_pulse_us,
                                           uint32_t *pulse_interval_sum_us,
                                           uint8_t *pulse_interval_count)
{
    uint32_t primask = __get_PRIMASK();

    /* Take a self-consistent snapshot because the receive IRQ can update the
     * pulse timing fields while foreground code is checking sync state. */
    __disable_irq();
    *last_pulse_us = midi_clock_last_pulse_us;
    *pulse_interval_sum_us = midi_clock_pulse_interval_sum_us;
    *pulse_interval_count = midi_clock_pulse_interval_count;
    if (primask == 0U)
        __enable_irq();
}

static void midi_clock_update_sync_state(void)
{
    uint32_t timeout_us;
    uint32_t last_pulse_us;
    uint32_t pulse_interval_sum_us;
    uint8_t pulse_interval_count;

    /* Sync loss is only meaningful while external transport is considered
     * active. Once lost, the flag remains latched until a new external anchor
     * arrives (START/CONTINUE/clock after loss) or the user returns to internal tempo. */
    if (!midi_transport_running || midi_clock_sync_lost)
        return;

    midi_clock_get_timing_snapshot(&last_pulse_us, &pulse_interval_sum_us, &pulse_interval_count);
    if (last_pulse_us == 0U || pulse_interval_count == 0U)
        return;

    timeout_us = midi_clock_compute_activity_timeout_us(pulse_interval_sum_us, pulse_interval_count);

    uint32_t now_us = TIM2->CNT;
    if ((now_us - last_pulse_us) > timeout_us)
    {
        midi_transport_running = 0U;
        midi_clock_external_bpm_valid = 0U;
        midi_clock_sync_lost = 1U;
    }
}

static uint8_t midi_input_is_sync_byte(uint8_t byte)
{
    if (byte == MIDI_REALTIME_CLOCK ||
        byte == MIDI_REALTIME_START ||
        byte == MIDI_REALTIME_CONTINUE ||
        byte == MIDI_REALTIME_STOP)
    {
        return 1U;
    }

    if (byte == MIDI_TIMECODE_QUARTER_FRAME)
    {
        midi_input_expect_timecode_data = 1U;
        return 1U;
    }

    if (midi_input_expect_timecode_data)
    {
        /* MIDI realtime bytes may legally appear between the quarter-frame
         * status and its data byte, so ignore them without cancelling the
         * pending data-byte expectation. */
        if (byte >= MIDI_REALTIME_STATUS_FIRST)
            return 0U;

        midi_input_expect_timecode_data = 0U;
        return (uint8_t)((byte & MIDI_STATUS_BIT) == 0U);
    }

    return 0U;
}

void MidiReceive(uint8_t byte)
{
    uint32_t interval_us = 0U;

    /* This path accepts only sync-related bytes for timing/transport state.
     * Other MIDI content is still soft-thru forwarded by the USART2 IRQ path. */
    if (!midi_input_is_sync_byte(byte))
        return;

    if (byte == MIDI_REALTIME_START)
    {
        /* Start also acts as a fresh sync anchor: clear any stale averaging
         * history and treat the next clock pulse as the new first sample. */
        LED_MidiClockPulse(); // Immediately blink the red LED for the first beat
        midi_internal_clock_pulse_count = 0U;
#if !MIDI_CLOCK_LOOPBACK_MONITOR_ONLY
        midi_clock_output_reset_phase();
#endif
        midi_transport_running = 1U;
        midi_transport_stop_latched = 0U;
        midi_transport_event = MIDI_TRANSPORT_EVENT_START;
        LED_MidiInPulse();
        midi_clock_reset_sync();
        midi_barbeat_valid = 1U;
       
        return;
    }

    if (byte == MIDI_REALTIME_CONTINUE)
    {
        /* Continue resumes external transport but does not trust any old pulse
         * spacing history, so clock averaging restarts from scratch here too. */
        LED_MidiClockPulse();
        midi_internal_clock_pulse_count = 0U;
#if !MIDI_CLOCK_LOOPBACK_MONITOR_ONLY
        midi_clock_output_reset_phase();
#endif
        midi_transport_running = 1U;
        midi_transport_stop_latched = 0U;
        midi_transport_event = MIDI_TRANSPORT_EVENT_CONTINUE;
        LED_MidiInPulse();
        midi_clock_reset_sync();
        midi_barbeat_valid = 1U;
        return;
    }

    if (byte == MIDI_REALTIME_STOP)
    {
        /* Stop is authoritative: transport is no longer running, so clear all
         * external-clock state immediately instead of waiting for a timeout. */
        midi_transport_running = 0U;
        midi_transport_stop_latched = 1U;
        midi_transport_event = MIDI_TRANSPORT_EVENT_STOP;
        midi_clock_reset_sync();
        return;
    }

    if (byte != MIDI_REALTIME_CLOCK)
        return;

    /* Clock pulses update the moving-average window in microseconds. We keep
     * the average in "sum of recent pulse intervals" form because that is cheap
     * to maintain in the IRQ path and converts directly into BPM x10. */
    uint32_t now = TIM2->CNT;
    if (midi_clock_sync_lost)
    {
        midi_transport_running = 1U;
        midi_clock_reset_sync();
        midi_clock_last_pulse_us = now;
        midi_clock_external_activity_timeout_us =
            (uint32_t)MIDI_CLOCK_LOST_TIMEOUT_MIN_MS * MIDI_CLOCK_US_PER_MS;
#if !MIDI_CLOCK_LOOPBACK_MONITOR_ONLY
        midi_clock_output_reset_phase();
#endif
        return;
    }

    if (midi_clock_last_pulse_us == 0U)
    {
#if !MIDI_CLOCK_LOOPBACK_MONITOR_ONLY
        midi_clock_output_reset_phase();
#endif
    }
    else if (now != midi_clock_last_pulse_us)
    {
        /* TIM2 is a free-running 32-bit microsecond counter; handle natural
         * wrap-around so the interval logic never depends on resetting TIM2. */
        interval_us = (now >= midi_clock_last_pulse_us)
            ? (now - midi_clock_last_pulse_us)
            : (MIDI_TIMER_WRAP_VALUE - midi_clock_last_pulse_us + now + 1U);

        if (midi_clock_pulse_interval_count == MIDI_CLOCK_BPM_WINDOW_PULSES)
        {
            midi_clock_pulse_interval_sum_us -=
                midi_clock_pulse_intervals_us[midi_clock_pulse_interval_index];
        }
        else
        {
            midi_clock_pulse_interval_count++;
        }

        midi_clock_pulse_intervals_us[midi_clock_pulse_interval_index] = interval_us;
        midi_clock_pulse_interval_sum_us += interval_us;
        midi_clock_pulse_interval_index =
            (uint8_t)((midi_clock_pulse_interval_index + 1U) % MIDI_CLOCK_BPM_WINDOW_PULSES);

        if (midi_clock_pulse_interval_sum_us > 0U)
        {
            /* BPM x10 = (60,000,000 us/min * 10) * pulse_count / (24 MIDI clock
             * pulses per quarter note * summed pulse interval in microseconds). */
            uint64_t numerator = MIDI_CLOCK_US_PER_MINUTE_X10 * (uint64_t)midi_clock_pulse_interval_count;
            uint32_t denominator = MIDI_CLOCK_PULSES_PER_QUARTER_NOTE * midi_clock_pulse_interval_sum_us;
            uint32_t bpm_x10 = (uint32_t)((numerator + (uint64_t)(denominator / 2U)) / (uint64_t)denominator);

            if (bpm_x10 >= MIDI_CLOCK_BPM_X10_MIN && bpm_x10 <= MIDI_CLOCK_BPM_X10_MAX)
            {
                midi_clock_external_bpm_x10 = (uint16_t)bpm_x10;
                midi_clock_external_bpm_valid = 1U;
            }
            else
            {
                midi_clock_external_bpm_valid = 0U;
            }
        }

        midi_clock_diagnostics_note_interval(interval_us);
#if !MIDI_CLOCK_LOOPBACK_MONITOR_ONLY
        midi_clock_output_track_external_pulse(interval_us);
#endif
    }
    midi_clock_last_pulse_us = now;
    midi_clock_external_activity_timeout_us =
        midi_clock_compute_activity_timeout_us(midi_clock_pulse_interval_sum_us,
                                               midi_clock_pulse_interval_count);

    /* 24 realtime clock bytes = one quarter note, so pulse the LED on beats
     * rather than at the full MIDI clock rate. */
    midi_clock_pulse_count++;
    if (midi_clock_pulse_count < MIDI_CLOCK_PULSES_PER_QUARTER_NOTE)
        return;

    midi_clock_pulse_count = 0U;

    if (midi_barbeat_valid)
    {
        if (midi_barbeat_beat < MIDI_BARBEAT_BEATS_PER_BAR)
        {
            midi_barbeat_beat++;
        }
        else
        {
            uint8_t bars_per_cycle = midi_clock_get_bars_per_cycle();

            midi_barbeat_beat = 1U;
            midi_barbeat_bar = (midi_barbeat_bar < bars_per_cycle)
                ? (uint8_t)(midi_barbeat_bar + 1U)
                : 1U;
        }
    }

    LED_MidiClockPulse();
}

uint8_t MidiClockHandleInternalPulse(void)
{
    /* TIM6 is the only smart-output clock source. External sync disciplines
     * this timer, but outgoing F8 bytes always leave from the timer path. */
    midi_output_send_realtime_byte(MIDI_REALTIME_CLOCK);

    midi_internal_clock_pulse_count++;
    if (midi_internal_clock_pulse_count < MIDI_CLOCK_PULSES_PER_QUARTER_NOTE)
        return 0U;

    midi_internal_clock_pulse_count = 0U;
    return 1U;
}

uint8_t MidiTransportIsRunning(void)
{
    midi_clock_update_sync_state();
    return midi_transport_running;
}

uint8_t MidiClockIsSyncLost(void)
{
    midi_clock_update_sync_state();
    return midi_clock_sync_lost;
}

uint8_t MidiTransportStopLatched(void)
{
    return midi_transport_stop_latched;
}

uint32_t MidiClockOutputTimerPeriodForBpm(uint16_t bpm)
{
    uint32_t denominator = (uint32_t)bpm * MIDI_CLOCK_PULSES_PER_QUARTER_NOTE;
    uint32_t pulse_counts = (MIDI_CLOCK_OUTPUT_COUNTS_PER_MINUTE + (denominator / 2U)) / denominator;

    if (pulse_counts == 0U)
        pulse_counts = 1U;

    return pulse_counts - 1U;
}

void MidiClockOutputSetTempoBpm(uint16_t bpm)
{
    midi_clock_output_apply_pulse_counts(MidiClockOutputTimerPeriodForBpm(bpm) + 1U);
}

void MidiClockUseInternalTempo(void)
{
    midi_transport_running = 0U;
    midi_transport_stop_latched = 0U;
    midi_transport_event = MIDI_TRANSPORT_EVENT_NONE;
    midi_internal_clock_pulse_count = 0U;
    midi_clock_reset_sync();
}

uint8_t MidiClockGetExternalBpm(uint16_t *bpm)
{
    uint16_t bpm_x10;

    if (!bpm || !MidiClockGetExternalBpmX10(&bpm_x10))
        return 0U;

    *bpm = (uint16_t)((bpm_x10 + MIDI_BPM_X10_ROUNDING_OFFSET) / 10U);
    return 1U;
}

uint8_t MidiClockGetExternalBpmX10(uint16_t *bpm_x10)
{
    midi_clock_update_sync_state();

    if (!bpm_x10 || !midi_clock_external_bpm_valid || !midi_clock_external_is_active())
        return 0U;

    *bpm_x10 = midi_clock_external_bpm_x10;
    return 1U;
}

uint8_t MidiClockIsExternalSignalPresent(void)
{
    midi_clock_update_sync_state();
    return midi_clock_external_is_active();
}

uint8_t MidiClockGetBarBeat(uint8_t *bar, uint8_t *beat)
{
    uint32_t primask = __get_PRIMASK();
    uint8_t valid;

    if (!bar || !beat)
        return 0U;

    __disable_irq();
    valid = midi_barbeat_valid;
    *bar = midi_barbeat_bar;
    *beat = midi_barbeat_beat;
    if (primask == 0U)
        __enable_irq();

    return valid;
}

static void midi_clock_output_apply_pulse_counts(uint32_t pulse_counts)
{
    uint32_t primask = __get_PRIMASK();

    if (pulse_counts == 0U)
        pulse_counts = 1U;

    __disable_irq();
    midi_clock_output_timer.Instance->ARR = pulse_counts - 1U;
    midi_clock_output_timer.Instance->CNT = 0U;
    if (primask == 0U)
        __enable_irq();
}

#if !MIDI_CLOCK_LOOPBACK_MONITOR_ONLY
static uint32_t midi_clock_output_counts_for_pulse_interval_us(uint32_t pulse_interval_us)
{
    uint64_t pulse_counts;

    if (pulse_interval_us == 0U)
        return 1U;

    pulse_counts = (((uint64_t)MIDI_CLOCK_OUTPUT_TIMER_TICK_HZ * (uint64_t)pulse_interval_us) + 500000ULL) / 1000000ULL;
    if (pulse_counts == 0U)
        pulse_counts = 1U;
    else if (pulse_counts > 0x10000ULL)
        pulse_counts = 0x10000ULL;

    return (uint32_t)pulse_counts;
}

static void midi_clock_output_reset_phase(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    midi_clock_output_timer.Instance->CNT = 0U;
    if (primask == 0U)
        __enable_irq();
}

static void midi_clock_output_track_external_pulse(uint32_t interval_us)
{
    if (interval_us != 0U)
    {
        uint64_t denominator = (uint64_t)interval_us * (uint64_t)MIDI_CLOCK_PULSES_PER_QUARTER_NOTE;
        uint32_t external_bpm = (uint32_t)((60000000ULL + (denominator / 2ULL)) / denominator);

        if (external_bpm >= BPM_MIN && external_bpm <= BPM_MAX)
            AppState_SetTempoBpm((uint16_t)external_bpm);
    }

    midi_clock_output_apply_pulse_counts(midi_clock_output_counts_for_pulse_interval_us(interval_us));
}
#endif

void MidiClockDiagnosticService(void)
{
#if MIDI_CLOCK_DIAGNOSTICS_ENABLED
    static uint32_t last_report_tick = 0U;
    uint32_t now = HAL_GetTick();
    uint32_t sum_us;
    uint32_t min_us;
    uint32_t max_us;
    uint16_t count;
    uint16_t bpm_x10 = 0U;
    uint8_t active;

    if ((now - last_report_tick) < MIDI_CLOCK_DIAGNOSTIC_REPORT_MS)
        return;

    last_report_tick = now;
    active = MidiClockIsExternalSignalPresent();

    {
        uint32_t primask = __get_PRIMASK();

        __disable_irq();
        sum_us = midi_clock_diag_interval_sum_us;
        min_us = midi_clock_diag_interval_min_us;
        max_us = midi_clock_diag_interval_max_us;
        count = midi_clock_diag_interval_count;
        midi_clock_diag_interval_sum_us = 0U;
        midi_clock_diag_interval_min_us = UINT32_MAX;
        midi_clock_diag_interval_max_us = 0U;
        midi_clock_diag_interval_count = 0U;
        if (primask == 0U)
            __enable_irq();
    }

    if (count == 0U || min_us == UINT32_MAX)
        return;

    (void)MidiClockGetExternalBpmX10(&bpm_x10);
    printf("CLKDIAG active=%u samples=%u avg=%luus min=%lu max=%lu pkpk=%lu bpm=%u.%u\r\n",
           (unsigned)active,
           (unsigned)count,
           (unsigned long)(sum_us / (uint32_t)count),
           (unsigned long)min_us,
           (unsigned long)max_us,
           (unsigned long)(max_us - min_us),
           (unsigned)(bpm_x10 / 10U),
           (unsigned)(bpm_x10 % 10U));
#endif
}

MidiTransportEvent_t MidiTransportConsumeEvent(void)
{
    MidiTransportEvent_t event = midi_transport_event;
    midi_transport_event = MIDI_TRANSPORT_EVENT_NONE;
    return event;
}

void USART2_IRQHandler(void)
{
    uint32_t status = USART2->SR;

    /* Read RX data first to clear UART error conditions and keep the receive
     * side draining promptly; soft-thru and sync decoding both hang off that
     * same byte stream. */
    if (status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE))
    {
        uint8_t byte = (uint8_t)USART2->DR;

        if (status & USART_SR_RXNE)
        {
            MidiMonitor_ReceiveByte(MIDI_MONITOR_SOURCE_UART2, byte);
            midi_input_queue_thru_byte(byte);
            MidiReceive(byte);
        }

        status = USART2->SR;
    }

    if ((status & USART_SR_TXE) && ((USART2->CR1 & USART_CR1_TXEIE) != 0U))
        midi_input_service_thru_tx();
}

void UART4_IRQHandler(void)
{
    uint32_t status = UART4->SR;

    if (status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE))
    {
        uint8_t byte = (uint8_t)UART4->DR;

        if (status & USART_SR_RXNE)
            MidiMonitor_ReceiveByte(MIDI_MONITOR_SOURCE_UART4, byte);

        status = UART4->SR;
    }

    if ((status & USART_SR_TXE) && ((UART4->CR1 & USART_CR1_TXEIE) != 0U))
        MidiOutput_HandleTxIrq();
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
