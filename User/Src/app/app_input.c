#include "app/app_input.h"

#include "main.h"

#include "app/app_dispatch.h"
#include "app/app_requests.h"
#include "app_event.h"
#include "button_functions.h"
#include "display_functions.h"

#include <stdio.h>

#define ENCODER_CHECK_SERIAL_ENABLED      0U
#define ENCODER_SWITCH_DEBOUNCE_MS       20U

#define TEMPO_ENCODER_TRANSITIONS_PER_STEP 4
#define TEMPO_ENCODER_DIRECTION_SIGN      1
#define TEMPO_ENCODER_ACCEL_MID_MS       60U
#define TEMPO_ENCODER_ACCEL_FAST_MS      35U
#define TEMPO_ENCODER_ACCEL_VFAST_MS     20U
#define TEMPO_ENCODER_STEP_MID            2
#define TEMPO_ENCODER_STEP_FAST           4
#define TEMPO_ENCODER_STEP_VFAST          8
#define ROTARY1_SCROLL_TRANSITIONS_PER_STEP 4
#define ROTARY1_SCROLL_DIRECTION_SIGN   -1

static volatile uint8_t app_input_encoder_button_log_pending_mask = 0U;
static uint8_t app_input_encoder_switch_raw_level[3] = {1U, 1U, 1U};
static uint8_t app_input_encoder_switch_stable_level[3] = {1U, 1U, 1U};
static uint32_t app_input_encoder_switch_last_change_tick[3] = {0U, 0U, 0U};
static uint8_t app_input_encoder2_last_state = 0U;
static int8_t app_input_encoder2_transition_accum = 0;
static volatile int8_t app_input_encoder2_pending_steps = 0;
static volatile uint8_t app_input_encoder2_activity_pending = 0U;
static uint8_t app_input_tempo_encoder_last_state = 0U;
static int8_t app_input_tempo_encoder_transition_accum = 0;
static uint32_t app_input_tempo_encoder_last_step_tick = 0U;
static volatile int8_t app_input_tempo_encoder_pending_delta = 0;
static volatile uint8_t app_input_tempo_encoder_activity_pending = 0U;
static uint8_t app_input_rotary1_last_state = 0U;
static int8_t app_input_rotary1_transition_accum = 0;
static volatile int8_t app_input_rotary1_pending_steps = 0;
static volatile uint8_t app_input_rotary1_activity_pending = 0U;

static uint8_t AppInput_ReadLevel(GPIO_TypeDef *gpio_port, uint16_t gpio_pin);
static uint8_t AppInput_ReadState(GPIO_TypeDef *clk_gpio_port, uint16_t clk_gpio_pin,
                                  GPIO_TypeDef *dt_gpio_port, uint16_t dt_gpio_pin);
static uint8_t AppInput_Encoder2ReadSwitchLevel(void);
static void AppInput_EncoderCheckLogTurn(uint8_t encoder_index, int8_t delta);
static uint8_t AppInput_HandleEncoderSwitchExti(uint16_t gpio_pin);
static void AppInput_EncoderCheckQueueButtonPress(uint8_t encoder_index);
static void AppInput_EncoderCheckUpdateSwitchState(uint8_t event_index,
                                                   uint8_t raw_level,
                                                   volatile uint8_t *activity_pending_flag);
static void AppInput_EncoderCheckInit(void);
static void AppInput_EncoderCheckProcessPending(void);
static void AppInput_EncoderCheckSampleSwitches(void);
static int8_t AppInput_EncoderTransitionDelta(uint8_t previous_state, uint8_t current_state);
static int8_t AppInput_EncoderAccumulateTransition(int8_t transition_accum, int8_t transition_delta);
static void AppInput_EncoderAddPendingDelta(volatile int8_t *pending_delta, int8_t delta);
static void AppInput_EncoderSampleSimple(GPIO_TypeDef *clk_gpio_port,
                                         uint16_t clk_gpio_pin,
                                         GPIO_TypeDef *dt_gpio_port,
                                         uint16_t dt_gpio_pin,
                                         uint8_t *last_state,
                                         int8_t *transition_accum,
                                         volatile int8_t *pending_steps,
                                         volatile uint8_t *activity_pending,
                                         uint8_t transitions_per_step,
                                         int8_t step_sign);
