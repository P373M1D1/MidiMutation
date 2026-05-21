#include "midi/midi_clock_estimator.h"

#include "midi/midi_transport_internal.h"

#define MIDI_CLOCK_US_PER_MINUTE_X10  600000000ULL
#define MIDI_CLOCK_BPM_X10_MIN        200U
#define MIDI_CLOCK_BPM_X10_MAX        2400U
#define MIDI_CLOCK_BPM_WINDOW_MIN_PULSES      24U
#define MIDI_CLOCK_BPM_WINDOW_MOTION_PULSES   24U
#define MIDI_CLOCK_BPM_WINDOW_SHRINK_BPS      100U
#define MIDI_CLOCK_BPM_WINDOW_EXPAND_BPS      25U
#define MIDI_CLOCK_BPM_WINDOW_HOLD_PULSES     24U
#define MIDI_CLOCK_PLL_LOCK_ERROR_FILTER_DIVISOR      16U
#define MIDI_CLOCK_PLL_LOCK_ENTER_THRESHOLD_DIVISOR    5U
#define MIDI_CLOCK_PLL_LOCK_EXIT_THRESHOLD_DIVISOR     3U
#define MIDI_CLOCK_PLL_LOCK_THRESHOLD_MIN_US         250U
#define MIDI_CLOCK_PLL_LOCK_STABLE_PULSES             12U
#define MIDI_CLOCK_PLL_TRACK_PHASE_GAIN_DIVISOR        8U
#define MIDI_CLOCK_PLL_ACQUIRE_PHASE_GAIN_DIVISOR      2U
#define MIDI_CLOCK_PLL_TRACK_FREQUENCY_GAIN_DIVISOR   64U
#define MIDI_CLOCK_PLL_ACQUIRE_FREQUENCY_GAIN_DIVISOR 16U
#define MIDI_CLOCK_PLL_FREQUENCY_CLAMP_DIVISOR        32U
#define MIDI_CLOCK_PLL_FREQUENCY_CLAMP_MIN_US          8U
#define MIDI_CLOCK_RECOVERED_PHASE_CLAMP_DIVISOR       2U
#define MIDI_CLOCK_RECOVERED_PHASE_CLAMP_MIN_US      250U
#define MIDI_CLOCK_PUBLICATION_READY_MIN_PULSES       MIDI_CLOCK_BPM_WINDOW_MIN_PULSES

static uint32_t midi_clock_pulse_intervals_us[MIDI_CLOCK_BPM_WINDOW_PULSES];
static uint8_t midi_clock_pulse_interval_index = 0U;
static uint8_t midi_clock_estimator_window_pulses = 0U;
static uint8_t midi_clock_estimator_window_hold_pulses = 0U;
static volatile uint16_t midi_clock_external_bpm_measured_x10 = 0U;
static volatile uint8_t midi_clock_external_bpm_measured_valid = 0U;
static volatile uint32_t midi_clock_recovered_pulse_interval_us = 0U;
static volatile uint32_t midi_clock_recovered_phase_pulse_us = 0U;
static volatile uint32_t midi_clock_recovered_last_pulse_us = 0U;
static volatile uint16_t midi_clock_recovered_bpm_x10 = 0U;
static volatile uint8_t midi_clock_recovered_pulse_valid = 0U;
static volatile uint8_t midi_clock_recovered_interval_valid = 0U;
static volatile uint8_t midi_clock_recovered_bpm_valid = 0U;
static volatile int32_t midi_clock_recovered_phase_error_us = 0;
static volatile int32_t midi_clock_recovered_phase_correction_us = 0;
static volatile int32_t midi_clock_recovered_frequency_correction_us = 0;
static volatile uint32_t midi_clock_recovered_abs_phase_error_avg_us = 0U;
static volatile uint8_t midi_clock_recovered_pll_lock_counter = 0U;
static volatile uint8_t midi_clock_recovered_pll_locked = 0U;
static volatile uint8_t midi_clock_recovered_pll_fast_mode = 0U;

