// Main application entry point and runtime-owned state.

/* USER CODE BEGIN Header */
/**
  ******************************************************************************
   * @file           : main.cpp
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

#define TAP_BUF_SIZE                     4U /* number of recent tap timestamps kept for tap-tempo averaging */

#define STARTUP_SPLASH_X                 0U /* x origin for the startup splash image */
#define STARTUP_SPLASH_Y                 0U /* y origin for the startup splash image */
#define STARTUP_STATUS_TEXT_X           10U /* x position of the startup flash-status text */
#define STARTUP_STATUS_TEXT_Y           10U /* y position of the startup flash-status text */
#define STARTUP_STATUS_FONT             Font_7x10 /* font used for the startup flash-status text */
#define STARTUP_STATUS_FG_COLOUR        CHARCOAL /* foreground colour of the startup flash-status text */
#define STARTUP_STATUS_BG_COLOUR        BLACK /* background colour behind the startup splash/status area */
#define STARTUP_FLASH_OK_TEXT           "flash_OK" /* status string shown when persisted flash state validates */
#define STARTUP_FLASH_INVALID_TEXT      "flash_notOK" /* status string shown when persisted flash state is blank or invalid */
#define STARTUP_LOADING_BAR_MS        1000U /* startup loading-bar duration before the main screen appears */

#define MIDI_OUTPUT_UART_INSTANCE      UART4 /* dedicated UART instance used for controller-managed MIDI output */
#define MIDI_OUTPUT_TX_GPIO_PORT       GPIOD /* GPIO port for the dedicated MIDI output TX pin */
#define MIDI_OUTPUT_TX_PIN             GPIO_PIN_1 /* GPIO pin number for the dedicated MIDI output TX pin */
#define MIDI_OUTPUT_TX_AF              GPIO_AF11_UART4 /* alternate-function selection for the dedicated MIDI output TX pin */

#define TIM6_TICK_HZ                   10000U /* target counter frequency used for internal MIDI clock timing */
#define TIM6_PRESCALER_DIVISOR          9600U /* timer prescaler divisor used to derive TIM6_TICK_HZ */
#define TIM6_COUNTS_PER_MINUTE     (TIM6_TICK_HZ * 60U) /* number of TIM6 ticks that elapse in one minute */

#define TAP_RESET_INTERVAL_MS       3000U /* gap after which tap-tempo history is discarded as a new tap sequence */
#define TAP_MIN_INTERVAL_MS          250U /* shortest accepted gap between taps to reject bounce or unreal tempos */
#define TAP_MIN_COUNT                  2U /* minimum number of taps required before a BPM can be computed */

#define TEMPO_ENCODER_TRANSITIONS_PER_STEP 4 /* quadrature edges expected per mechanical detent */
#define TEMPO_ENCODER_DIRECTION_SIGN    1 /* set to -1 if clockwise and counter-clockwise feel reversed */
#define TEMPO_ENCODER_ACCEL_MID_MS     60U /* <= this detent interval uses medium acceleration */
#define TEMPO_ENCODER_ACCEL_FAST_MS    35U /* <= this detent interval uses fast acceleration */
#define TEMPO_ENCODER_ACCEL_VFAST_MS   20U /* <= this detent interval uses very-fast acceleration */
#define TEMPO_ENCODER_STEP_MID         2 /* BPM delta per detent for medium-fast turns */
#define TEMPO_ENCODER_STEP_FAST        4 /* BPM delta per detent for fast turns */
#define TEMPO_ENCODER_STEP_VFAST       8 /* BPM delta per detent for very-fast turns */
#define ROTARY1_SCROLL_TRANSITIONS_PER_STEP 4 /* quadrature edges expected per mechanical detent for encoder 1 */
#define ROTARY1_SCROLL_DIRECTION_SIGN  1 /* set to -1 if clockwise and counter-clockwise feel reversed for encoder 1 */

