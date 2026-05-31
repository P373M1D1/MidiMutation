#include "app/app_input_irq.h"

#include "app/app_input.h"
#include "app/app_input_sampling.h"
#include "main.h"

void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin)
{
    AppInput_HandleGpioExti(gpio_pin);
}

void AppInputIrq_HandleSamplerTimer(void)
{
    AppInputSampling_HandleTimerIrq();
}