static int8_t AppInput_TempoEncoderResolveStepMagnitude(uint32_t step_interval_ms);
static void AppInput_EncoderProcessPendingMotion(volatile uint8_t *activity_pending_flag,
                                                 volatile int8_t *pending_delta_flag,
                                                 uint8_t encoder_index,
                                                 uint8_t event_source);
static void AppInput_Rotary1Init(void);
static void AppInput_Rotary1SampleInterrupt(void);
static void AppInput_Rotary1RecordActivity(void);
static void AppInput_Rotary1ProcessPending(void);
static void AppInput_Encoder2Init(void);
static void AppInput_Encoder2SampleInterrupt(void);
static void AppInput_Encoder2ProcessPending(void);
static void AppInput_TempoEncoderInit(void);
static void AppInput_TempoEncoderSampleInterrupt(void);
static void AppInput_TempoEncoderProcessPending(void);

void AppInput_Init(void)
{
    AppInput_Rotary1Init();
    AppInput_Encoder2Init();
    AppInput_TempoEncoderInit();
    AppInput_EncoderCheckInit();
}

void AppInput_ProcessPending(void)
{
    AppInput_Rotary1ProcessPending();
    AppInput_Encoder2ProcessPending();
    AppInput_TempoEncoderProcessPending();
    AppInput_EncoderCheckProcessPending();
}

void AppInput_HandleGpioExti(uint16_t gpio_pin)
{
    if (AppInput_HandleEncoderSwitchExti(gpio_pin) != 0U)
        return;

    /* TAP now publishes an app event so tempo calculation stays in the
     * foreground. The other buttons still use their existing deferred path. */
    if (gpio_pin != TAP_Pin)
    {
        Button_HandleInterrupt(gpio_pin);
        return;
    }

#if BUTTON_LED_MONITOR_ENABLED
    Button_MonitorReportTapPress();
    return;
#endif

    {
        AppEvent_t tap_event = {
            APP_EVENT_TYPE_TAP_PRESS,
            APP_EVENT_SOURCE_TAP,
            0,
            HAL_GetTick()
        };

        (void)AppEvent_Push(&tap_event);
    }
}

static uint8_t AppInput_HandleEncoderSwitchExti(uint16_t gpio_pin)
{
    if (gpio_pin == ENC1_SW_Pin)
    {
        app_input_rotary1_activity_pending = 1U;
        return 1U;
    }

    if (gpio_pin == ENC2_SW_Pin)
    {
        app_input_encoder2_activity_pending = 1U;
        return 1U;
    }

    if (gpio_pin == ENC3_SW_Pin)
    {
        app_input_tempo_encoder_activity_pending = 1U;
        return 1U;
    }

    return 0U;
}

uint8_t AppInput_Encoder2SwitchIsPressed(void)
{
    return (app_input_encoder_switch_stable_level[1] == 0U) ? 1U : 0U;
}

static uint8_t AppInput_ReadLevel(GPIO_TypeDef *gpio_port, uint16_t gpio_pin)
{
    return ((gpio_port->IDR & gpio_pin) != 0U) ? 1U : 0U;
}

static uint8_t AppInput_ReadState(GPIO_TypeDef *clk_gpio_port, uint16_t clk_gpio_pin,
                                  GPIO_TypeDef *dt_gpio_port, uint16_t dt_gpio_pin)
{
    return (uint8_t)((AppInput_ReadLevel(clk_gpio_port, clk_gpio_pin) << 1U)
                   | AppInput_ReadLevel(dt_gpio_port, dt_gpio_pin));
}