static uint32_t midi_clock_estimator_abs_delta_u32(uint32_t a, uint32_t b);
static int32_t midi_clock_estimator_abs_s32(int32_t value);
static int32_t midi_clock_estimator_clamp_s32(int32_t value, int32_t limit);
static int32_t midi_clock_estimator_divide_with_min_step(int32_t value, uint32_t divisor);
static uint32_t midi_clock_estimator_step_toward_u32(uint32_t current,
                                                     uint32_t target,
                                                     uint32_t divisor);
static uint32_t midi_clock_estimator_apply_signed_delta_u32(uint32_t value,
                                                            int32_t delta,
                                                            uint32_t minimum);
static uint32_t midi_clock_estimator_phase_clamp_us(uint32_t interval_us);
static void midi_clock_estimator_publish_recovered_bpm(void);

__attribute__((section(".RamFunc")))
static void midi_clock_estimator_note_recovered_timing(uint32_t now_us, uint32_t interval_us);

__attribute__((section(".RamFunc")))
static uint32_t midi_clock_estimator_sum_recent_intervals(uint8_t interval_count)
{
    uint32_t sum_us = 0U;
    uint8_t buffer_index = midi_clock_pulse_interval_index;

    while (interval_count > 0U)
    {
        buffer_index = (buffer_index == 0U)
            ? (uint8_t)(MIDI_CLOCK_BPM_WINDOW_PULSES - 1U)
            : (uint8_t)(buffer_index - 1U);
        sum_us += midi_clock_pulse_intervals_us[buffer_index];
        interval_count--;
    }

    return sum_us;
}

__attribute__((section(".RamFunc")))
static uint8_t midi_clock_estimator_select_window_pulses(void)
{
    uint8_t available_count = midi_clock_pulse_interval_count;

    if (available_count == 0U)
    {
        midi_clock_estimator_window_pulses = 0U;
        midi_clock_estimator_window_hold_pulses = 0U;
        return 0U;
    }

    if (midi_clock_estimator_window_pulses == 0U
     || midi_clock_estimator_window_pulses > available_count)
    {
        midi_clock_estimator_window_pulses = available_count;
    }

    if (available_count >= MIDI_CLOCK_BPM_WINDOW_MOTION_PULSES)
    {
        uint32_t full_average_us;
        uint32_t recent_average_us;
        uint32_t recent_sum_us;
        uint32_t delta_us;
        uint32_t shrink_threshold_us;
        uint32_t expand_threshold_us;

        full_average_us = midi_clock_pulse_interval_sum_us / (uint32_t)available_count;
        recent_sum_us = midi_clock_estimator_sum_recent_intervals(MIDI_CLOCK_BPM_WINDOW_MOTION_PULSES);
        recent_average_us = recent_sum_us / (uint32_t)MIDI_CLOCK_BPM_WINDOW_MOTION_PULSES;
        delta_us = (recent_average_us >= full_average_us)
            ? (recent_average_us - full_average_us)
            : (full_average_us - recent_average_us);
        shrink_threshold_us = (((uint32_t)full_average_us * MIDI_CLOCK_BPM_WINDOW_SHRINK_BPS) + 5000U) / 10000U;
        expand_threshold_us = (((uint32_t)full_average_us * MIDI_CLOCK_BPM_WINDOW_EXPAND_BPS) + 5000U) / 10000U;

        if (delta_us >= shrink_threshold_us)
        {
            midi_clock_estimator_window_pulses = (available_count > MIDI_CLOCK_BPM_WINDOW_MIN_PULSES)
                ? MIDI_CLOCK_BPM_WINDOW_MIN_PULSES
                : available_count;
            midi_clock_estimator_window_hold_pulses = MIDI_CLOCK_BPM_WINDOW_HOLD_PULSES;
        }
        else if (midi_clock_estimator_window_hold_pulses > 0U)
        {
            midi_clock_estimator_window_hold_pulses--;
        }
        else if (delta_us <= expand_threshold_us
              && midi_clock_estimator_window_pulses < available_count)
        {
            midi_clock_estimator_window_pulses++;
        }
    }

    return midi_clock_estimator_window_pulses;
}

