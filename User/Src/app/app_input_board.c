#include "app/app_input_board.h"

#include "main.h"

#define APP_INPUT_BOARD_EXTI_PREEMPT_PRIORITY 3U
#define APP_INPUT_BOARD_EXTI_SUBPRIORITY 0U

void AppInputBoard_InitGpio(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    /* Configure the Nucleo user button as a second falling-edge random trigger. */
    gpio_init.Pin = USER_Btn_Pin;
    gpio_init.Mode = GPIO_MODE_IT_FALLING;
    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(USER_Btn_GPIO_Port, &gpio_init);

    /* Tap footswitch is active-low and only needs the press edge. */
    gpio_init.Pin = TAP_Pin;
    gpio_init.Mode = GPIO_MODE_IT_FALLING;
    gpio_init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(TAP_GPIO_Port, &gpio_init);

    /* Preset/special footswitches use falling-edge EXTI so the foreground path
     * sees a stable press event instead of inferring it from a later GPIO read. */
    gpio_init.Pin = PRESET_BTN1_Pin | PRESET_BTN2_Pin | PRESET_BTN3_Pin | PRESET_BTN4_Pin |
                    PRESET_BTN5_Pin | PRESET_BTN6_Pin | PRESET_BTN7_Pin | PRESET_BTN8_Pin |
                    PRESET_BTN9_Pin | PRESET_BTN10_Pin;
    gpio_init.Mode = GPIO_MODE_IT_FALLING;
    gpio_init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(PRESET_BTN_GPIO_Port, &gpio_init);

    /* Mute still needs both edges for hold/release behavior. */
    gpio_init.Pin = PRESET_BTN11_Pin;
    gpio_init.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpio_init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(PRESET_BTN_GPIO_Port, &gpio_init);

    /* Rotary 1 A/B stay on plain inputs because EXTI11/12 are already used by
     * PE11/12 preset footswitches. */
    gpio_init.Pin = ENC1_CLK_Pin | ENC1_DT_Pin;
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(ENC1_CLK_GPIO_Port, &gpio_init);

    gpio_init.Pin = ENC1_SW_Pin;
    gpio_init.Mode = GPIO_MODE_IT_FALLING;
    gpio_init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(ENC1_SW_GPIO_Port, &gpio_init);

    /* Encoder 2 motion is sampled from TIM7; its switch uses EXTI4. */
    gpio_init.Pin = ENC2_CLK_Pin | ENC2_DT_Pin;
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(ENC2_CLK_GPIO_Port, &gpio_init);

    gpio_init.Pin = ENC2_SW_Pin;
    gpio_init.Mode = GPIO_MODE_IT_FALLING;
    gpio_init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(ENC2_SW_GPIO_Port, &gpio_init);

    /* Encoder 3 motion is sampled from TIM7; its switch uses EXTI3. */
    gpio_init.Pin = ENC3_CLK_Pin | ENC3_DT_Pin;
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(ENC3_CLK_GPIO_Port, &gpio_init);

    gpio_init.Pin = ENC3_SW_Pin;
    gpio_init.Mode = GPIO_MODE_IT_FALLING;
    gpio_init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(ENC3_SW_GPIO_Port, &gpio_init);

    HAL_NVIC_SetPriority(EXTI0_IRQn, APP_INPUT_BOARD_EXTI_PREEMPT_PRIORITY, APP_INPUT_BOARD_EXTI_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
    HAL_NVIC_SetPriority(EXTI1_IRQn, APP_INPUT_BOARD_EXTI_PREEMPT_PRIORITY, APP_INPUT_BOARD_EXTI_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(EXTI1_IRQn);
    HAL_NVIC_SetPriority(EXTI2_IRQn, APP_INPUT_BOARD_EXTI_PREEMPT_PRIORITY, APP_INPUT_BOARD_EXTI_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(EXTI2_IRQn);
    HAL_NVIC_SetPriority(EXTI3_IRQn, APP_INPUT_BOARD_EXTI_PREEMPT_PRIORITY, APP_INPUT_BOARD_EXTI_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(EXTI3_IRQn);
    HAL_NVIC_SetPriority(EXTI4_IRQn, APP_INPUT_BOARD_EXTI_PREEMPT_PRIORITY, APP_INPUT_BOARD_EXTI_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(EXTI4_IRQn);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, APP_INPUT_BOARD_EXTI_PREEMPT_PRIORITY, APP_INPUT_BOARD_EXTI_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, APP_INPUT_BOARD_EXTI_PREEMPT_PRIORITY, APP_INPUT_BOARD_EXTI_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}