#define EXT_CLOCK_HOLDOVER_MIRROR_ENABLED 1U /* set to 0 to revert to legacy behavior without deleting code */
#define EXT_CLOCK_MIRROR_STABLE_SAMPLES 3U /* consecutive equal-BPM external samples required before mirroring */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;
TIM_HandleTypeDef htim6;

UART_HandleTypeDef huart3;
UART_HandleTypeDef huart4;

PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* USER CODE BEGIN PV */
/* ?????? Tap tempo state ???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */
static volatile uint32_t tap_ts[TAP_BUF_SIZE]; /* tap timestamps (ms)      */
static volatile uint8_t  tap_count = 0U;        /* valid entries in buffer  */
static volatile uint8_t  tap_head  = 0U;        /* circular write pointer   */
volatile uint16_t g_bpm     = BPM_DEFAULT; /* live BPM value         */
volatile uint8_t         bpm_dirty    = 0U;  /* set by ISR, read by main */
volatile uint32_t bpm_save_tick = 0U;  /* HAL_GetTick target to save BPM to Flash */
const Preset_t   *active_preset = NULL; /* current preset, needed by screensaver wake */
uint8_t active_preset_index = PRESET_DEFAULT;
static uint8_t tempo_encoder_last_state = 0U; /* previous sampled CLK/DT state for quadrature decoding */
static int8_t tempo_encoder_transition_accum = 0; /* transition accumulator to collapse 4 edges into 1 BPM step */
static uint32_t tempo_encoder_last_step_tick = 0U; /* ms timestamp of the previous completed encoder detent */
static uint8_t rotary1_last_state = 0U; /* previous sampled CLK/DT state for encoder 1 quadrature decoding */
static int8_t rotary1_transition_accum = 0; /* transition accumulator to collapse 4 edges into 1 device-list scroll step */
static volatile int8_t rotary1_pending_steps = 0; /* queued encoder 1 scroll steps waiting for foreground redraw */
static volatile uint8_t rotary1_activity_pending = 0U; /* set by encoder 1 IRQ activity so the main loop can wake the UI */
static uint16_t ext_mirror_candidate_bpm = 0U; /* latest external BPM candidate used by holdover mirroring */
static uint8_t ext_mirror_stable_count = 0U; /* how many consecutive samples matched ext_mirror_candidate_bpm */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_USB_OTG_FS_PCD_Init(void);
/* USER CODE BEGIN PFP */
static void MX_MIDI_Output_UART_Init(void);
static uint32_t MidiClockTimerPeriodForBpm(uint16_t bpm);
static void MX_TIM2_Init(void);
static void MX_TIM6_Init(uint16_t bpm);
static void Rotary1_Init(void);
static void Rotary1_HandleInterrupt(uint16_t gpio_pin);
static void Rotary1_ProcessPending(void);
static void Rotary1_RecordActivity(void);
static void TempoEncoder_Init(void);
static void TempoEncoder_Service(void);
static void TempoEncoder_ApplyBpmStep(int8_t step);
static void ExternalClockHoldoverMirror_Service(void);
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
  /* Bring peripherals up in an order that avoids display flash and ensures
   * MIDI timing is already running before the UI starts querying it. */
  Display_BL_Init();
  MidiInitInput();
  MX_MIDI_Output_UART_Init();
  MidiSetOutputUart(&huart4);
  ST7796_Init();
  MX_TIM2_Init();
    /* The splash asset is stored with swapped red/blue channels. */
    ST7796_DrawImageSwapRB(STARTUP_SPLASH_X, STARTUP_SPLASH_Y, IMAGE_WIDTH, IMAGE_HEIGHT, image_data);
  Display_BL_FadeIn();
  /* BPM flash status ??? top-left corner, visible during loading bar */
    ST7796_WriteString(STARTUP_STATUS_TEXT_X, STARTUP_STATUS_TEXT_Y,
      BPM_Flash_IsValid() ? STARTUP_FLASH_OK_TEXT : STARTUP_FLASH_INVALID_TEXT,
      STARTUP_STATUS_FONT, STARTUP_STATUS_FG_COLOUR, STARTUP_STATUS_BG_COLOUR);
  //Display_LoadingBar(7000U);
    Display_LoadingBar(STARTUP_LOADING_BAR_MS);
  Display_LoadingBarClear();
  Display_BL_FadeOut();
    ST7796_FillScreen(STARTUP_STATUS_BG_COLOUR);  /* clear while backlight is off ??? invisible */
  Display_BL_FadeIn();
    /* Restore persisted tempo/bank/preset so the first drawn main screen comes
     * up in the same state the unit was left in last time. */
  g_bpm = BPM_Flash_Load();
  if (!BPM_Flash_IsValid())
  {
      /* Flash blank or corrupt ??? using default BPM */
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
  Rotary1_Init();
  TempoEncoder_Init();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    /* Deferred work stays in the main loop: BPM/UI updates and flash-save
     * scheduling on one side, queued EXTI button events on the other. */
  #if EXT_CLOCK_HOLDOVER_MIRROR_ENABLED
    ExternalClockHoldoverMirror_Service();
  #endif
    Rotary1_ProcessPending();
    TempoEncoder_Service();
    BPM_Service();
    Button_ProcessPendingEvents();
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
 // This is the UART used for debug prints over the ST-Link USB interface. It is not used for MIDI output, which is on UART4. The MIDI output UART is initialised in MX_MIDI_Output_UART_Init() to ensure it is up and running before the UI starts sending MIDI messages
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

  /* Configure preset/special footswitches as falling-edge only so the ISR
   * records the press edge directly instead of inferring it from a later GPIO read. */
  GPIO_InitStruct.Pin = PRESET_BTN1_Pin | PRESET_BTN2_Pin | PRESET_BTN3_Pin | PRESET_BTN4_Pin |
                        PRESET_BTN5_Pin | PRESET_BTN6_Pin | PRESET_BTN7_Pin | PRESET_BTN8_Pin |
                        PRESET_BTN9_Pin | PRESET_BTN10_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(PRESET_BTN_GPIO_Port, &GPIO_InitStruct);

  /* Mute still needs both edges for its hold/release behavior. */
  GPIO_InitStruct.Pin = PRESET_BTN11_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
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

  /* Rotary 1 uses free EXTI lines 11/12/14, so it can wake the UI on both
   * quadrature motion and switch presses without colliding with the preset bank. */
  GPIO_InitStruct.Pin = ENC1_CLK_Pin | ENC1_DT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(ENC1_CLK_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = ENC1_SW_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(ENC1_SW_GPIO_Port, &GPIO_InitStruct);

  /* Encoder 3 uses simple GPIO polling for tempo. SW is reserved for later. */
  GPIO_InitStruct.Pin = ENC3_CLK_Pin | ENC3_DT_Pin | ENC3_SW_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

// This is the MIDI port used for outputting Program Change and CC messages to the connected MIDI devices. It is initialised separately from USART3 (which is used for debug prints) to ensure it is up and running before the UI starts sending MIDI messages. 

static void MX_MIDI_Output_UART_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_UART4_CLK_ENABLE();

  GPIO_InitStruct.Pin = MIDI_OUTPUT_TX_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = MIDI_OUTPUT_TX_AF;
  HAL_GPIO_Init(MIDI_OUTPUT_TX_GPIO_PORT, &GPIO_InitStruct);

  huart4.Instance = MIDI_OUTPUT_UART_INSTANCE;
  huart4.Init.BaudRate = MIDI_BAUD_RATE;
  huart4.Init.WordLength = UART_WORDLENGTH_8B;
  huart4.Init.StopBits = UART_STOPBITS_1;
  huart4.Init.Parity = UART_PARITY_NONE;
  huart4.Init.Mode = UART_MODE_TX;
  huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart4.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart4) != HAL_OK)
  {
    Error_Handler();
  }
}

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