static uint8_t AppInput_Encoder2ReadSwitchLevel(void)
{
    return AppInput_ReadLevel(ENC2_SW_GPIO_Port, ENC2_SW_Pin);
}

static void AppInput_EncoderCheckLogTurn(uint8_t encoder_index, int8_t delta)
{
#if ENCODER_CHECK_SERIAL_ENABLED
    if (delta > 0)
        printf("ENC%u CW\r\n", (unsigned)encoder_index);
    else if (delta < 0)
        printf("ENC%u CCW\r\n", (unsigned)encoder_index);
#else
    (void)encoder_index;
    (void)delta;
#endif
}

static void AppInput_EncoderCheckQueueButtonPress(uint8_t encoder_index)
{
    uint8_t event_index = (uint8_t)(encoder_index - 1U);
    app_input_encoder_button_log_pending_mask |= (uint8_t)(1U << event_index);
}

static void AppInput_EncoderCheckUpdateSwitchState(uint8_t event_index,
                                                   uint8_t raw_level,
                                                   volatile uint8_t *activity_pending_flag)
{
    uint32_t now = HAL_GetTick();

    if (raw_level != app_input_encoder_switch_raw_level[event_index])
    {
        app_input_encoder_switch_raw_level[event_index] = raw_level;
        app_input_encoder_switch_last_change_tick[event_index] = now;
    }

    if (raw_level == app_input_encoder_switch_stable_level[event_index])
        return;

    if ((now - app_input_encoder_switch_last_change_tick[event_index]) < ENCODER_SWITCH_DEBOUNCE_MS)
        return;

    app_input_encoder_switch_stable_level[event_index] = raw_level;
    if (raw_level == 0U)
    {
        *activity_pending_flag = 1U;
        App_QueueEncoderPressEvent((uint8_t)(1U << event_index), now);
        AppInput_EncoderCheckQueueButtonPress((uint8_t)(event_index + 1U));
    }
}

static void AppInput_EncoderCheckInit(void)
{
    uint32_t now = HAL_GetTick();

    app_input_encoder_switch_raw_level[0] = AppInput_ReadLevel(ENC1_SW_GPIO_Port, ENC1_SW_Pin);
    app_input_encoder_switch_raw_level[1] = AppInput_Encoder2ReadSwitchLevel();
    app_input_encoder_switch_raw_level[2] = AppInput_ReadLevel(ENC3_SW_GPIO_Port, ENC3_SW_Pin);
    app_input_encoder_switch_stable_level[0] = app_input_encoder_switch_raw_level[0];
    app_input_encoder_switch_stable_level[1] = app_input_encoder_switch_raw_level[1];
    app_input_encoder_switch_stable_level[2] = app_input_encoder_switch_raw_level[2];
    app_input_encoder_switch_last_change_tick[0] = now;
    app_input_encoder_switch_last_change_tick[1] = now;
    app_input_encoder_switch_last_change_tick[2] = now;
#if ENCODER_CHECK_SERIAL_ENABLED
    printf("\r\nEncoder check ready on USART3 @ 115200\r\n");
    printf("Turn encoders or press encoder switches to verify wiring.\r\n");
#endif
}

static void AppInput_EncoderCheckProcessPending(void)
{
    uint32_t primask;
    uint8_t press_mask;

    primask = __get_PRIMASK();
    __disable_irq();
    press_mask = app_input_encoder_button_log_pending_mask;
    app_input_encoder_button_log_pending_mask = 0U;
    if (primask == 0U)
        __enable_irq();

#if ENCODER_CHECK_SERIAL_ENABLED
    if (press_mask & 0x01U)
        printf("ENC1 BUTTON\r\n");
    if (press_mask & 0x02U)
        printf("ENC2 BUTTON\r\n");
    if (press_mask & 0x04U)
        printf("ENC3 BUTTON\r\n");
#else
    (void)press_mask;
#endif
}

