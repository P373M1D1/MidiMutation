/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H /* include guard for CubeMX-generated board pin declarations */

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define USER_Btn_Pin GPIO_PIN_13 /* Nucleo user pushbutton GPIO pin number */
#define USER_Btn_GPIO_Port GPIOC /* GPIO port that carries the Nucleo user pushbutton */
#define TAP_Pin GPIO_PIN_15 /* tap-tempo footswitch GPIO pin number */
#define TAP_GPIO_Port GPIOG /* GPIO port that carries the tap-tempo footswitch */
#define PRESET_BTN1_Pin GPIO_PIN_0 /* preset footswitch 1 GPIO pin number */
#define PRESET_BTN2_Pin GPIO_PIN_1 /* preset footswitch 2 GPIO pin number */
#define PRESET_BTN3_Pin GPIO_PIN_2 /* preset footswitch 3 GPIO pin number */
#define PRESET_BTN4_Pin GPIO_PIN_11 /* preset footswitch 4 GPIO pin number, moved off EXTI3 */
#define PRESET_BTN5_Pin GPIO_PIN_12 /* preset footswitch 5 GPIO pin number, moved off EXTI4 */
#define PRESET_BTN6_Pin GPIO_PIN_5 /* preset footswitch 6 GPIO pin number */
#define PRESET_BTN7_Pin GPIO_PIN_6 /* preset footswitch 7 GPIO pin number */
#define PRESET_BTN8_Pin GPIO_PIN_7 /* preset footswitch 8 GPIO pin number */
#define PRESET_BTN9_Pin GPIO_PIN_8 /* random-action footswitch GPIO pin number */
#define PRESET_BTN10_Pin GPIO_PIN_9 /* special-functions footswitch GPIO pin number */
#define PRESET_BTN11_Pin GPIO_PIN_10 /* mute / bank-combo footswitch GPIO pin number */
#define PRESET_BTN_GPIO_Port GPIOE /* GPIO port shared by the preset/special/mute footswitch bank */
#define MCO_Pin GPIO_PIN_0 /* master clock output pin number */
#define MCO_GPIO_Port GPIOH /* GPIO port that carries the master clock output */
#define LD1_Pin GPIO_PIN_0 /* board LED1 GPIO pin number */
#define LD1_GPIO_Port GPIOB /* GPIO port for board LED1 */
#define ST7796_RST_Pin GPIO_PIN_12 /* display reset pin number */
#define ST7796_RST_GPIO_Port GPIOF /* GPIO port that drives the display reset line */
#define LD3_Pin GPIO_PIN_14 /* board LED3 GPIO pin number */
#define LD3_GPIO_Port GPIOB /* GPIO port for board LED3 */
#define STLK_RX_Pin GPIO_PIN_8 /* ST-LINK virtual COM RX pin number */
#define STLK_RX_GPIO_Port GPIOD /* GPIO port for ST-LINK virtual COM RX */
#define STLK_TX_Pin GPIO_PIN_9 /* ST-LINK virtual COM TX pin number */
#define STLK_TX_GPIO_Port GPIOD /* GPIO port for ST-LINK virtual COM TX */
#define ST7796_CS_Pin GPIO_PIN_14 /* display chip-select pin number */
#define ST7796_CS_GPIO_Port GPIOD /* GPIO port that drives the display chip-select line */
#define ST7796_DC_Pin GPIO_PIN_15 /* display data/command pin number */
#define ST7796_DC_GPIO_Port GPIOD /* GPIO port that drives the display data/command line */
#define USB_PowerSwitchOn_Pin GPIO_PIN_6 /* USB power-switch enable pin number */
#define USB_PowerSwitchOn_GPIO_Port GPIOG /* GPIO port that controls the USB power switch */
#define USB_OverCurrent_Pin GPIO_PIN_7 /* USB over-current sense pin number */
#define USB_OverCurrent_GPIO_Port GPIOG /* GPIO port that reports USB over-current */
#define USB_SOF_Pin GPIO_PIN_8 /* USB start-of-frame pin number */
#define USB_SOF_GPIO_Port GPIOA /* GPIO port for USB start-of-frame */
#define USB_VBUS_Pin GPIO_PIN_9 /* USB VBUS sense pin number */
#define USB_VBUS_GPIO_Port GPIOA /* GPIO port for USB VBUS sense */
#define USB_ID_Pin GPIO_PIN_10 /* USB OTG ID pin number */
#define USB_ID_GPIO_Port GPIOA /* GPIO port for the USB OTG ID pin */
#define USB_DM_Pin GPIO_PIN_11 /* USB D- pin number */
#define USB_DM_GPIO_Port GPIOA /* GPIO port for USB D- */
#define USB_DP_Pin GPIO_PIN_12 /* USB D+ pin number */
#define USB_DP_GPIO_Port GPIOA /* GPIO port for USB D+ */
#define TMS_Pin GPIO_PIN_13 /* SWD TMS pin number */
#define TMS_GPIO_Port GPIOA /* GPIO port for SWD TMS */
#define TCK_Pin GPIO_PIN_14 /* SWD TCK pin number */
#define TCK_GPIO_Port GPIOA /* GPIO port for SWD TCK */
#define SWO_Pin GPIO_PIN_3 /* SWO trace pin number */
#define SWO_GPIO_Port GPIOB /* GPIO port for SWO trace */
#define LD2_Pin GPIO_PIN_7 /* board LED2 GPIO pin number */
#define LD2_GPIO_Port GPIOB /* GPIO port for board LED2 */

/* USER CODE BEGIN Private defines */

