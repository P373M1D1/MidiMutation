#include "app/app_encoder_sampler.h"

#include "app/app_encoder_math.h"
#include "main.h"

#include <limits.h>

#define APP_ENCODER_SAMPLER_COUNT 3U
#define APP_ENCODER_SAMPLER_TEMPO_TRANSITIONS_PER_STEP 4
#define APP_ENCODER_SAMPLER_TEMPO_DIRECTION_SIGN 1
#define APP_ENCODER_SAMPLER_TEMPO_ACCEL_MID_MS 60U
#define APP_ENCODER_SAMPLER_TEMPO_ACCEL_FAST_MS 35U
#define APP_ENCODER_SAMPLER_TEMPO_ACCEL_VFAST_MS 20U
#define APP_ENCODER_SAMPLER_TEMPO_STEP_MID 2
#define APP_ENCODER_SAMPLER_TEMPO_STEP_FAST 4
#define APP_ENCODER_SAMPLER_TEMPO_STEP_VFAST 8
#define APP_ENCODER_SAMPLER_SCROLL_TRANSITIONS_PER_STEP 4
#define APP_ENCODER_SAMPLER_SCROLL_DIRECTION_SIGN -1

static uint8_t app_encoder_sampler_last_state[APP_ENCODER_SAMPLER_COUNT] = {0U, 0U, 0U};
static int8_t app_encoder_sampler_transition_accum[APP_ENCODER_SAMPLER_COUNT] = {0, 0, 0};
static volatile int8_t app_encoder_sampler_pending_delta[APP_ENCODER_SAMPLER_COUNT] = {0, 0, 0};
static volatile uint8_t app_encoder_sampler_activity_pending[APP_ENCODER_SAMPLER_COUNT] = {0U, 0U, 0U};
static uint32_t app_encoder_sampler_tempo_last_step_tick = 0U;

static uint8_t AppEncoderSampler_ReadLevel(GPIO_TypeDef *gpio_port, uint16_t gpio_pin);
static uint8_t AppEncoderSampler_ReadState(GPIO_TypeDef *clk_gpio_port,
                                           uint16_t clk_gpio_pin,
                                           GPIO_TypeDef *dt_gpio_port,
                                           uint16_t dt_gpio_pin);
static void AppEncoderSampler_AddPendingDelta(volatile int8_t *pending_delta, int8_t delta);
static void AppEncoderSampler_SampleSimple(AppEncoderSamplerId_t encoder_id,
                                           GPIO_TypeDef *clk_gpio_port,
                                           uint16_t clk_gpio_pin,
                                           GPIO_TypeDef *dt_gpio_port,
                                           uint16_t dt_gpio_pin,
                                           uint8_t transitions_per_step,
                                           int8_t step_sign);
static void AppEncoderSampler_SampleTempoInterrupt(void);
static int8_t AppEncoderSampler_ResolveTempoStepMagnitude(uint32_t step_interval_ms);

void AppEncoderSampler_Init(void)
{
    app_encoder_sampler_last_state[APP_ENCODER_SAMPLER_ENCODER1] = AppEncoderSampler_ReadState(ENC1_CLK_GPIO_Port,
                                                                                                ENC1_CLK_Pin,
                                                                                                ENC1_DT_GPIO_Port,
                                                                                                ENC1_DT_Pin);
    app_encoder_sampler_last_state[APP_ENCODER_SAMPLER_ENCODER2] = AppEncoderSampler_ReadState(ENC2_CLK_GPIO_Port,
                                                                                                ENC2_CLK_Pin,
                                                                                                ENC2_DT_GPIO_Port,
                                                                                                ENC2_DT_Pin);
    app_encoder_sampler_last_state[APP_ENCODER_SAMPLER_ENCODER3] = AppEncoderSampler_ReadState(ENC3_CLK_GPIO_Port,
                                                                                                ENC3_CLK_Pin,
                                                                                                ENC3_DT_GPIO_Port,
                                                                                                ENC3_DT_Pin);
    app_encoder_sampler_transition_accum[APP_ENCODER_SAMPLER_ENCODER1] = 0;
    app_encoder_sampler_transition_accum[APP_ENCODER_SAMPLER_ENCODER2] = 0;
    app_encoder_sampler_transition_accum[APP_ENCODER_SAMPLER_ENCODER3] = 0;
    app_encoder_sampler_pending_delta[APP_ENCODER_SAMPLER_ENCODER1] = 0;
    app_encoder_sampler_pending_delta[APP_ENCODER_SAMPLER_ENCODER2] = 0;
    app_encoder_sampler_pending_delta[APP_ENCODER_SAMPLER_ENCODER3] = 0;
    app_encoder_sampler_activity_pending[APP_ENCODER_SAMPLER_ENCODER1] = 0U;
    app_encoder_sampler_activity_pending[APP_ENCODER_SAMPLER_ENCODER2] = 0U;
    app_encoder_sampler_activity_pending[APP_ENCODER_SAMPLER_ENCODER3] = 0U;
    app_encoder_sampler_tempo_last_step_tick = 0U;
}