/* ?????? TIM6 init: APB1 timer clock = 96 MHz ????????????????????????????????????????????????????????????????????????????????????????????????????????????
 * Prescaler 9600-1 ??? 10 kHz tick (0.1 ms resolution).
 * ARR is set for one internal MIDI clock pulse (24 PPQN), not one full beat.
 * The TIM6 ISR asks midi_functions whether this pulse completed a quarter note
 * so the green beat LED still blinks once per beat.
 */
static uint32_t MidiClockTimerPeriodForBpm(uint16_t bpm)
{
  uint32_t denominator = (uint32_t)bpm * MIDI_CLOCK_PULSES_PER_QUARTER_NOTE;
  uint32_t pulse_counts = (TIM6_COUNTS_PER_MINUTE + (denominator / 2U)) / denominator;

  if (pulse_counts == 0U)
    pulse_counts = 1U;

  return pulse_counts - 1U;
}

static void MX_TIM6_Init(uint16_t bpm)
{
  __HAL_RCC_TIM6_CLK_ENABLE();
  htim6.Instance               = TIM6;
  htim6.Init.Prescaler         = TIM6_PRESCALER_DIVISOR - 1U;
  htim6.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim6.Init.Period            = MidiClockTimerPeriodForBpm(bpm);
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
    Error_Handler();
  __HAL_TIM_CLEAR_FLAG(&htim6, TIM_FLAG_UPDATE); /* clear UIF set by UG during init */
  HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 2U, 0U);
  HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
}

