#include "app/app_input_encoder_switches.h"

#include "app/app_encoder_sampler.h"
#include "app/app_input.h"
#include "app/app_requests.h"
#include "main.h"

#include <stdio.h>

#define ENCODER_CHECK_SERIAL_ENABLED 0U
#define ENCODER_SWITCH_DEBOUNCE_MS 20U

static volatile uint8_t app_input_encoder_button_log_pending_mask = 0U;
static uint8_t app_input_encoder_switch_raw_level[3] = {1U, 1U, 1U};
static uint8_t app_input_encoder_switch_stable_level[3] = {1U, 1U, 1U};
static uint32_t app_input_encoder_switch_last_change_tick[3] = {0U, 0U, 0U};

static uint8_t AppInputEncoderSwitches_ReadLevel(GPIO_TypeDef *gpio_port, uint16_t gpio_pin);
static uint8_t AppInputEncoderSwitches_Encoder2ReadSwitchLevel(void);
static void AppInputEncoderSwitches_QueueButtonPressLog(uint8_t encoder_index);
static void AppInputEncoderSwitches_UpdateSwitchState(uint8_t event_index,
                                                      uint8_t raw_level);

/* Seeds encoder-switch debounce state from the current GPIO levels. */
void AppInputEncoderSwitches_Init(void)
{
    uint32_t now = HAL_GetTick();

    app_input_encoder_switch_raw_level[0] = AppInputEncoderSwitches_ReadLevel(ENC1_SW_GPIO_Port, ENC1_SW_Pin);
    app_input_encoder_switch_raw_level[1] = AppInputEncoderSwitches_Encoder2ReadSwitchLevel();
    app_input_encoder_switch_raw_level[2] = AppInputEncoderSwitches_ReadLevel(ENC3_SW_GPIO_Port, ENC3_SW_Pin);
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

/* Samples all encoder switches and advances their debounce state. */
void AppInputEncoderSwitches_Sample(void)
{
    uint8_t encoder1_sw_level = AppInputEncoderSwitches_ReadLevel(ENC1_SW_GPIO_Port, ENC1_SW_Pin);
    uint8_t encoder2_sw_level = AppInputEncoderSwitches_Encoder2ReadSwitchLevel();
    uint8_t encoder3_sw_level = AppInputEncoderSwitches_ReadLevel(ENC3_SW_GPIO_Port, ENC3_SW_Pin);

    AppInputEncoderSwitches_UpdateSwitchState(0U, encoder1_sw_level);
    AppInputEncoderSwitches_UpdateSwitchState(1U, encoder2_sw_level);
    AppInputEncoderSwitches_UpdateSwitchState(2U, encoder3_sw_level);
}

/* Flushes deferred encoder-switch diagnostics from the foreground loop. */
void AppInputEncoderSwitches_ProcessPending(void)
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

/* Claims encoder-switch EXTI lines; debounced sampling queues the real press events. */
uint8_t AppInputEncoderSwitches_HandleExti(uint16_t gpio_pin)
{
    if (gpio_pin == ENC1_SW_Pin)
        return 1U;

    if (gpio_pin == ENC2_SW_Pin)
        return 1U;

    if (gpio_pin == ENC3_SW_Pin)
        return 1U;

    return 0U;
}

/* Returns the debounced pressed state of encoder 2's push switch. */
uint8_t AppInput_Encoder2SwitchIsPressed(void)
{
    return (app_input_encoder_switch_stable_level[1] == 0U) ? 1U : 0U;
}

static uint8_t AppInputEncoderSwitches_ReadLevel(GPIO_TypeDef *gpio_port, uint16_t gpio_pin)
{
    return ((gpio_port->IDR & gpio_pin) != 0U) ? 1U : 0U;
}

static uint8_t AppInputEncoderSwitches_Encoder2ReadSwitchLevel(void)
{
    return AppInputEncoderSwitches_ReadLevel(ENC2_SW_GPIO_Port, ENC2_SW_Pin);
}

static void AppInputEncoderSwitches_QueueButtonPressLog(uint8_t encoder_index)
{
    uint8_t event_index = (uint8_t)(encoder_index - 1U);
    app_input_encoder_button_log_pending_mask |= (uint8_t)(1U << event_index);
}

static void AppInputEncoderSwitches_UpdateSwitchState(uint8_t event_index,
                                                      uint8_t raw_level)
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
        App_QueueEncoderPressEvent((uint8_t)(1U << event_index), now);
        AppInputEncoderSwitches_QueueButtonPressLog((uint8_t)(event_index + 1U));
    }
}