static uint32_t midi_clock_estimator_abs_delta_u32(uint32_t a, uint32_t b)
{
    return (a >= b) ? (a - b) : (b - a);
}

static int32_t midi_clock_estimator_abs_s32(int32_t value)
{
    return (value >= 0) ? value : -value;
}

static int32_t midi_clock_estimator_clamp_s32(int32_t value, int32_t limit)
{
    if (value > limit)
        return limit;

    if (value < -limit)
        return -limit;

    return value;
}

static int32_t midi_clock_estimator_divide_with_min_step(int32_t value, uint32_t divisor)
{
    int32_t correction;

    if (value == 0)
        return 0;

    if (divisor == 0U)
        divisor = 1U;

    correction = value / (int32_t)divisor;
    if (correction == 0)
        correction = (value > 0) ? 1 : -1;

    return correction;
}

static uint32_t midi_clock_estimator_step_toward_u32(uint32_t current,
                                                     uint32_t target,
                                                     uint32_t divisor)
{
    uint32_t delta;
    uint32_t step;

    if (current == target)
        return current;

    if (divisor == 0U)
        divisor = 1U;

    delta = midi_clock_estimator_abs_delta_u32(current, target);
    step = delta / divisor;
    if (step == 0U)
        step = 1U;

    return (target > current)
        ? (current + step)
        : (current - step);
}

static uint32_t midi_clock_estimator_apply_signed_delta_u32(uint32_t value,
                                                            int32_t delta,
                                                            uint32_t minimum)
{
    if (delta >= 0)
    {
        value += (uint32_t)delta;
    }
    else
    {
        uint32_t abs_delta = (uint32_t)(-delta);

        if (value > (minimum + abs_delta))
            value -= abs_delta;
        else
            value = minimum;
    }

    if (value < minimum)
        value = minimum;

    return value;
}

static uint32_t midi_clock_estimator_phase_clamp_us(uint32_t interval_us)
{
    uint32_t clamp_us = interval_us / MIDI_CLOCK_RECOVERED_PHASE_CLAMP_DIVISOR;

    if (clamp_us < MIDI_CLOCK_RECOVERED_PHASE_CLAMP_MIN_US)
        clamp_us = MIDI_CLOCK_RECOVERED_PHASE_CLAMP_MIN_US;

    return clamp_us;
}

static void midi_clock_estimator_publish_recovered_bpm(void)
{
    uint64_t numerator;
    uint32_t denominator;
    uint32_t bpm_x10;

    if (!midi_clock_recovered_interval_valid || midi_clock_recovered_pulse_interval_us == 0U)
    {
        midi_clock_recovered_bpm_x10 = 0U;
        midi_clock_recovered_bpm_valid = 0U;
        return;
    }

    numerator = MIDI_CLOCK_US_PER_MINUTE_X10;
    denominator = MIDI_CLOCK_PULSES_PER_QUARTER_NOTE * midi_clock_recovered_pulse_interval_us;
    bpm_x10 = (uint32_t)((numerator + (uint64_t)(denominator / 2U)) / (uint64_t)denominator);

    if (bpm_x10 >= MIDI_CLOCK_BPM_X10_MIN && bpm_x10 <= MIDI_CLOCK_BPM_X10_MAX)
    {
        midi_clock_recovered_bpm_x10 = (uint16_t)bpm_x10;
        midi_clock_recovered_bpm_valid = 1U;
    }
    else
    {
        midi_clock_recovered_bpm_x10 = 0U;
        midi_clock_recovered_bpm_valid = 0U;
    }
}

