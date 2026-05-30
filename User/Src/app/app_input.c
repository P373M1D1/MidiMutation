#include "app/app_input.h"

#include "main.h"

#include "app/app_button_monitor.h"
#include "app/app_encoder_sampler.h"
#include "app/app_expression_input.h"
#include "app/app_footswitch_input.h"
#include "app/app_input_encoder_switches.h"
#include "app/app_input_sampling.h"
#include "app/app_requests.h"
#include "app_event.h"
#include "display_functions.h"

#include <stdio.h>

#define ENCODER_CHECK_SERIAL_ENABLED      0U
#define APP_INPUT_TAP_DEBOUNCE_MS         20U

static uint8_t app_input_tap_press_latched = 0U;
static uint32_t app_input_tap_last_event_tick = 0U;

static void AppInput_EncoderCheckLogTurn(uint8_t encoder_index, int8_t delta);
static void AppInput_EncoderProcessPendingMotion(AppEncoderSamplerId_t encoder_id,
                                                 uint8_t encoder_index,
                                                 uint8_t event_source);
static void AppInput_RecordEncoderActivity(void);

/* Initializes the input samplers, ADC path, and encoder switch handling. */
void AppInput_Init(void)
{
    AppEncoderSampler_Init();
    AppExpressionInput_Init();
    AppInputEncoderSwitches_Init();
    AppInputSampling_Init();
}


/* Processes deferred input work that does not belong in interrupt context. */
void AppInput_ProcessPending(void)
{
    AppFootswitchInput_ProcessPending();
    AppExpressionInput_ProcessPending();

    if (app_input_tap_press_latched
        && (HAL_GPIO_ReadPin(TAP_GPIO_Port, TAP_Pin) != GPIO_PIN_RESET))
    {
        app_input_tap_press_latched = 0U;
    }

    AppInput_EncoderProcessPendingMotion(APP_ENCODER_SAMPLER_ENCODER1,
                                         1U,
                                         APP_EVENT_SOURCE_ENC1);
    AppInput_EncoderProcessPendingMotion(APP_ENCODER_SAMPLER_ENCODER2,
                                         2U,
                                         APP_EVENT_SOURCE_ENC2);
    AppInput_EncoderProcessPendingMotion(APP_ENCODER_SAMPLER_ENCODER3,
                                         3U,
                                         APP_EVENT_SOURCE_ENC3);
    AppInputEncoderSwitches_ProcessPending();
}

/* Routes a GPIO EXTI edge to the relevant input subsystem. */
void AppInput_HandleGpioExti(uint16_t gpio_pin)
{
    uint32_t now;

    if (AppInputEncoderSwitches_HandleExti(gpio_pin) != 0U)
        return;

    /* TAP now publishes an app event so tempo calculation stays in the
     * foreground. The other buttons still use their existing deferred path. */
    if (gpio_pin != TAP_Pin)
    {
        AppFootswitchInput_HandleGpioExti(gpio_pin);
        return;
    }

    if (AppButtonMonitor_HandleTapPress())
        return;

    now = HAL_GetTick();
    if ((now - app_input_tap_last_event_tick) < APP_INPUT_TAP_DEBOUNCE_MS)
        return;

    if (app_input_tap_press_latched)
        return;

    if (HAL_GPIO_ReadPin(TAP_GPIO_Port, TAP_Pin) != GPIO_PIN_RESET)
        return;

    app_input_tap_press_latched = 1U;
    app_input_tap_last_event_tick = now;

    {
        AppEvent_t tap_event = {
            APP_EVENT_TYPE_TAP_PRESS,
            APP_EVENT_SOURCE_TAP,
            0,
            now
        };

        (void)AppEvent_Push(&tap_event);
    }
}

static void AppInput_EncoderCheckLogTurn(uint8_t encoder_index, int8_t delta)
{
#if ENCODER_CHECK_SERIAL_ENABLED
    if (delta > 0)
        printf("Encoder %u CW\r\n", (unsigned)encoder_index);
    else if (delta < 0)
        printf("Encoder %u CCW\r\n", (unsigned)encoder_index);
#else
    (void)encoder_index;
    (void)delta;
#endif
}

static void AppInput_EncoderProcessPendingMotion(AppEncoderSamplerId_t encoder_id,
                                                 uint8_t encoder_index,
                                                 uint8_t event_source)
{
    uint32_t event_tick;
    uint8_t activity_pending;
    int8_t pending_delta;

    AppEncoderSampler_TakePendingMotion(encoder_id, &activity_pending, &pending_delta);

    event_tick = HAL_GetTick();

    if (pending_delta != 0)
        AppInput_EncoderCheckLogTurn(encoder_index, pending_delta);

    if (!activity_pending && (pending_delta == 0))
        return;

    if (Display_ScreensaverIsActive())
    {
        AppInput_RecordEncoderActivity();
        return;
    }

    App_QueueScreensaverActivityEvent();

    App_QueueEncoderTurnEvent(event_source, pending_delta, event_tick);
}

static void AppInput_RecordEncoderActivity(void)
{
    uint8_t screensaver_was_active = Display_ScreensaverIsActive();

    App_QueueScreensaverWakeEvent();

    if (screensaver_was_active)
        App_QueueRedrawMainScreenEvent();
}