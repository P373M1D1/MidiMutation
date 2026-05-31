#include "app/app_button_monitor.h"

#include "app/app_button_combo.h"
#include "main.h"

#include "led_functions.h"

#include <stdio.h>

#define APP_BUTTON_MONITOR_FOOTSWITCH_COUNT 11U
#define APP_BUTTON_MONITOR_MUTE_DEBUG_ENABLED 1U

#if BUTTON_LED_MONITOR_ENABLED
static const char * const app_button_monitor_input_labels[APP_BUTTON_MONITOR_FOOTSWITCH_COUNT] = {
    "PE0",
    "PE1",
    "PE2",
    "PE11",
    "PE12",
    "PE5",
    "PE6",
    "PE7",
    "PE8",
    "PE9",
    "PE10",
};
#endif

void AppButtonMonitor_Init(void)
{
#if BUTTON_LED_MONITOR_ENABLED
    LED_ClearButtonMonitorIndicators();
    printf("\r\nButton/LED monitor ready on USART3 @ 115200\r\n");
    printf("Press a footswitch: firmware will print the detected button number and light its mapped LED.\r\n");
    printf("Tap footswitch on PG15 is also reported in monitor output.\r\n");
    printf("Normal preset/button actions are bypassed while BUTTON_LED_MONITOR_ENABLED=1.\r\n");
#endif
}

uint8_t AppButtonMonitor_HandleFootswitchPress(uint8_t index)
{
#if BUTTON_LED_MONITOR_ENABLED
    if (index >= APP_BUTTON_MONITOR_FOOTSWITCH_COUNT)
        return 1U;

    LED_ShowButtonMonitorIndicator(index);
    printf("BTNMON button=%u input=%s led=%s\r\n",
           (unsigned)(index + 1U),
           app_button_monitor_input_labels[index],
           LED_GetButtonMonitorLabel(index));
    return 1U;
#else
    (void)index;
    return 0U;
#endif
}

uint8_t AppButtonMonitor_HandleTapPress(void)
{
#if BUTTON_LED_MONITOR_ENABLED
    LED_TapPressPulse();
    printf("BTNMON tap input=PG15 led=PF10(TAP_PRESS)\r\n");
    return 1U;
#else
    return 0U;
#endif
}

void AppButtonMonitor_LogMuteState(const char *tag,
                                   uint32_t now,
                                   uint8_t logical_pressed)
{
#if APP_BUTTON_MONITOR_MUTE_DEBUG_ENABLED
    uint8_t tap_held = (HAL_GPIO_ReadPin(TAP_GPIO_Port, TAP_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
    uint8_t mute_held = (HAL_GPIO_ReadPin(PRESET_BTN_GPIO_Port, PRESET_BTN11_Pin) == GPIO_PIN_RESET) ? 1U : 0U;

    printf("MUTEDBG %-12s t=%lu pressed=%u pending=%u tapHeld=%u muteHeld=%u\r\n",
           tag,
           (unsigned long)now,
           (unsigned)logical_pressed,
           (unsigned)AppButtonCombo_IsMuteActivationPending(),
           (unsigned)tap_held,
           (unsigned)mute_held);
#else
    (void)tag;
    (void)now;
    (void)logical_pressed;
#endif
}