__attribute__((section(".RamFunc")))
static void midi_clock_estimator_note_recovered_timing(uint32_t now_us, uint32_t interval_us)
{
    uint32_t predicted_phase_pulse_us;
    uint32_t recovered_interval_us;
    uint32_t phase_limit_us;
    uint32_t lock_enter_threshold_us;
    uint32_t lock_exit_threshold_us;
    uint32_t phase_gain_divisor;
    uint32_t frequency_gain_divisor;
    uint32_t frequency_limit_us;
    uint32_t recovered_phase_pulse_us;
    uint32_t abs_phase_error_us;
    uint8_t locked;
    int32_t phase_error_us;
    int32_t phase_correction_us;
    int32_t frequency_correction_us;

    if (interval_us == 0U)
        return;

    if (!midi_clock_recovered_pulse_valid)
    {
        midi_clock_recovered_phase_pulse_us = now_us;
        midi_clock_recovered_last_pulse_us = now_us;
        midi_clock_recovered_pulse_valid = 1U;
        midi_clock_recovered_phase_error_us = 0;
        midi_clock_recovered_phase_correction_us = 0;
        midi_clock_recovered_frequency_correction_us = 0;
        midi_clock_recovered_abs_phase_error_avg_us = 0U;
        midi_clock_recovered_pll_lock_counter = 0U;
        midi_clock_recovered_pll_locked = 0U;
        midi_clock_recovered_pll_fast_mode = 0U;
    }

    if (!midi_clock_recovered_interval_valid)
    {
        midi_clock_recovered_pulse_interval_us = interval_us;
        midi_clock_recovered_phase_pulse_us = now_us;
        midi_clock_recovered_last_pulse_us = now_us;
        midi_clock_recovered_interval_valid = 1U;
        midi_clock_recovered_phase_error_us = 0;
        midi_clock_recovered_phase_correction_us = 0;
        midi_clock_recovered_frequency_correction_us = 0;
        midi_clock_recovered_abs_phase_error_avg_us = 0U;
        midi_clock_recovered_pll_lock_counter = 0U;
        midi_clock_recovered_pll_locked = 0U;
        midi_clock_recovered_pll_fast_mode = 0U;
        midi_clock_estimator_publish_recovered_bpm();
        return;
    }

    recovered_interval_us = midi_clock_recovered_pulse_interval_us;
    predicted_phase_pulse_us = midi_clock_recovered_phase_pulse_us + recovered_interval_us;
    phase_error_us = (int32_t)(now_us - predicted_phase_pulse_us);
    phase_limit_us = midi_clock_estimator_phase_clamp_us(recovered_interval_us);
    phase_error_us = midi_clock_estimator_clamp_s32(phase_error_us, (int32_t)phase_limit_us);

    abs_phase_error_us = (uint32_t)midi_clock_estimator_abs_s32(phase_error_us);
    if (midi_clock_recovered_abs_phase_error_avg_us == 0U)
        midi_clock_recovered_abs_phase_error_avg_us = abs_phase_error_us;
    else
        midi_clock_recovered_abs_phase_error_avg_us = midi_clock_estimator_step_toward_u32(
            midi_clock_recovered_abs_phase_error_avg_us,
            abs_phase_error_us,
            MIDI_CLOCK_PLL_LOCK_ERROR_FILTER_DIVISOR);

    lock_enter_threshold_us = recovered_interval_us / MIDI_CLOCK_PLL_LOCK_ENTER_THRESHOLD_DIVISOR;
    if (lock_enter_threshold_us < MIDI_CLOCK_PLL_LOCK_THRESHOLD_MIN_US)
        lock_enter_threshold_us = MIDI_CLOCK_PLL_LOCK_THRESHOLD_MIN_US;
    lock_exit_threshold_us = recovered_interval_us / MIDI_CLOCK_PLL_LOCK_EXIT_THRESHOLD_DIVISOR;
    if (lock_exit_threshold_us < MIDI_CLOCK_PLL_LOCK_THRESHOLD_MIN_US)
        lock_exit_threshold_us = MIDI_CLOCK_PLL_LOCK_THRESHOLD_MIN_US;

    if (midi_clock_recovered_pll_locked)
    {
        if (midi_clock_recovered_abs_phase_error_avg_us >= lock_exit_threshold_us)
        {
            midi_clock_recovered_pll_locked = 0U;
            midi_clock_recovered_pll_lock_counter = 0U;
        }
    }
    else if (abs_phase_error_us <= lock_enter_threshold_us
          && midi_clock_recovered_abs_phase_error_avg_us <= lock_enter_threshold_us)
    {
        if (midi_clock_recovered_pll_lock_counter < UINT8_MAX)
            midi_clock_recovered_pll_lock_counter++;

        if (midi_clock_recovered_pll_lock_counter >= MIDI_CLOCK_PLL_LOCK_STABLE_PULSES)
            midi_clock_recovered_pll_locked = 1U;
    }
    else
    {
        midi_clock_recovered_pll_lock_counter = 0U;
    }

    locked = midi_clock_recovered_pll_locked;
    phase_gain_divisor = locked
        ? MIDI_CLOCK_PLL_TRACK_PHASE_GAIN_DIVISOR
        : MIDI_CLOCK_PLL_ACQUIRE_PHASE_GAIN_DIVISOR;
    frequency_gain_divisor = locked
        ? MIDI_CLOCK_PLL_TRACK_FREQUENCY_GAIN_DIVISOR
        : MIDI_CLOCK_PLL_ACQUIRE_FREQUENCY_GAIN_DIVISOR;

    phase_correction_us = midi_clock_estimator_divide_with_min_step(phase_error_us,
                                                                    phase_gain_divisor);
    frequency_correction_us = midi_clock_estimator_divide_with_min_step(phase_error_us,
                                                                        frequency_gain_divisor);
    frequency_limit_us = recovered_interval_us / MIDI_CLOCK_PLL_FREQUENCY_CLAMP_DIVISOR;
    if (frequency_limit_us < MIDI_CLOCK_PLL_FREQUENCY_CLAMP_MIN_US)
        frequency_limit_us = MIDI_CLOCK_PLL_FREQUENCY_CLAMP_MIN_US;
    frequency_correction_us = midi_clock_estimator_clamp_s32(frequency_correction_us,
                                                             (int32_t)frequency_limit_us);

    midi_clock_recovered_pulse_interval_us = midi_clock_estimator_apply_signed_delta_u32(
        recovered_interval_us,
        frequency_correction_us,
        1U);
    recovered_phase_pulse_us = midi_clock_estimator_apply_signed_delta_u32(
        predicted_phase_pulse_us,
        phase_correction_us,
        0U);
    midi_clock_recovered_phase_pulse_us = recovered_phase_pulse_us;
    midi_clock_recovered_last_pulse_us = (recovered_phase_pulse_us > now_us)
        ? now_us
        : recovered_phase_pulse_us;
    midi_clock_recovered_phase_error_us = phase_error_us;
    midi_clock_recovered_phase_correction_us = phase_correction_us;
    midi_clock_recovered_frequency_correction_us = frequency_correction_us;
    midi_clock_recovered_pll_fast_mode = locked ? 0U : 1U;

    midi_clock_estimator_publish_recovered_bpm();
}