static void AppInput_EncoderCheckSampleSwitches(void)
{
    uint8_t encoder1_sw_level = AppInput_ReadLevel(ENC1_SW_GPIO_Port, ENC1_SW_Pin);
    uint8_t encoder2_sw_level = AppInput_Encoder2ReadSwitchLevel();
    uint8_t encoder3_sw_level = AppInput_ReadLevel(ENC3_SW_GPIO_Port, ENC3_SW_Pin);

    AppInput_EncoderCheckUpdateSwitchState(0U, encoder1_sw_level, &app_input_rotary1_activity_pending);
    AppInput_EncoderCheckUpdateSwitchState(1U, encoder2_sw_level, &app_input_encoder2_activity_pending);
    AppInput_EncoderCheckUpdateSwitchState(2U, encoder3_sw_level, &app_input_tempo_encoder_activity_pending);
}

static int8_t AppInput_EncoderTransitionDelta(uint8_t previous_state, uint8_t current_state)
{
    static const int8_t transition_delta[16] = {
         0, -1,  1,  0,
         1,  0,  0, -1,
        -1,  0,  0,  1,
         0,  1, -1,  0
    };

    return transition_delta[(previous_state << 2U) | current_state];
}

static int8_t AppInput_EncoderAccumulateTransition(int8_t transition_accum, int8_t transition_delta)
{
    if (transition_delta == 0)
        return transition_accum;

    if (((transition_accum > 0) && (transition_delta < 0))
     || ((transition_accum < 0) && (transition_delta > 0)))
    {
        return transition_delta;
    }

    return (int8_t)(transition_accum + transition_delta);
}

static void AppInput_EncoderAddPendingDelta(volatile int8_t *pending_delta, int8_t delta)
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

static void AppInput_EncoderSampleSimple(GPIO_TypeDef *clk_gpio_port,
                                         uint16_t clk_gpio_pin,
                                         GPIO_TypeDef *dt_gpio_port,
                                         uint16_t dt_gpio_pin,
                                         uint8_t *last_state,
                                         int8_t *transition_accum,
                                         volatile int8_t *pending_steps,
                                         volatile uint8_t *activity_pending,
                                         uint8_t transitions_per_step,
                                         int8_t step_sign)
{
    uint8_t current_state;
    int8_t transition_delta;

    if (!last_state || !transition_accum || !pending_steps || !activity_pending)
        return;

    current_state = AppInput_ReadState(clk_gpio_port, clk_gpio_pin, dt_gpio_port, dt_gpio_pin);
    if (current_state == *last_state)
        return;

    *activity_pending = 1U;
    transition_delta = AppInput_EncoderTransitionDelta(*last_state, current_state);
    *transition_accum = AppInput_EncoderAccumulateTransition(*transition_accum, transition_delta);
    *last_state = current_state;

    if (*transition_accum >= (int8_t)transitions_per_step)
    {
        AppInput_EncoderAddPendingDelta(pending_steps, step_sign);
        *transition_accum = 0;
    }
    else if (*transition_accum <= -(int8_t)transitions_per_step)
    {
        AppInput_EncoderAddPendingDelta(pending_steps, (int8_t)-step_sign);
        *transition_accum = 0;
    }
}

static int8_t AppInput_TempoEncoderResolveStepMagnitude(uint32_t step_interval_ms)
{
    if (step_interval_ms <= TEMPO_ENCODER_ACCEL_VFAST_MS)
        return TEMPO_ENCODER_STEP_VFAST;
    if (step_interval_ms <= TEMPO_ENCODER_ACCEL_FAST_MS)
        return TEMPO_ENCODER_STEP_FAST;
    if (step_interval_ms <= TEMPO_ENCODER_ACCEL_MID_MS)
        return TEMPO_ENCODER_STEP_MID;

    return 1;
}