static void Rotary1_Init(void)
{
  uint8_t clk_state = (HAL_GPIO_ReadPin(ENC1_CLK_GPIO_Port, ENC1_CLK_Pin) == GPIO_PIN_SET) ? 1U : 0U;
  uint8_t dt_state = (HAL_GPIO_ReadPin(ENC1_DT_GPIO_Port, ENC1_DT_Pin) == GPIO_PIN_SET) ? 1U : 0U;

  rotary1_last_state = (uint8_t)((clk_state << 1U) | dt_state);
  rotary1_transition_accum = 0;
  rotary1_pending_steps = 0;
  rotary1_activity_pending = 0U;
}

static void Rotary1_HandleInterrupt(uint16_t gpio_pin)
{
  static const int8_t transition_delta[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
  };

  rotary1_activity_pending = 1U;

  if (gpio_pin == ENC1_SW_Pin)
    return;

  uint8_t clk_state = (HAL_GPIO_ReadPin(ENC1_CLK_GPIO_Port, ENC1_CLK_Pin) == GPIO_PIN_SET) ? 1U : 0U;
  uint8_t dt_state = (HAL_GPIO_ReadPin(ENC1_DT_GPIO_Port, ENC1_DT_Pin) == GPIO_PIN_SET) ? 1U : 0U;
  uint8_t current_state = (uint8_t)((clk_state << 1U) | dt_state);

  if (current_state == rotary1_last_state)
    return;

  uint8_t transition_index = (uint8_t)((rotary1_last_state << 2U) | current_state);
  rotary1_last_state = current_state;
  rotary1_transition_accum += transition_delta[transition_index];

  if (rotary1_transition_accum >= ROTARY1_SCROLL_TRANSITIONS_PER_STEP)
  {
    int16_t queued_steps = (int16_t)rotary1_pending_steps + (int16_t)ROTARY1_SCROLL_DIRECTION_SIGN;

    if (queued_steps > INT8_MAX)
      queued_steps = INT8_MAX;
    rotary1_pending_steps = (int8_t)queued_steps;
    rotary1_transition_accum = 0;
  }
  else if (rotary1_transition_accum <= -ROTARY1_SCROLL_TRANSITIONS_PER_STEP)
  {
    int16_t queued_steps = (int16_t)rotary1_pending_steps - (int16_t)ROTARY1_SCROLL_DIRECTION_SIGN;

    if (queued_steps < INT8_MIN)
      queued_steps = INT8_MIN;
    rotary1_pending_steps = (int8_t)queued_steps;
    rotary1_transition_accum = 0;
  }
}

