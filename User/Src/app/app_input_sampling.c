#include "app/app_input_sampling.h"

#include "app/app_encoder_sampler.h"
#include "app/app_input_encoder_switches.h"
#include "main.h"

#define APP_INPUT_SAMPLER_TIMER_TICK_HZ 1000000U
#define APP_INPUT_SAMPLER_TIMER_PRESCALER_DIVISOR 96U
#define APP_INPUT_ENCODER_SAMPLE_HZ 2000U
#define APP_INPUT_SAMPLER_IRQ_PREEMPT_PRIORITY 3U
#define APP_INPUT_SAMPLER_IRQ_SUBPRIORITY 0U

static TIM_HandleTypeDef app_input_sampler_timer;

static void AppInputSampling_SampleInputs(void);

/* Starts the periodic TIM7 sampler used for encoder and switch sampling. */
void AppInputSampling_Init(void)
{
    __HAL_RCC_TIM7_CLK_ENABLE();
    app_input_sampler_timer.Instance = TIM7;
    app_input_sampler_timer.Init.Prescaler = APP_INPUT_SAMPLER_TIMER_PRESCALER_DIVISOR - 1U;
    app_input_sampler_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    app_input_sampler_timer.Init.Period = (APP_INPUT_SAMPLER_TIMER_TICK_HZ / APP_INPUT_ENCODER_SAMPLE_HZ) - 1U;
    app_input_sampler_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&app_input_sampler_timer) != HAL_OK)
        Error_Handler();

    __HAL_TIM_CLEAR_FLAG(&app_input_sampler_timer, TIM_FLAG_UPDATE);
    HAL_NVIC_SetPriority(TIM7_IRQn,
                         APP_INPUT_SAMPLER_IRQ_PREEMPT_PRIORITY,
                         APP_INPUT_SAMPLER_IRQ_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(TIM7_IRQn);

    if (HAL_TIM_Base_Start_IT(&app_input_sampler_timer) != HAL_OK)
        Error_Handler();
}

/* Services the TIM7 sampling interrupt and dispatches the sampled inputs. */
void AppInputSampling_HandleTimerIrq(void)
{
    if ((TIM7->SR & TIM_SR_UIF) == 0U)
        return;

    TIM7->SR = ~TIM_SR_UIF;
    AppInputSampling_SampleInputs();
}

static void AppInputSampling_SampleInputs(void)
{
    AppInputEncoderSwitches_Sample();
    AppEncoderSampler_SampleInterrupt();
}