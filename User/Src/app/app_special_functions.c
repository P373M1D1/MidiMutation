#include "app/app_special_functions.h"

#include "led_functions.h"

static uint8_t app_special_functions_active = 0U;

uint8_t AppSpecialFunctions_IsActive(void)
{
    return app_special_functions_active;
}

uint8_t AppSpecialFunctions_Toggle(void)
{
    app_special_functions_active = (app_special_functions_active == 0U) ? 1U : 0U;
    LED_SetSpecialFunctionIndicator(app_special_functions_active);
    return app_special_functions_active;
}

void AppSpecialFunctions_Reset(void)
{
    app_special_functions_active = 0U;
    LED_SetSpecialFunctionIndicator(0U);
}