static void Rotary1_RecordActivity(void)
{
  uint8_t screensaver_was_active = Display_ScreensaverIsActive();

  Display_ScreensaverDismiss();
  Display_ScreensaverActivity();

  if (screensaver_was_active)
  {
    Display_DrawMainScreen(active_preset ? active_preset : Presets_Get(current_bank * PRESETS_PER_BANK), g_bpm);
  }
}

static void Rotary1_ProcessPending(void)
{
  uint32_t primask;
  uint8_t activity_pending;
  int8_t pending_steps;

  primask = __get_PRIMASK();
  __disable_irq();
  activity_pending = rotary1_activity_pending;
  pending_steps = rotary1_pending_steps;
  rotary1_activity_pending = 0U;
  rotary1_pending_steps = 0;
  if (primask == 0U)
    __enable_irq();

  if (!activity_pending && (pending_steps == 0))
    return;

  if (Display_ScreensaverIsActive())
  {
    Rotary1_RecordActivity();
    return;
  }

  Display_ScreensaverActivity();

  if (pending_steps != 0 && active_preset != NULL)
  {
    if (Display_MainInfoScrollBy(pending_steps))
      Display_DrawMainScreen(active_preset, g_bpm);
  }
}

static void TempoEncoder_Init(void)
{
  uint8_t clk_state = (HAL_GPIO_ReadPin(ENC3_CLK_GPIO_Port, ENC3_CLK_Pin) == GPIO_PIN_SET) ? 1U : 0U;
  uint8_t dt_state = (HAL_GPIO_ReadPin(ENC3_DT_GPIO_Port, ENC3_DT_Pin) == GPIO_PIN_SET) ? 1U : 0U;

  tempo_encoder_last_state = (uint8_t)((clk_state << 1U) | dt_state);
  tempo_encoder_transition_accum = 0;
  tempo_encoder_last_step_tick = 0U;
}

static void TempoEncoder_ApplyBpmStep(int8_t step)
{
  int32_t next_bpm = (int32_t)g_bpm + (int32_t)step;

  if (next_bpm < (int32_t)BPM_MIN)
    next_bpm = (int32_t)BPM_MIN;
  else if (next_bpm > (int32_t)BPM_MAX)
    next_bpm = (int32_t)BPM_MAX;

  if ((uint16_t)next_bpm == g_bpm)
    return;

  g_bpm = (uint16_t)next_bpm;
  TIM6->CNT = 0U;
  TIM6->ARR = MidiClockTimerPeriodForBpm(g_bpm);
  bpm_dirty = 1U;
  bpm_save_tick = HAL_GetTick() + BPM_SAVE_DELAY_MS;
}