void AppEncoderSampler_SampleInterrupt(void)
{
    AppEncoderSampler_SampleSimple(APP_ENCODER_SAMPLER_ENCODER1,
                                   ENC1_CLK_GPIO_Port,
                                   ENC1_CLK_Pin,
                                   ENC1_DT_GPIO_Port,
                                   ENC1_DT_Pin,
                                   APP_ENCODER_SAMPLER_SCROLL_TRANSITIONS_PER_STEP,
                                   (int8_t)-APP_ENCODER_SAMPLER_SCROLL_DIRECTION_SIGN);
    AppEncoderSampler_SampleSimple(APP_ENCODER_SAMPLER_ENCODER2,
                                   ENC2_CLK_GPIO_Port,
                                   ENC2_CLK_Pin,
                                   ENC2_DT_GPIO_Port,
                                   ENC2_DT_Pin,
                                   APP_ENCODER_SAMPLER_SCROLL_TRANSITIONS_PER_STEP,
                                   1);
    AppEncoderSampler_SampleTempoInterrupt();
}

void AppEncoderSampler_MarkActivity(AppEncoderSamplerId_t encoder_id)
{
    if ((uint8_t)encoder_id >= APP_ENCODER_SAMPLER_COUNT)
        return;

    app_encoder_sampler_activity_pending[encoder_id] = 1U;
}