static void AppInput_EncoderProcessPendingMotion(volatile uint8_t *activity_pending_flag,
                                                 volatile int8_t *pending_delta_flag,
                                                 uint8_t encoder_index,
                                                 uint8_t event_source)
{
    uint32_t primask;
    uint32_t event_tick;
    uint8_t activity_pending;
    int8_t pending_delta;

    if (!activity_pending_flag || !pending_delta_flag)
        return;

    primask = __get_PRIMASK();
    __disable_irq();
    activity_pending = *activity_pending_flag;
    pending_delta = *pending_delta_flag;
    *activity_pending_flag = 0U;
    *pending_delta_flag = 0;
    if (primask == 0U)
        __enable_irq();

    event_tick = HAL_GetTick();

    if (pending_delta != 0)
        AppInput_EncoderCheckLogTurn(encoder_index, pending_delta);

    if (!activity_pending && (pending_delta == 0))
        return;

    if (Display_ScreensaverIsActive())
    {
        AppInput_Rotary1RecordActivity();
        return;
    }

    App_QueueScreensaverActivityEvent();

    if ((pending_delta != 0)
     && !Display_MenuIsActive()
     && !Display_PresetEditIsActive())
    {
        AppDispatch_HandleEncoderTurnEvent(event_source, pending_delta);
        return;
    }

    App_QueueEncoderTurnEvent(event_source, pending_delta, event_tick);
}

static void AppInput_Rotary1Init(void)
{
    app_input_rotary1_last_state = AppInput_ReadState(ENC1_CLK_GPIO_Port, ENC1_CLK_Pin,
                                                      ENC1_DT_GPIO_Port, ENC1_DT_Pin);
    app_input_rotary1_transition_accum = 0;
    app_input_rotary1_pending_steps = 0;
    app_input_rotary1_activity_pending = 0U;
}

static void AppInput_Rotary1SampleInterrupt(void)
{
    AppInput_EncoderSampleSimple(ENC1_CLK_GPIO_Port,
                                 ENC1_CLK_Pin,
                                 ENC1_DT_GPIO_Port,
                                 ENC1_DT_Pin,
                                 &app_input_rotary1_last_state,
                                 &app_input_rotary1_transition_accum,
                                 &app_input_rotary1_pending_steps,
                                 &app_input_rotary1_activity_pending,
                                 ROTARY1_SCROLL_TRANSITIONS_PER_STEP,
                                 (int8_t)-ROTARY1_SCROLL_DIRECTION_SIGN);
}

static void AppInput_Rotary1RecordActivity(void)
{
    uint8_t screensaver_was_active = Display_ScreensaverIsActive();

    App_QueueScreensaverWakeEvent();

    if (screensaver_was_active)
        App_QueueRedrawMainScreenEvent();
}

static void AppInput_Rotary1ProcessPending(void)
{
    AppInput_EncoderProcessPendingMotion(&app_input_rotary1_activity_pending,
                                         &app_input_rotary1_pending_steps,
                                         1U,
                                         APP_EVENT_SOURCE_ENC1);
}

static void AppInput_Encoder2Init(void)
{
    app_input_encoder2_last_state = AppInput_ReadState(ENC2_CLK_GPIO_Port, ENC2_CLK_Pin,
                                                       ENC2_DT_GPIO_Port, ENC2_DT_Pin);
    app_input_encoder2_transition_accum = 0;
    app_input_encoder2_pending_steps = 0;
    app_input_encoder2_activity_pending = 0U;
}

static void AppInput_Encoder2SampleInterrupt(void)
{
    AppInput_EncoderSampleSimple(ENC2_CLK_GPIO_Port,
                                 ENC2_CLK_Pin,
                                 ENC2_DT_GPIO_Port,
                                 ENC2_DT_Pin,
                                 &app_input_encoder2_last_state,
                                 &app_input_encoder2_transition_accum,
                                 &app_input_encoder2_pending_steps,
                                 &app_input_encoder2_activity_pending,
                                 ROTARY1_SCROLL_TRANSITIONS_PER_STEP,
                                 1);
}

