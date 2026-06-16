#include "app/app_expression_input.h"

#include "app/app_requests.h"
#include "app_event.h"
#include "runtime_config.h"
#include "stm32f4xx_hal.h"

#include <limits.h>

#define APP_EXPRESSION_INPUT_RAW_MAX                RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MAX
#define APP_EXPRESSION_INPUT_RAW_DELTA_PER_STEP     64
#define APP_EXPRESSION_INPUT_FILTER_NUMERATOR        3U
#define APP_EXPRESSION_INPUT_FILTER_DENOMINATOR      4U

static uint8_t app_expression_input_sample_valid = 0U;
static volatile uint16_t app_expression_input_pending_raw_sample = 0U;
static volatile uint8_t app_expression_input_pending_sample_ready = 0U;
static volatile uint16_t app_expression_input_latest_raw_sample = 0U;
static volatile uint8_t app_expression_input_latest_raw_sample_valid = 0U;
static uint16_t app_expression_input_last_raw_sample = 0U;
static uint16_t app_expression_input_filtered_raw_sample = 0U;
static int32_t app_expression_input_residual_delta = 0;
static uint8_t app_expression_input_adc_initialized = 0U;

static uint16_t AppExpressionInput_ClampRawSample(uint16_t raw_sample);
static uint16_t AppExpressionInput_ApplyCalibration(uint16_t raw_sample, const RuntimeConfigGlobal_t *global);
static void AppExpressionInput_ResetTrackingState(void);
static void AppExpressionInput_ReportRawSample(uint16_t raw_sample);
static void AppExpressionInput_InitAdc(void);
static void AppExpressionInput_ServiceAdc(void);

/* Initializes the expression-pedal ADC sampling and state tracking. */
void AppExpressionInput_Init(void)
{
    AppExpressionInput_InitAdc();
    AppExpressionInput_ResetTrackingState();
}

/* Stores a new raw ADC sample for foreground processing. */
void AppExpressionInput_PublishRawSample(uint16_t raw_sample)
{
    uint32_t primask = __get_PRIMASK();
    uint16_t clamped_sample = AppExpressionInput_ClampRawSample(raw_sample);

    __disable_irq();
    app_expression_input_pending_raw_sample = clamped_sample;
    app_expression_input_pending_sample_ready = 1U;
    app_expression_input_latest_raw_sample = clamped_sample;
    app_expression_input_latest_raw_sample_valid = 1U;
    if (primask == 0U)
        __enable_irq();
}

/* Returns the latest sampled raw pedal value if one is available. */
uint8_t AppExpressionInput_TryGetLatestRawSample(uint16_t *raw_sample)
{
    uint32_t primask;
    uint16_t latest_raw_sample;
    uint8_t sample_valid;

    if (!raw_sample)
        return 0U;

    primask = __get_PRIMASK();
    __disable_irq();
    latest_raw_sample = app_expression_input_latest_raw_sample;
    sample_valid = app_expression_input_latest_raw_sample_valid;
    if (primask == 0U)
        __enable_irq();

    if (!sample_valid)
        return 0U;

    *raw_sample = latest_raw_sample;
    return 1U;
}

/* Converts pending raw samples into expression-pedal turn events. */
void AppExpressionInput_ProcessPending(void)
{
    const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();
    uint32_t primask;
    uint16_t next_raw_sample;
    uint16_t calibrated_raw_sample;
    uint16_t filtered_raw_sample;
    int32_t raw_delta;
    int16_t step_delta;

    if (!global)
        return;

    AppExpressionInput_ServiceAdc();

    primask = __get_PRIMASK();
    __disable_irq();
    if (!app_expression_input_pending_sample_ready)
    {
        if (primask == 0U)
            __enable_irq();
        return;
    }

    next_raw_sample = app_expression_input_pending_raw_sample;
    app_expression_input_pending_sample_ready = 0U;
    if (primask == 0U)
        __enable_irq();

    AppExpressionInput_ReportRawSample(next_raw_sample);

    if (global->expression_pedal_mode == RUNTIME_CONFIG_EXPRESSION_PEDAL_MODE_DISABLED)
    {
        AppExpressionInput_ResetTrackingState();
        return;
    }

    calibrated_raw_sample = AppExpressionInput_ApplyCalibration(next_raw_sample, global);

    if (!app_expression_input_sample_valid)
    {
        app_expression_input_filtered_raw_sample = calibrated_raw_sample;
        app_expression_input_last_raw_sample = calibrated_raw_sample;
        app_expression_input_sample_valid = 1U;
        app_expression_input_residual_delta = 0;
        return;
    }

    filtered_raw_sample = (uint16_t)((((uint32_t)app_expression_input_filtered_raw_sample
                                      * APP_EXPRESSION_INPUT_FILTER_NUMERATOR)
                                     + (uint32_t)calibrated_raw_sample
                                     + (APP_EXPRESSION_INPUT_FILTER_DENOMINATOR / 2U))
                                    / APP_EXPRESSION_INPUT_FILTER_DENOMINATOR);
    app_expression_input_filtered_raw_sample = filtered_raw_sample;

    raw_delta = (int32_t)filtered_raw_sample - (int32_t)app_expression_input_last_raw_sample;
    app_expression_input_last_raw_sample = filtered_raw_sample;
    if (raw_delta > -(int32_t)APP_EXPRESSION_INPUT_NOISE_THRESHOLD
     && raw_delta < (int32_t)APP_EXPRESSION_INPUT_NOISE_THRESHOLD)
    {
        raw_delta = 0;
    }

    app_expression_input_residual_delta += raw_delta;

    step_delta = (int16_t)(app_expression_input_residual_delta / APP_EXPRESSION_INPUT_RAW_DELTA_PER_STEP);
    app_expression_input_residual_delta -= (int32_t)step_delta * APP_EXPRESSION_INPUT_RAW_DELTA_PER_STEP;

    if (step_delta > INT8_MAX)
        step_delta = INT8_MAX;
    else if (step_delta < INT8_MIN)
        step_delta = INT8_MIN;

    if (step_delta != 0)
    {
        App_QueueEncoderTurnEvent(APP_EVENT_SOURCE_EXPRESSION, (int8_t)step_delta, HAL_GetTick());
    }
}

