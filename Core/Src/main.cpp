// Activate preset by index (0-3)

/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "st7796.h"
#include "fonts.h"
#include "image.h"
#include "display_functions.h"
#include "button_functions.h"
#include "bpm_functions.h"
#include "led_functions.h"
#include "midi_functions.h"
#include "midi_devices.h"
#include "presets.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* Preset_t is defined in presets.h */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;
TIM_HandleTypeDef htim6;

UART_HandleTypeDef huart3;

PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* USER CODE BEGIN PV */
/* ── Tap tempo state ──────────────────────────────────────────────────────── */
#define TAP_BUF_SIZE 4U
static volatile uint32_t tap_ts[TAP_BUF_SIZE]; /* tap timestamps (ms)      */
static volatile uint8_t  tap_count = 0U;        /* valid entries in buffer  */
static volatile uint8_t  tap_head  = 0U;        /* circular write pointer   */
volatile uint16_t g_bpm     = 120U;      /* live BPM value           */
volatile uint8_t         bpm_dirty    = 0U;  /* set by ISR, read by main */
volatile uint32_t bpm_save_tick = 0U;  /* HAL_GetTick target to save BPM to Flash */
const Preset_t   *active_preset = NULL; /* current preset, needed by screensaver wake */
uint8_t active_preset_index = PRESET_DEFAULT;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_USB_OTG_FS_PCD_Init(void);
/* USER CODE BEGIN PFP */
static void MX_TIM2_Init(void);
static void MX_TIM6_Init(uint16_t bpm);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_USART3_UART_Init();
  MX_USB_OTG_FS_PCD_Init();
  /* USER CODE BEGIN 2 */
  Display_BL_Init();
  MidiInitInput();
  MIDI_InitPort(0, UART4, GPIOC, GPIO_PIN_10, GPIO_AF8_UART4);  /* Echosystem – TRS-A */
  MIDI_InitPort(1, UART5, GPIOC, GPIO_PIN_12, GPIO_AF8_UART5);  /* Reverb     – DIN-5 */
  ST7796_Init();
  MX_TIM2_Init();
  ST7796_DrawImageSwapRB(0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, image_data);
  Display_BL_FadeIn();
  /* BPM flash status — top-left corner, visible during loading bar */
  ST7796_WriteString(10U, 10U,
      BPM_Flash_IsValid() ? "flash_OK" : "flash_notOK",
      Font_7x10, ST7796_DARKGRAY, ST7796_BLACK);
  //Display_LoadingBar(7000U);
  Display_LoadingBar(1000U);
  Display_LoadingBarClear();
  Display_BL_FadeOut();
  ST7796_FillScreen(ST7796_BLACK);  /* clear while backlight is off – invisible */
  Display_BL_FadeIn();
  g_bpm = BPM_Flash_Load();
  if (!BPM_Flash_IsValid())
  {
      /* Flash blank or corrupt — using default BPM */
      g_bpm = BPM_DEFAULT;
  }
    current_bank = BPM_Flash_LoadBankIndex();
  active_preset_index = BPM_Flash_LoadPresetIndex();
    if ((active_preset_index / PRESETS_PER_BANK) != current_bank)
    {
      active_preset_index = (uint8_t)(current_bank * PRESETS_PER_BANK);
    }
  MX_TIM6_Init(g_bpm);
  HAL_TIM_Base_Start_IT(&htim6);
  App_ActivatePreset(active_preset_index);
  bpm_save_tick = 0U;
  Display_ScreensaverActivity();  /* seed inactivity timer from boot */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    Handle_Tap_Tempo();
    Button_CheckAndHandle();
    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{
  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 384;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 8;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief USB_OTG_FS Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_OTG_FS_PCD_Init(void)
{

  /* USER CODE BEGIN USB_OTG_FS_Init 0 */

  /* USER CODE END USB_OTG_FS_Init 0 */

  /* USER CODE BEGIN USB_OTG_FS_Init 1 */

  /* USER CODE END USB_OTG_FS_Init 1 */
  hpcd_USB_OTG_FS.Instance = USB_OTG_FS;
  hpcd_USB_OTG_FS.Init.dev_endpoints = 6;
  hpcd_USB_OTG_FS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_OTG_FS.Init.dma_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_OTG_FS.Init.Sof_enable = ENABLE;
  hpcd_USB_OTG_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.lpm_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.battery_charging_enable = ENABLE;
  hpcd_USB_OTG_FS.Init.vbus_sensing_enable = ENABLE;
  hpcd_USB_OTG_FS.Init.use_dedicated_ep1 = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_OTG_FS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_OTG_FS_Init 2 */

  /* USER CODE END USB_OTG_FS_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LD1_Pin|LD3_Pin|LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(USB_PowerSwitchOn_GPIO_Port, USB_PowerSwitchOn_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level - ST7796 control pins */
  HAL_GPIO_WritePin(ST7796_RST_GPIO_Port, ST7796_RST_Pin, GPIO_PIN_SET);   /* RST high = not in reset */
  HAL_GPIO_WritePin(ST7796_CS_GPIO_Port,  ST7796_CS_Pin,  GPIO_PIN_SET);   /* CS high = deselected */
  HAL_GPIO_WritePin(ST7796_DC_GPIO_Port,  ST7796_DC_Pin,  GPIO_PIN_SET);   /* DC high = data */
  HAL_GPIO_WritePin(MIDI_IN_LED_GPIO_Port, MIDI_IN_LED_Pin, GPIO_PIN_RESET);


  /*Configure GPIO pin : PG15 tap tempo footswitch (active-low, falling edge = press) */
  GPIO_InitStruct.Pin = TAP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(TAP_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PE0-PE10 footswitches (active-low, falling edge, pull-up) */
  GPIO_InitStruct.Pin = PRESET_BTN1_Pin | PRESET_BTN2_Pin | PRESET_BTN3_Pin | PRESET_BTN4_Pin |
                        PRESET_BTN5_Pin | PRESET_BTN6_Pin | PRESET_BTN7_Pin | PRESET_BTN8_Pin |
                        PRESET_BTN9_Pin | PRESET_BTN10_Pin | PRESET_BTN11_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(PRESET_BTN_GPIO_Port, &GPIO_InitStruct);

  /* Enable EXTI IRQs for PE0-PE10 footswitches */
  HAL_NVIC_SetPriority(EXTI0_IRQn, 3U, 0U); HAL_NVIC_EnableIRQ(EXTI0_IRQn);
  HAL_NVIC_SetPriority(EXTI1_IRQn, 3U, 0U); HAL_NVIC_EnableIRQ(EXTI1_IRQn);
  HAL_NVIC_SetPriority(EXTI2_IRQn, 3U, 0U); HAL_NVIC_EnableIRQ(EXTI2_IRQn);
  HAL_NVIC_SetPriority(EXTI3_IRQn, 3U, 0U); HAL_NVIC_EnableIRQ(EXTI3_IRQn);
  HAL_NVIC_SetPriority(EXTI4_IRQn, 3U, 0U); HAL_NVIC_EnableIRQ(EXTI4_IRQn);
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 3U, 0U); HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 3U, 0U); HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /*Configure GPIO pins : LD1_Pin LD3_Pin LD2_Pin */
  GPIO_InitStruct.Pin = LD1_Pin|LD3_Pin|LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_PowerSwitchOn_Pin */
  GPIO_InitStruct.Pin = USB_PowerSwitchOn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(USB_PowerSwitchOn_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_OverCurrent_Pin */
  GPIO_InitStruct.Pin = USB_OverCurrent_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USB_OverCurrent_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* EXTI15_10 is shared by PE10 footswitch and PG15 tap tempo. */
  /* ST7796 control pins: RST=PF12, CS=PD14, DC=PD15 */
  GPIO_InitStruct.Pin = ST7796_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(ST7796_RST_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = ST7796_CS_Pin | ST7796_DC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(ST7796_CS_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = MIDI_IN_LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(MIDI_IN_LED_GPIO_Port, &GPIO_InitStruct);
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

// OWN EDIT: Using TIM2 for MIDI clock pulse instead of HAL(getTick) because HAL tick is too coarse (1 ms) for accurate BPM measurement at higher tempos.
void MX_TIM2_Init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();
    TIM_HandleTypeDef htim2;
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 95; // 96MHz / (95+1) = 1MHz (1us per tick)
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 0xFFFFFFFF;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
    {
        Error_Handler();
    }
    HAL_TIM_Base_Start(&htim2);
}

/* ── TIM6 init: APB1 timer clock = 96 MHz ────────────────────────────────────
 * Prescaler 9600-1 → 10 kHz tick (0.1 ms resolution).
 * ARR = (600 000 / BPM) - 1  → fires once per full beat.
 *   20  BPM → ARR 29 999 (3 000 ms)
 *   120 BPM → ARR  4 999 (  500 ms)
 *   240 BPM → ARR  2 499 (  250 ms)
 */
static void MX_TIM6_Init(uint16_t bpm)
{
  __HAL_RCC_TIM6_CLK_ENABLE();
  htim6.Instance               = TIM6;
  htim6.Init.Prescaler         = 9600U - 1U;
  htim6.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim6.Init.Period            = (600000U / (uint32_t)bpm) - 1U;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
    Error_Handler();
  __HAL_TIM_CLEAR_FLAG(&htim6, TIM_FLAG_UPDATE); /* clear UIF set by UG during init */
  HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 2U, 0U);
  HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
}
  

/* ── USER button EXTI: tap tempo ──────────────────────────────────────────
 * Records timestamps of the last TAP_BUF_SIZE presses, averages the
 * intervals, and updates TIM6 ARR + display.
 * Resets history if gap > 3 000 ms (< 20 BPM).
 * Ignores taps < 250 ms apart (> 240 BPM / debounce).
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  uint8_t screensaver_was_active = Display_ScreensaverIsActive();

  // Preset switches are polled in Button_CheckAndHandle().

  if (GPIO_Pin != TAP_Pin) return;

  uint32_t now = HAL_GetTick();

    Display_ScreensaverDismiss();
    Display_ScreensaverActivity();  /* any tap = user activity */

    if (screensaver_was_active)
    {
      Display_DrawMainScreen(active_preset ? active_preset : Presets_Get(current_bank * PRESETS_PER_BANK), g_bpm);
      return;
    }

    if (Button_HandleTapPress(now)) {
      Button_CancelTapBankCombo();
      Display_DrawMainScreen(Presets_Get(current_bank * PRESETS_PER_BANK), g_bpm);
      return;
    }

  /* Ignore tap tempo while an external MIDI clock is actively running */
  if (MidiTransportIsRunning())
    return;

  if (tap_count > 0U)
  {
    uint8_t  prev     = (uint8_t)((tap_head + TAP_BUF_SIZE - 1U) % TAP_BUF_SIZE);
    uint32_t interval = now - tap_ts[prev];

    if (interval > 3000U)          /* too slow – reset */
    {
      tap_count = 0U;
      tap_head  = 0U;
    }
    else if (interval < 250U)      /* too fast / bounce – ignore */
    {
      return;
    }
  }

  /* Record tap */
  tap_ts[tap_head] = now;
  tap_head = (uint8_t)((tap_head + 1U) % TAP_BUF_SIZE);
  if (tap_count < TAP_BUF_SIZE) tap_count++;

  if (tap_count < 2U) return; /* need at least two taps */

  /* Average all consecutive intervals in the buffer */
  uint32_t sum = 0U;
  uint8_t  n   = tap_count;
  for (uint8_t i = 0U; i < n - 1U; i++)
  {
    uint8_t a = (uint8_t)((tap_head + TAP_BUF_SIZE - n + i)      % TAP_BUF_SIZE);
    uint8_t b = (uint8_t)((tap_head + TAP_BUF_SIZE - n + i + 1U) % TAP_BUF_SIZE);
    sum += tap_ts[b] - tap_ts[a];
  }
  uint32_t avg_ms = sum / (uint32_t)(n - 1U);
  if (avg_ms == 0U) return;

  //uint32_t new_bpm = 60000U / avg_ms;
  uint32_t new_bpm = (uint32_t)((60000.0f / (float)avg_ms) + 0.5f);
  if (new_bpm < 20U || new_bpm > 240U) return;

  g_bpm = (uint16_t)new_bpm;

  /* Tapping takes us back to internal tempo — clear any external sync state
   * so Display_UpdateBPM doesn't stay stuck on "EXT SYNC LOST". */
  MidiClockUseInternalTempo();

  /* Sync LED to this tap and update blink rate */
  TIM6->CNT = 0U;
  TIM6->ARR = (600000U / new_bpm) - 1U;
  LED_BeatPulse();  /* light on tap-down, in addition to the timer beat */

  bpm_dirty     = 1U;
  bpm_save_tick = HAL_GetTick() + BPM_SAVE_DELAY_MS; /* re-arm save timer */
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