__attribute__((section(".RamFunc")))
void MidiClockEstimator_Reset(void)
{
    midi_clock_pulse_interval_index = 0U;
    midi_clock_estimator_window_pulses = 0U;
    midi_clock_estimator_window_hold_pulses = 0U;
    midi_clock_external_bpm_window_pulses = 0U;
    midi_clock_external_bpm_measured_x10 = 0U;
    midi_clock_external_bpm_measured_valid = 0U;
    midi_clock_recovered_pulse_interval_us = 0U;
    midi_clock_recovered_phase_pulse_us = 0U;
    midi_clock_recovered_last_pulse_us = 0U;
    midi_clock_recovered_bpm_x10 = 0U;
    midi_clock_recovered_pulse_valid = 0U;
    midi_clock_recovered_interval_valid = 0U;
    midi_clock_recovered_bpm_valid = 0U;
    midi_clock_recovered_phase_error_us = 0;
    midi_clock_recovered_phase_correction_us = 0;
    midi_clock_recovered_frequency_correction_us = 0;
    midi_clock_recovered_abs_phase_error_avg_us = 0U;
    midi_clock_recovered_pll_lock_counter = 0U;
    midi_clock_recovered_pll_locked = 0U;
    midi_clock_recovered_pll_fast_mode = 0U;

    for (uint8_t index = 0U; index < MIDI_CLOCK_BPM_WINDOW_PULSES; index++)
        midi_clock_pulse_intervals_us[index] = 0U;
}