static void TempoEncoder_Service(void)
{
  static const int8_t transition_delta[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
  };
  uint8_t clk_state = (HAL_GPIO_ReadPin(ENC3_CLK_GPIO_Port, ENC3_CLK_Pin) == GPIO_PIN_SET) ? 1U : 0U;
  uint8_t dt_state = (HAL_GPIO_ReadPin(ENC3_DT_GPIO_Port, ENC3_DT_Pin) == GPIO_PIN_SET) ? 1U : 0U;
  uint8_t current_state = (uint8_t)((clk_state << 1U) | dt_state);

  if (current_state == tempo_encoder_last_state)
    return;

  uint8_t transition_index = (uint8_t)((tempo_encoder_last_state << 2U) | current_state);
  tempo_encoder_last_state = current_state;
  tempo_encoder_transition_accum += transition_delta[transition_index];

  if (tempo_encoder_transition_accum >= TEMPO_ENCODER_TRANSITIONS_PER_STEP)
  {
    uint32_t now = HAL_GetTick();
    uint32_t step_interval_ms = (tempo_encoder_last_step_tick == 0U) ? UINT32_MAX : (now - tempo_encoder_last_step_tick);
    int8_t step_size = (int8_t)TEMPO_ENCODER_DIRECTION_SIGN;

    if (step_interval_ms <= TEMPO_ENCODER_ACCEL_VFAST_MS)
      step_size = (int8_t)(TEMPO_ENCODER_DIRECTION_SIGN * TEMPO_ENCODER_STEP_VFAST);
    else if (step_interval_ms <= TEMPO_ENCODER_ACCEL_FAST_MS)
      step_size = (int8_t)(TEMPO_ENCODER_DIRECTION_SIGN * TEMPO_ENCODER_STEP_FAST);
    else if (step_interval_ms <= TEMPO_ENCODER_ACCEL_MID_MS)
      step_size = (int8_t)(TEMPO_ENCODER_DIRECTION_SIGN * TEMPO_ENCODER_STEP_MID);

    tempo_encoder_transition_accum = 0;
    tempo_encoder_last_step_tick = now;
    TempoEncoder_ApplyBpmStep(step_size);
  }
  else if (tempo_encoder_transition_accum <= -TEMPO_ENCODER_TRANSITIONS_PER_STEP)
  {
    uint32_t now = HAL_GetTick();
    uint32_t step_interval_ms = (tempo_encoder_last_step_tick == 0U) ? UINT32_MAX : (now - tempo_encoder_last_step_tick);
    int8_t step_size = (int8_t)(-TEMPO_ENCODER_DIRECTION_SIGN);

    if (step_interval_ms <= TEMPO_ENCODER_ACCEL_VFAST_MS)
      step_size = (int8_t)(-TEMPO_ENCODER_DIRECTION_SIGN * TEMPO_ENCODER_STEP_VFAST);
    else if (step_interval_ms <= TEMPO_ENCODER_ACCEL_FAST_MS)
      step_size = (int8_t)(-TEMPO_ENCODER_DIRECTION_SIGN * TEMPO_ENCODER_STEP_FAST);
    else if (step_interval_ms <= TEMPO_ENCODER_ACCEL_MID_MS)
      step_size = (int8_t)(-TEMPO_ENCODER_DIRECTION_SIGN * TEMPO_ENCODER_STEP_MID);

    tempo_encoder_transition_accum = 0;
    tempo_encoder_last_step_tick = now;
    TempoEncoder_ApplyBpmStep(step_size);
  }
}

static void ExternalClockHoldoverMirror_Service(void)
{
  uint16_t external_bpm_x10;
  uint16_t external_bpm;

  /* Mirror a stable external tempo into g_bpm so cable loss can fall through
   * to internal clocking without a large tempo jump. This never sets bpm_dirty
   * and never schedules flash writes, so it is runtime-only holdover state. */
  if (!MidiTransportIsRunning() || !MidiClockGetExternalBpmX10(&external_bpm_x10))
  {
    ext_mirror_stable_count = 0U;
    return;
  }

  external_bpm = (uint16_t)((external_bpm_x10 + 5U) / 10U);
  if (external_bpm < BPM_MIN || external_bpm > BPM_MAX)
  {
    ext_mirror_stable_count = 0U;
    return;
  }

  if (external_bpm != ext_mirror_candidate_bpm)
  {
    ext_mirror_candidate_bpm = external_bpm;
    ext_mirror_stable_count = 1U;
    return;
  }

  if (ext_mirror_stable_count < 0xFFU)
    ext_mirror_stable_count++;

  if (ext_mirror_stable_count < EXT_CLOCK_MIRROR_STABLE_SAMPLES)
    return;

  if (g_bpm == ext_mirror_candidate_bpm)
    return;

  g_bpm = ext_mirror_candidate_bpm;
  TIM6->CNT = 0U;
  TIM6->ARR = MidiClockTimerPeriodForBpm(g_bpm);
}
  