void AppEncoderSampler_TakePendingMotion(AppEncoderSamplerId_t encoder_id,
                                         uint8_t *activity_pending,
                                         int8_t *pending_delta)
{
    uint32_t primask;

    if (!activity_pending || !pending_delta || ((uint8_t)encoder_id >= APP_ENCODER_SAMPLER_COUNT))
    {
        if (activity_pending)
            *activity_pending = 0U;
        if (pending_delta)
            *pending_delta = 0;
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    *activity_pending = app_encoder_sampler_activity_pending[encoder_id];
    *pending_delta = app_encoder_sampler_pending_delta[encoder_id];
    app_encoder_sampler_activity_pending[encoder_id] = 0U;
    app_encoder_sampler_pending_delta[encoder_id] = 0;
    if (primask == 0U)
        __enable_irq();
}

static uint8_t AppEncoderSampler_ReadLevel(GPIO_TypeDef *gpio_port, uint16_t gpio_pin)
{
    return ((gpio_port->IDR & gpio_pin) != 0U) ? 1U : 0U;
}

static uint8_t AppEncoderSampler_ReadState(GPIO_TypeDef *clk_gpio_port,
                                           uint16_t clk_gpio_pin,
                                           GPIO_TypeDef *dt_gpio_port,
                                           uint16_t dt_gpio_pin)
{
    return (uint8_t)((AppEncoderSampler_ReadLevel(clk_gpio_port, clk_gpio_pin) << 1U)
                   | AppEncoderSampler_ReadLevel(dt_gpio_port, dt_gpio_pin));
}

static void AppEncoderSampler_AddPendingDelta(volatile int8_t *pending_delta, int8_t delta)
{
    int16_t next_delta;

    if (!pending_delta || delta == 0)
        return;

    next_delta = (int16_t)(*pending_delta) + (int16_t)delta;
    if (next_delta > INT8_MAX)
        next_delta = INT8_MAX;
    else if (next_delta < INT8_MIN)
        next_delta = INT8_MIN;

    *pending_delta = (int8_t)next_delta;
}

static void AppEncoderSampler_SampleSimple(AppEncoderSamplerId_t encoder_id,
                                           GPIO_TypeDef *clk_gpio_port,
                                           uint16_t clk_gpio_pin,
                                           GPIO_TypeDef *dt_gpio_port,
                                           uint16_t dt_gpio_pin,
                                           uint8_t transitions_per_step,
                                           int8_t step_sign)
{
    uint8_t current_state;
    int8_t transition_delta;

    if ((uint8_t)encoder_id >= APP_ENCODER_SAMPLER_COUNT)
        return;

    current_state = AppEncoderSampler_ReadState(clk_gpio_port, clk_gpio_pin, dt_gpio_port, dt_gpio_pin);
    if (current_state == app_encoder_sampler_last_state[encoder_id])
        return;

    app_encoder_sampler_activity_pending[encoder_id] = 1U;
    transition_delta = AppEncoderMath_TransitionDelta(app_encoder_sampler_last_state[encoder_id], current_state);
    app_encoder_sampler_transition_accum[encoder_id] = AppEncoderMath_AccumulateTransition(app_encoder_sampler_transition_accum[encoder_id],
                                                                                           transition_delta);
    app_encoder_sampler_last_state[encoder_id] = current_state;

    if (app_encoder_sampler_transition_accum[encoder_id] >= (int8_t)transitions_per_step)
    {
        AppEncoderSampler_AddPendingDelta(&app_encoder_sampler_pending_delta[encoder_id], step_sign);
        app_encoder_sampler_transition_accum[encoder_id] = 0;
    }
    else if (app_encoder_sampler_transition_accum[encoder_id] <= -(int8_t)transitions_per_step)
    {
        AppEncoderSampler_AddPendingDelta(&app_encoder_sampler_pending_delta[encoder_id], (int8_t)-step_sign);
        app_encoder_sampler_transition_accum[encoder_id] = 0;
    }
}

static void AppEncoderSampler_SampleTempoInterrupt(void)
{
    uint8_t current_state = AppEncoderSampler_ReadState(ENC3_CLK_GPIO_Port, ENC3_CLK_Pin,
                                                        ENC3_DT_GPIO_Port, ENC3_DT_Pin);
    int8_t transition_delta;

    if (current_state == app_encoder_sampler_last_state[APP_ENCODER_SAMPLER_ENCODER3])
        return;

    app_encoder_sampler_activity_pending[APP_ENCODER_SAMPLER_ENCODER3] = 1U;
    transition_delta = AppEncoderMath_TransitionDelta(app_encoder_sampler_last_state[APP_ENCODER_SAMPLER_ENCODER3],
                                                      current_state);
    app_encoder_sampler_transition_accum[APP_ENCODER_SAMPLER_ENCODER3] = AppEncoderMath_AccumulateTransition(app_encoder_sampler_transition_accum[APP_ENCODER_SAMPLER_ENCODER3],
                                                                                                               transition_delta);
    app_encoder_sampler_last_state[APP_ENCODER_SAMPLER_ENCODER3] = current_state;

    if (app_encoder_sampler_transition_accum[APP_ENCODER_SAMPLER_ENCODER3] >= APP_ENCODER_SAMPLER_TEMPO_TRANSITIONS_PER_STEP)
    {
        uint32_t now = HAL_GetTick();
        uint32_t step_interval_ms = (app_encoder_sampler_tempo_last_step_tick == 0U)
                                  ? UINT32_MAX
                                  : (now - app_encoder_sampler_tempo_last_step_tick);
        int8_t step_size = (int8_t)(APP_ENCODER_SAMPLER_TEMPO_DIRECTION_SIGN
                                  * AppEncoderSampler_ResolveTempoStepMagnitude(step_interval_ms));

        app_encoder_sampler_transition_accum[APP_ENCODER_SAMPLER_ENCODER3] = 0;
        app_encoder_sampler_tempo_last_step_tick = now;
        AppEncoderSampler_AddPendingDelta(&app_encoder_sampler_pending_delta[APP_ENCODER_SAMPLER_ENCODER3], step_size);
    }
    else if (app_encoder_sampler_transition_accum[APP_ENCODER_SAMPLER_ENCODER3] <= -APP_ENCODER_SAMPLER_TEMPO_TRANSITIONS_PER_STEP)
    {
        uint32_t now = HAL_GetTick();
        uint32_t step_interval_ms = (app_encoder_sampler_tempo_last_step_tick == 0U)
                                  ? UINT32_MAX
                                  : (now - app_encoder_sampler_tempo_last_step_tick);
        int8_t step_size = (int8_t)(-APP_ENCODER_SAMPLER_TEMPO_DIRECTION_SIGN
                                  * AppEncoderSampler_ResolveTempoStepMagnitude(step_interval_ms));

        app_encoder_sampler_transition_accum[APP_ENCODER_SAMPLER_ENCODER3] = 0;
        app_encoder_sampler_tempo_last_step_tick = now;
        AppEncoderSampler_AddPendingDelta(&app_encoder_sampler_pending_delta[APP_ENCODER_SAMPLER_ENCODER3], step_size);
    }
}

static int8_t AppEncoderSampler_ResolveTempoStepMagnitude(uint32_t step_interval_ms)
{
    if (step_interval_ms <= APP_ENCODER_SAMPLER_TEMPO_ACCEL_VFAST_MS)
        return APP_ENCODER_SAMPLER_TEMPO_STEP_VFAST;
    if (step_interval_ms <= APP_ENCODER_SAMPLER_TEMPO_ACCEL_FAST_MS)
        return APP_ENCODER_SAMPLER_TEMPO_STEP_FAST;
    if (step_interval_ms <= APP_ENCODER_SAMPLER_TEMPO_ACCEL_MID_MS)
        return APP_ENCODER_SAMPLER_TEMPO_STEP_MID;

    return 1;
}