#define BUTTON_LED_MONITOR_ENABLED 0U /* set to 1 to turn footswitch presses into USART3 + LED numbering diagnostics instead of normal actions */

#define PRESET_LED1_Pin GPIO_PIN_0 /* preset 1 indicator LED output pin */
#define PRESET_LED1_GPIO_Port GPIOF /* GPIO port for preset 1 indicator LED */
#define PRESET_LED2_Pin GPIO_PIN_1 /* preset 2 indicator LED output pin */
#define PRESET_LED2_GPIO_Port GPIOF /* GPIO port for preset 2 indicator LED */
#define PRESET_LED3_Pin GPIO_PIN_3 /* preset 3 indicator LED output pin (PF3 due to wiring swap) */
#define PRESET_LED3_GPIO_Port GPIOF /* GPIO port for preset 3 indicator LED */
#define PRESET_LED4_Pin GPIO_PIN_2 /* preset 4 indicator LED output pin (PF2 due to wiring swap) */
#define PRESET_LED4_GPIO_Port GPIOF /* GPIO port for preset 4 indicator LED */
#define PRESET_LED5_Pin GPIO_PIN_4 /* preset 5 indicator LED output pin */
#define PRESET_LED5_GPIO_Port GPIOF /* GPIO port for preset 5 indicator LED */
#define PRESET_LED6_Pin GPIO_PIN_5 /* preset 6 indicator LED output pin */
#define PRESET_LED6_GPIO_Port GPIOF /* GPIO port for preset 6 indicator LED */
#define PRESET_LED7_Pin GPIO_PIN_6 /* preset 7 indicator LED output pin */
#define PRESET_LED7_GPIO_Port GPIOF /* GPIO port for preset 7 indicator LED */
#define PRESET_LED8_Pin GPIO_PIN_7 /* preset 8 indicator LED output pin */
#define PRESET_LED8_GPIO_Port GPIOF /* GPIO port for preset 8 indicator LED */
#define PRESET_LED9_Pin GPIO_PIN_8 /* preset 9 indicator LED output pin */
#define PRESET_LED9_GPIO_Port GPIOF /* GPIO port for preset 9 indicator LED */
#define PRESET_LED10_Pin GPIO_PIN_9 /* preset 10 indicator LED output pin */
#define PRESET_LED10_GPIO_Port GPIOF /* GPIO port for preset 10 indicator LED */
#define TAP_FEEDBACK_LED_Pin GPIO_PIN_10 /* tap footswitch press feedback LED output pin */
#define TAP_FEEDBACK_LED_GPIO_Port GPIOF /* GPIO port for tap footswitch press feedback LED */
#define PRESET_LED11_Pin GPIO_PIN_11 /* preset 11 indicator LED output pin */
#define PRESET_LED11_GPIO_Port GPIOF /* GPIO port for preset 11 indicator LED */

#define MIDI_IN_LED_Pin GPIO_PIN_15 /* dedicated MIDI input activity LED pin number */
#define MIDI_IN_LED_GPIO_Port GPIOF /* GPIO port for the MIDI input activity LED */

#define ENC1_CLK_Pin GPIO_PIN_11 /* encoder 1 quadrature A/CLK input on a free EXTI line */
#define ENC1_CLK_GPIO_Port GPIOG /* GPIO port for encoder 1 quadrature A/CLK input */
#define ENC1_DT_Pin GPIO_PIN_12 /* encoder 1 quadrature B/DT input on a free EXTI line */
#define ENC1_DT_GPIO_Port GPIOG /* GPIO port for encoder 1 quadrature B/DT input */
#define ENC1_SW_Pin GPIO_PIN_14 /* encoder 1 pushbutton switch input on a free EXTI line */
#define ENC1_SW_GPIO_Port GPIOG /* GPIO port for encoder 1 pushbutton switch input */

#define ENC2_CLK_Pin GPIO_PIN_4 /* reserved second-middle-encoder quadrature A/CLK input on TIM3_CH1-capable pin */
#define ENC2_CLK_GPIO_Port GPIOB /* GPIO port for the reserved second-middle-encoder quadrature A/CLK input */
#define ENC2_DT_Pin GPIO_PIN_5 /* reserved second-middle-encoder quadrature B/DT input on TIM3_CH2-capable pin */
#define ENC2_DT_GPIO_Port GPIOB /* GPIO port for the reserved second-middle-encoder quadrature B/DT input */
#define ENC2_SW_Pin GPIO_PIN_4 /* reserved second-middle-encoder pushbutton switch input */
#define ENC2_SW_GPIO_Port GPIOD /* GPIO port for the reserved second-middle-encoder pushbutton switch input */

#define ENC3_SW_Pin GPIO_PIN_3 /* encoder 3 pushbutton switch input on the tempo encoder */
#define ENC3_SW_GPIO_Port GPIOD /* GPIO port for the encoder 3 pushbutton switch */
#define ENC3_CLK_Pin GPIO_PIN_12 /* encoder 3 quadrature A/CLK input (tempo encoder) */
#define ENC3_CLK_GPIO_Port GPIOD /* GPIO port for encoder 3 quadrature A/CLK input */
#define ENC3_DT_Pin GPIO_PIN_13 /* encoder 3 quadrature B/DT input (tempo encoder) */
#define ENC3_DT_GPIO_Port GPIOD /* GPIO port for encoder 3 quadrature B/DT input */

#define RELAY1_Pin GPIO_PIN_10 /* Relay_1 transistor drive output: HIGH closes relay */
#define RELAY1_GPIO_Port GPIOG /* GPIO port for Relay_1 drive output */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