__attribute__((section(".RamFunc")))
void MidiClockEstimator_AnchorPulse(uint32_t now_us)
{
    midi_clock_recovered_phase_pulse_us = now_us;
    midi_clock_recovered_last_pulse_us = now_us;
    midi_clock_recovered_pulse_valid = 1U;
    midi_clock_recovered_phase_error_us = 0;
    midi_clock_recovered_phase_correction_us = 0;
    midi_clock_recovered_frequency_correction_us = 0;
    midi_clock_recovered_abs_phase_error_avg_us = 0U;
    midi_clock_recovered_pll_lock_counter = 0U;
    midi_clock_recovered_pll_locked = 0U;
    midi_clock_recovered_pll_fast_mode = 0U;
}

__attribute__((section(".RamFunc")))
void MidiClockEstimator_NotePulseInterval(uint32_t now_us, uint32_t interval_us)
{
    uint8_t estimator_window_pulses;
    uint32_t estimator_window_sum_us;

    if (interval_us == 0U)
        return;

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
    midi_clock_estimator_note_recovered_timing(now_us, interval_us);

    if (midi_clock_pulse_interval_sum_us > 0U)
    {
        estimator_window_pulses = midi_clock_estimator_select_window_pulses();
        if (estimator_window_pulses == 0U)
        {
            midi_clock_external_bpm_valid = 0U;
            midi_clock_external_bpm_window_pulses = 0U;
            midi_clock_external_bpm_measured_valid = 0U;
            return;
        }

        estimator_window_sum_us = (estimator_window_pulses == midi_clock_pulse_interval_count)
            ? midi_clock_pulse_interval_sum_us
            : midi_clock_estimator_sum_recent_intervals(estimator_window_pulses);

        if (estimator_window_sum_us == 0U)
        {
            midi_clock_external_bpm_valid = 0U;
            midi_clock_external_bpm_window_pulses = 0U;
            midi_clock_external_bpm_measured_valid = 0U;
            return;
        }

        uint64_t numerator = MIDI_CLOCK_US_PER_MINUTE_X10 * (uint64_t)estimator_window_pulses;
        uint32_t denominator = MIDI_CLOCK_PULSES_PER_QUARTER_NOTE * estimator_window_sum_us;
        uint32_t bpm_x10 = (uint32_t)((numerator + (uint64_t)(denominator / 2U)) / (uint64_t)denominator);

        midi_clock_external_bpm_measured_x10 = (bpm_x10 > UINT16_MAX) ? UINT16_MAX : (uint16_t)bpm_x10;
        midi_clock_external_bpm_measured_valid = 1U;

        if (bpm_x10 >= MIDI_CLOCK_BPM_X10_MIN && bpm_x10 <= MIDI_CLOCK_BPM_X10_MAX)
        {
            midi_clock_external_bpm_x10 = (uint16_t)bpm_x10;
            midi_clock_external_bpm_valid = 1U;
            midi_clock_external_bpm_window_pulses = estimator_window_pulses;
        }
        else
        {
            midi_clock_external_bpm_valid = 0U;
            midi_clock_external_bpm_window_pulses = 0U;
        }
    }
}