static uint16_t AppExpressionInput_ClampRawSample(uint16_t raw_sample)
{
    if (raw_sample > APP_EXPRESSION_INPUT_RAW_MAX)
        return APP_EXPRESSION_INPUT_RAW_MAX;

    return raw_sample;
}

static uint16_t AppExpressionInput_ApplyCalibration(uint16_t raw_sample, const RuntimeConfigGlobal_t *global)
{
    uint16_t calibrated_sample = AppExpressionInput_ClampRawSample(raw_sample);
    uint16_t min_raw = RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MIN;
    uint16_t max_raw = RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MAX;
    uint16_t toe_floor;

    if (global)
    {
        min_raw = global->expression_pedal_min_raw;
        max_raw = global->expression_pedal_max_raw;
    }

    if (min_raw >= max_raw)
    {
        min_raw = RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MIN;
        max_raw = RUNTIME_CONFIG_GLOBAL_EXPRESSION_RAW_MAX;
    }

    if (calibrated_sample < min_raw)
        calibrated_sample = min_raw;
    else if (calibrated_sample > max_raw)
        calibrated_sample = max_raw;

    calibrated_sample = (uint16_t)((((uint32_t)(calibrated_sample - min_raw))
                                  * APP_EXPRESSION_INPUT_RAW_MAX)
                                 / (uint32_t)(max_raw - min_raw));

    if (global && global->expression_pedal_invert)
        calibrated_sample = (uint16_t)(APP_EXPRESSION_INPUT_RAW_MAX - calibrated_sample);

    if (calibrated_sample <= (uint16_t)APP_EXPRESSION_INPUT_HEEL_DEAD_ZONE)
    {
        calibrated_sample = 0U;
        return calibrated_sample;
    }

    if ((uint16_t)APP_EXPRESSION_INPUT_TOE_DEAD_ZONE >= APP_EXPRESSION_INPUT_RAW_MAX)
        return calibrated_sample;

    toe_floor = (uint16_t)(APP_EXPRESSION_INPUT_RAW_MAX - (uint16_t)APP_EXPRESSION_INPUT_TOE_DEAD_ZONE);
    if (calibrated_sample >= toe_floor)
        calibrated_sample = APP_EXPRESSION_INPUT_RAW_MAX;

    return calibrated_sample;
}

static void AppExpressionInput_ResetTrackingState(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    app_expression_input_pending_raw_sample = 0U;
    app_expression_input_pending_sample_ready = 0U;
    if (primask == 0U)
        __enable_irq();

    app_expression_input_sample_valid = 0U;
    app_expression_input_last_raw_sample = 0U;
    app_expression_input_filtered_raw_sample = 0U;
    app_expression_input_residual_delta = 0;
}

static void AppExpressionInput_ReportRawSample(uint16_t raw_sample)
{
    (void)raw_sample;
}

static void AppExpressionInput_InitAdc(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    /* PA0 -> ADC1_IN0 analog input */
    GPIOA->MODER |= (3UL << (0U * 2U));
    GPIOA->PUPDR &= ~(3UL << (0U * 2U));

    /* ADC common prescaler PCLK2/4 for stable conversion timing. */
    ADC->CCR &= ~ADC_CCR_ADCPRE;
    ADC->CCR |= ADC_CCR_ADCPRE_0;

    ADC1->CR1 = 0U;
    ADC1->CR2 = 0U;

    /* One regular conversion: channel 0, long enough sample time for pedal source impedance. */
    ADC1->SQR1 = 0U;
    ADC1->SQR2 = 0U;
    ADC1->SQR3 = 0U;
    ADC1->SMPR2 &= ~ADC_SMPR2_SMP0;
    ADC1->SMPR2 |= ADC_SMPR2_SMP0_2; /* 84 cycles */

    ADC1->CR2 |= ADC_CR2_ADON;
    ADC1->CR2 |= ADC_CR2_CONT;
    ADC1->CR2 |= ADC_CR2_SWSTART;

    app_expression_input_adc_initialized = 1U;
}

static void AppExpressionInput_ServiceAdc(void)
{
    if (!app_expression_input_adc_initialized)
        return;

    if ((ADC1->SR & ADC_SR_EOC) == 0U)
        return;

    AppExpressionInput_PublishRawSample((uint16_t)ADC1->DR);
}