static void AppInput_Encoder2ProcessPending(void)
{
    AppInput_EncoderProcessPendingMotion(&app_input_encoder2_activity_pending,
                                         &app_input_encoder2_pending_steps,
                                         2U,
                                         APP_EVENT_SOURCE_ENC2);
}

static void AppInput_TempoEncoderInit(void)
{
    app_input_tempo_encoder_last_state = AppInput_ReadState(ENC3_CLK_GPIO_Port, ENC3_CLK_Pin,
                                                            ENC3_DT_GPIO_Port, ENC3_DT_Pin);
    app_input_tempo_encoder_transition_accum = 0;
    app_input_tempo_encoder_last_step_tick = 0U;
    app_input_tempo_encoder_pending_delta = 0;
    app_input_tempo_encoder_activity_pending = 0U;
}

static void AppInput_TempoEncoderSampleInterrupt(void)
{
    uint8_t current_state = AppInput_ReadState(ENC3_CLK_GPIO_Port, ENC3_CLK_Pin,
                                               ENC3_DT_GPIO_Port, ENC3_DT_Pin);
    int8_t transition_delta;

    if (current_state == app_input_tempo_encoder_last_state)
        return;

    app_input_tempo_encoder_activity_pending = 1U;
    transition_delta = AppInput_EncoderTransitionDelta(app_input_tempo_encoder_last_state, current_state);
    app_input_tempo_encoder_transition_accum = AppInput_EncoderAccumulateTransition(app_input_tempo_encoder_transition_accum,
                                                                                    transition_delta);
    app_input_tempo_encoder_last_state = current_state;

    if (app_input_tempo_encoder_transition_accum >= TEMPO_ENCODER_TRANSITIONS_PER_STEP)
    {
        uint32_t now = HAL_GetTick();
        uint32_t step_interval_ms = (app_input_tempo_encoder_last_step_tick == 0U)
                                  ? UINT32_MAX
                                  : (now - app_input_tempo_encoder_last_step_tick);
        int8_t step_size = (int8_t)(TEMPO_ENCODER_DIRECTION_SIGN
                                  * AppInput_TempoEncoderResolveStepMagnitude(step_interval_ms));

        app_input_tempo_encoder_transition_accum = 0;
        app_input_tempo_encoder_last_step_tick = now;
        AppInput_EncoderAddPendingDelta(&app_input_tempo_encoder_pending_delta, step_size);
    }
    else if (app_input_tempo_encoder_transition_accum <= -TEMPO_ENCODER_TRANSITIONS_PER_STEP)
    {
        uint32_t now = HAL_GetTick();
        uint32_t step_interval_ms = (app_input_tempo_encoder_last_step_tick == 0U)
                                  ? UINT32_MAX
                                  : (now - app_input_tempo_encoder_last_step_tick);
        int8_t step_size = (int8_t)(-TEMPO_ENCODER_DIRECTION_SIGN
                                  * AppInput_TempoEncoderResolveStepMagnitude(step_interval_ms));

        app_input_tempo_encoder_transition_accum = 0;
        app_input_tempo_encoder_last_step_tick = now;
        AppInput_EncoderAddPendingDelta(&app_input_tempo_encoder_pending_delta, step_size);
    }
}

static void AppInput_TempoEncoderProcessPending(void)
{
    AppInput_EncoderProcessPendingMotion(&app_input_tempo_encoder_activity_pending,
                                         &app_input_tempo_encoder_pending_delta,
                                         3U,
                                         APP_EVENT_SOURCE_ENC3);
}

void App_EncoderSampleIRQHandler(void)
{
    if ((TIM7->SR & TIM_SR_UIF) == 0U)
        return;

    TIM7->SR = ~TIM_SR_UIF;
    AppInput_EncoderCheckSampleSwitches();
    AppInput_Rotary1SampleInterrupt();
    AppInput_Encoder2SampleInterrupt();
    AppInput_TempoEncoderSampleInterrupt();
}