void MidiClockEstimator_GetStatus(MidiClockEstimatorStatus_t *status)
{
    uint32_t primask;
    uint8_t interval_valid;
    uint8_t bpm_valid;
    uint8_t pll_locked;
    uint8_t window_pulses;
    uint8_t observed_pulses;

    if (!status)
        return;

    primask = __get_PRIMASK();
    __disable_irq();
    interval_valid = midi_clock_recovered_interval_valid;
    bpm_valid = midi_clock_recovered_bpm_valid;
    pll_locked = midi_clock_recovered_pll_locked;
    window_pulses = midi_clock_estimator_window_pulses;
    observed_pulses = midi_clock_pulse_interval_count;
    if (primask == 0U)
        __enable_irq();

    status->estimator_valid = interval_valid;
    status->lock_quality = !interval_valid
        ? MIDI_CLOCK_LOCK_QUALITY_NONE
        : (pll_locked ? MIDI_CLOCK_LOCK_QUALITY_LOCKED : MIDI_CLOCK_LOCK_QUALITY_ACQUIRING);
    status->publication_ready = (uint8_t)(bpm_valid
        && pll_locked
        && window_pulses >= MIDI_CLOCK_PUBLICATION_READY_MIN_PULSES);
    status->window_pulses = window_pulses;
    status->observed_pulses = observed_pulses;
}

__attribute__((section(".RamFunc")))
uint8_t MidiClockEstimator_GetRecoveredPulseTimestampUs(uint32_t *pulse_us)
{
    if (!pulse_us || !midi_clock_recovered_pulse_valid)
        return 0U;

    *pulse_us = midi_clock_recovered_last_pulse_us;
    return 1U;
}

__attribute__((section(".RamFunc")))
uint8_t MidiClockEstimator_GetRecoveredPulseIntervalUs(uint32_t *interval_us)
{
    if (!interval_us || !midi_clock_recovered_interval_valid)
        return 0U;

    *interval_us = midi_clock_recovered_pulse_interval_us;
    return 1U;
}

void MidiClockEstimator_GetRecoveredTimingSnapshot(uint32_t *pulse_us,
												   uint32_t *interval_us)
{
	if (pulse_us)
        *pulse_us = midi_clock_recovered_pulse_valid ? midi_clock_recovered_phase_pulse_us : 0U;

	if (interval_us)
		*interval_us = midi_clock_recovered_interval_valid ? midi_clock_recovered_pulse_interval_us : 0U;
}

void MidiClockEstimator_GetRecoveredPllDiagnostics(int32_t *phase_error_us,
                                               int32_t *phase_correction_us,
                                               int32_t *frequency_correction_us,
                                               uint8_t *fast_mode)
{
    if (phase_error_us)
        *phase_error_us = midi_clock_recovered_phase_error_us;

    if (phase_correction_us)
        *phase_correction_us = midi_clock_recovered_phase_correction_us;

    if (frequency_correction_us)
        *frequency_correction_us = midi_clock_recovered_frequency_correction_us;

    if (fast_mode)
        *fast_mode = midi_clock_recovered_pll_fast_mode;
}

uint8_t MidiClockEstimator_GetRecoveredBpmX10(uint16_t *bpm_x10)
{
    if (!bpm_x10 || !midi_clock_recovered_bpm_valid)
        return 0U;

    *bpm_x10 = midi_clock_recovered_bpm_x10;
    return 1U;
}

uint8_t MidiClockEstimator_GetMeasuredBpmX10(uint16_t *bpm_x10)
{
    if (!bpm_x10 || !midi_clock_external_bpm_measured_valid)
        return 0U;

    *bpm_x10 = midi_clock_external_bpm_measured_x10;
    return 1U;
}

uint8_t MidiClockEstimator_GetRawBpmX10(uint16_t *bpm_x10)
{
    if (!bpm_x10 || !midi_clock_external_bpm_valid)
        return 0U;

    *bpm_x10 = midi_clock_external_bpm_x10;
    return 1U;
}