/* ?????? USER button EXTI: tap tempo ??????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
 * Records timestamps of the last TAP_BUF_SIZE presses, averages the
 * intervals, and updates TIM6 ARR + display.
 * Resets history if gap > 3 000 ms (< 20 BPM).
 * Ignores taps < 250 ms apart (> 240 BPM / debounce).
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == ENC1_CLK_Pin || GPIO_Pin == ENC1_DT_Pin || GPIO_Pin == ENC1_SW_Pin)
  {
    Rotary1_HandleInterrupt(GPIO_Pin);
    return;
  }

  uint8_t screensaver_was_active = Display_ScreensaverIsActive();

  if (GPIO_Pin == ENC1_CLK_Pin || GPIO_Pin == ENC1_DT_Pin || GPIO_Pin == ENC1_SW_Pin)
  {
    Rotary1_RecordActivity();
    return;
  }

  /* TAP is handled immediately here; the other footswitches are latched on
   * EXTI and finished later in Button_ProcessPendingEvents(). */
  if (GPIO_Pin != TAP_Pin)
  {
    Button_HandleInterrupt(GPIO_Pin);
    return;
  }

  uint32_t now = HAL_GetTick();

    Display_ScreensaverDismiss();
    Display_ScreensaverActivity();  /* any tap = user activity */

    if (screensaver_was_active)
    {
      /* The first tap after idle should only wake the UI, not also retime BPM. */
      Display_DrawMainScreen(active_preset ? active_preset : Presets_Get(current_bank * PRESETS_PER_BANK), g_bpm);
      return;
    }

    if (Button_HandleTapPress(now)) {
      Button_CancelTapBankCombo();
      Display_DrawMainScreen(Presets_Get(current_bank * PRESETS_PER_BANK), g_bpm);
      return;
    }

  /* Ignore tap tempo while an external MIDI clock is actively running */
  if (MidiTransportIsRunning()) {
    return;
  }

  if (tap_count > 0U)
  {
    uint8_t  prev     = (uint8_t)((tap_head + TAP_BUF_SIZE - 1U) % TAP_BUF_SIZE);
    uint32_t interval = now - tap_ts[prev];

    if (interval > TAP_RESET_INTERVAL_MS)          /* too slow ??? reset */
    {
      tap_count = 0U;
      tap_head  = 0U;
    }
    else if (interval < TAP_MIN_INTERVAL_MS)      /* too fast / bounce ??? ignore */
    {
      return;
    }
  }

  /* Record tap */
  tap_ts[tap_head] = now;
  tap_head = (uint8_t)((tap_head + 1U) % TAP_BUF_SIZE);
  if (tap_count < TAP_BUF_SIZE) tap_count++;

  if (tap_count < TAP_MIN_COUNT) {
    return; /* need at least two taps */
  }

  /* Average all consecutive intervals in the circular buffer so tap tempo is
   * less twitchy than using only the most recent gap. */
  uint32_t sum = 0U;
  uint8_t  n   = tap_count;
  for (uint8_t i = 0U; i < n - 1U; i++)
  {
    uint8_t a = (uint8_t)((tap_head + TAP_BUF_SIZE - n + i)      % TAP_BUF_SIZE);
    uint8_t b = (uint8_t)((tap_head + TAP_BUF_SIZE - n + i + 1U) % TAP_BUF_SIZE);
    sum += tap_ts[b] - tap_ts[a];
  }
  uint32_t avg_ms = sum / (uint32_t)(n - 1U);
  if (avg_ms == 0U) {
    return;
  }

  //uint32_t new_bpm = 60000U / avg_ms;
  uint32_t new_bpm = (uint32_t)((60000.0f / (float)avg_ms) + 0.5f);
  if (new_bpm < BPM_MIN || new_bpm > BPM_MAX) {
    return;
  }

  g_bpm = (uint16_t)new_bpm;

  /* Tapping takes us back to internal tempo ??? clear any external sync state
   * so Display_UpdateBPM doesn't stay stuck on "EXT SYNC LOST". */
  MidiClockUseInternalTempo();

  /* Sync LED to this tap and update blink rate */
  TIM6->CNT = 0U;
  TIM6->ARR = MidiClockTimerPeriodForBpm((uint16_t)new_bpm);
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

