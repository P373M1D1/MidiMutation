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
#include "app_event.h"
#include "runtime_config.h"
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
#define STARTUP_LOADING_BAR_MS_DEFAULT 1000U /* startup loading-bar duration before the main screen appears */

#define MIDI_OUTPUT_UART_INSTANCE      UART4 /* dedicated UART instance used for controller-managed MIDI output */
#define MIDI_OUTPUT_TX_GPIO_PORT       GPIOD /* GPIO port for the dedicated MIDI output TX pin */
#define MIDI_OUTPUT_TX_PIN             GPIO_PIN_1 /* GPIO pin number for the dedicated MIDI output TX pin */
#define MIDI_OUTPUT_TX_AF              GPIO_AF11_UART4 /* alternate-function selection for the dedicated MIDI output TX pin */
#define MIDI_OUTPUT_UART_IRQ_PREEMPT_PRIORITY 1U /* keep UART4 TXE service ahead of clock-discipline and input IRQ work */
#define MIDI_OUTPUT_UART_IRQ_SUBPRIORITY     0U /* no secondary offset needed for the dedicated MIDI output IRQ */

#define TIM6_TICK_HZ                  100000U /* target counter frequency used for internal MIDI clock timing */
#define TIM6_PRESCALER_DIVISOR           960U /* timer prescaler divisor used to derive TIM6_TICK_HZ */
#define TIM6_COUNTS_PER_MINUTE     (TIM6_TICK_HZ * 60U) /* number of TIM6 ticks that elapse in one minute */
#define TIM7_TICK_HZ                 1000000U /* shared encoder-sampler timer tick rate */
#define TIM7_PRESCALER_DIVISOR            96U /* 96 MHz APB1 timer clock divided down to 1 MHz */
#define ENCODER_SAMPLE_HZ              2000U /* shared interrupt rate for encoder quadrature sampling */
#define ENCODER_CHECK_SERIAL_ENABLED      0U /* set to 1 to enable temporary serial encoder test output */
#define APP_EVENT_DIAGNOSTICS_ENABLED     0U /* set to 1 to enable serial diagnostics for app-event queue drops */
#define ENCODER_SWITCH_DEBOUNCE_MS       20U /* debounce window for encoder pushbutton test prints */

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
#define ROTARY1_SCROLL_DIRECTION_SIGN -1 /* set to 1 if clockwise and counter-clockwise feel reversed for encoder 1 */

#define EXT_CLOCK_HOLDOVER_MIRROR_ENABLED 1U /* set to 0 to revert to legacy behavior without deleting code */
#define EXT_CLOCK_MIRROR_STABLE_SAMPLES 3U /* consecutive equal-BPM external samples required before mirroring */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;
TIM_HandleTypeDef htim6;
TIM_HandleTypeDef htim7;

UART_HandleTypeDef huart3;
UART_HandleTypeDef huart4;

PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* USER CODE BEGIN PV */
DMA_HandleTypeDef hdma_spi1_tx;
/* ?????? Tap tempo state ???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? */
static volatile uint32_t tap_ts[TAP_BUF_SIZE]; /* tap timestamps (ms)      */
static volatile uint8_t  tap_count = 0U;        /* valid entries in buffer  */
static volatile uint8_t  tap_head  = 0U;        /* circular write pointer   */
volatile uint16_t g_bpm     = BPM_DEFAULT; /* live BPM value         */
volatile uint8_t         bpm_dirty    = 0U;  /* set by ISR, read by main */
volatile uint32_t bpm_save_tick = 0U;  /* HAL_GetTick target to save BPM to Flash */

static uint32_t App_GetStartupLoadingBarDurationMs(void)
{
  const RuntimeConfigGlobal_t *global = RuntimeConfig_GetGlobal();

  if (!global)
    return STARTUP_LOADING_BAR_MS_DEFAULT;

  return (uint32_t)global->startup_delay_seconds * 1000UL;
}
const Preset_t   *active_preset = NULL; /* current preset, needed by screensaver wake */
uint8_t active_preset_index = PRESET_DEFAULT;
static volatile uint8_t encoder_button_press_pending_mask = 0U; /* queued encoder switch press events waiting for serial test output */
static uint8_t encoder_switch_raw_level[3] = {1U, 1U, 1U}; /* last sampled raw GPIO level for each encoder pushbutton */
static uint8_t encoder_switch_stable_level[3] = {1U, 1U, 1U}; /* current debounced level so one physical press only queues once */
static uint32_t encoder_switch_last_change_tick[3] = {0U, 0U, 0U}; /* HAL tick when the raw encoder switch level last changed */
static uint8_t encoder2_last_state = 0U; /* previous sampled CLK/DT state for encoder 2 quadrature decoding */
static int8_t encoder2_transition_accum = 0; /* transition accumulator to collapse 4 edges into 1 future value step */
static volatile int8_t encoder2_pending_steps = 0; /* queued encoder 2 steps until a function is assigned */
static volatile uint8_t encoder2_activity_pending = 0U; /* set by encoder 2 motion or switch so the main loop can wake the UI */
static uint8_t tempo_encoder_last_state = 0U; /* previous sampled CLK/DT state for quadrature decoding */
static int8_t tempo_encoder_transition_accum = 0; /* transition accumulator to collapse 4 edges into 1 BPM step */
static uint32_t tempo_encoder_last_step_tick = 0U; /* ms timestamp of the previous completed encoder detent */
static volatile int8_t tempo_encoder_pending_delta = 0; /* queued BPM delta accumulated in the encoder ISR */
static volatile uint8_t tempo_encoder_activity_pending = 0U; /* set by tempo encoder motion or switch so the main loop can wake the UI */
static uint8_t rotary1_last_state = 0U; /* previous sampled CLK/DT state for encoder 1 quadrature decoding */
static int8_t rotary1_transition_accum = 0; /* transition accumulator to collapse 4 encoder 1 edges into 1 scroll step */
static volatile int8_t rotary1_pending_steps = 0; /* queued encoder 1 scroll steps waiting for foreground redraw */
static volatile uint8_t rotary1_activity_pending = 0U; /* set by encoder 1 IRQ activity so the main loop can wake the UI */
static uint16_t ext_mirror_candidate_bpm = 0U; /* latest external BPM candidate used by holdover mirroring */
static uint8_t ext_mirror_stable_count = 0U; /* how many consecutive samples matched ext_mirror_candidate_bpm */
static uint8_t app_screensaver_wake_event_pending = 0U; /* coalesce repeated wake requests until the handler runs */
static uint8_t app_screensaver_activity_event_pending = 0U; /* coalesce repeated activity requests until the handler runs */
static uint8_t app_periodic_ui_service_event_pending = 0U; /* sticky guard so the per-loop UI service request is queued at most once until handled */
static uint8_t app_preset_activate_event_pending = 0U; /* last queued preset activation wins until the handler runs */
static uint8_t app_redraw_main_screen_event_pending = 0U; /* coalesce repeated full-screen redraw requests until the handler runs */
static uint8_t app_save_request_pending_mask = 0U; /* one pending bit per save kind so flash writes are not queued redundantly */
static uint8_t app_pending_preset_activate_index = 0U; /* payload stored outside the queue so repeated preset turns collapse to one event */
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
static uint32_t MidiClockTimerCountsForPulseIntervalUs(uint32_t pulse_interval_us);
static void MX_TIM2_Init(void);
static void MX_TIM6_Init(uint16_t bpm);
static void MX_TIM7_Init(void);
static void EncoderCheck_Init(void);
static void EncoderCheck_ProcessPending(void);
static void Rotary1_Init(void);
static void Rotary1_ProcessPending(void);
static void Rotary1_RecordActivity(void);
static void Encoder2_Init(void);
static void Encoder2_ProcessPending(void);
static void TempoEncoder_Init(void);
static void TempoEncoder_ProcessPending(void);
static void TempoEncoder_ApplyBpmStep(int8_t step);
static void ExternalClockHoldoverMirror_Service(void);
static void AppEventDiagnosticService(void);
static const Preset_t *App_GetCurrentDisplayPreset(void);
static void App_ProcessPendingEvents(void);
static void App_HandleTapPressEvent(uint32_t now);
static void App_HandleEncoderPressEvent(uint8_t press_mask);
static void App_HandleEncoderTurnEvent(uint8_t encoder_source, int8_t delta);
static void App_PreparePresetActivation(uint8_t exit_preset_edit);
static void App_HandleBankStepEvent(int8_t delta, uint8_t step_mode);
static void App_HandlePresetActivateEvent(uint8_t preset_index);
static void App_HandlePresetActivateRandomEvent(void);
static void App_HandlePresetActivateMuteEvent(void);
static void App_HandleScreensaverWakeEvent(void);
static void App_HandleScreensaverActivityEvent(void);
static void App_HandlePeriodicUiServiceEvent(void);
static void App_HandleRedrawActiveDisplayEvent(void);
static void App_HandleRedrawMainScreenEvent(void);
static void App_HandleSaveRequestEvent(uint8_t save_kind);
static void App_QueueEncoderPressEvent(uint8_t press_mask);
static void App_QueueEncoderTurnEvent(uint8_t encoder_source, int8_t delta);
static void App_QueueBankStepEvent(int8_t delta, uint8_t step_mode);
static void App_QueuePresetActivateEvent(uint8_t preset_index);
static void App_QueueScreensaverWakeEvent(void);
static void App_QueueScreensaverActivityEvent(void);
static void App_QueuePeriodicUiServiceEvent(void);
static void App_QueueRedrawMainScreenEvent(void);
static void App_QueueSaveRequestEvent(uint8_t save_kind);
static void Menu_SaveIfDirty(void);
static uint8_t Menu_BackOutOneLevel(void);
static uint8_t Menu_Enter(void);
static uint8_t Bank_StepUpWithSpillover(void);
static uint8_t PresetEdit_Enter(void);
static void PresetEdit_Exit(void);
static uint8_t PresetEdit_ApplyDelta(int8_t delta);
static uint8_t PresetEdit_SendCurrentPreset(void);
static uint8_t PresetEdit_ResetCurrentPresetToDefaults(void);
static uint8_t PresetEdit_CurrentPresetIsEditable(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

extern "C" int __io_putchar(int ch)
{
  uint8_t byte = (uint8_t)ch;

  if (huart3.Instance == USART3)
    HAL_UART_Transmit(&huart3, &byte, 1U, 10U);

  return ch;
}

static const char PresetEdit_NameCharset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz ";

static uint8_t PresetEdit_CurrentPresetIsEditable(void)
{
  const Preset_t *active_real_preset = Presets_Get(active_preset_index);

  return (active_preset != NULL && active_preset == active_real_preset) ? 1U : 0U;
}

static uint8_t PresetEdit_AdjustSentinelValue(uint8_t *value,
                                              uint8_t unused_value,
                                              uint8_t min_value,
                                              uint8_t max_value,
                                              int8_t delta)
{
  int16_t current_value;
  int16_t next_value;
  int16_t unused_marker = (int16_t)min_value - 1;

  if (delta == 0)
    return 0U;

  current_value = (*value == unused_value) ? unused_marker : (int16_t)(*value);
  next_value = current_value + (int16_t)delta;

  if (next_value < unused_marker)
    next_value = unused_marker;
  else if (next_value > (int16_t)max_value)
    next_value = (int16_t)max_value;

  if (next_value == current_value)
    return 0U;

  *value = (next_value == unused_marker) ? unused_value : (uint8_t)next_value;
  return 1U;
}

static int16_t PresetEdit_FindNameCharsetIndex(char ch)
{
  for (uint8_t index = 0U; index < (sizeof(PresetEdit_NameCharset) - 1U); ++index)
  {
    if (PresetEdit_NameCharset[index] == ch)
      return (int16_t)index;
  }

  return 0;
}

/* The on-screen name editor works on a fixed 20-cell buffer so the cursor can
 * move across trailing spaces that do not exist in the persisted C string yet. */
static void PresetEdit_LoadNameCells(const Preset_t *preset, char *name_cells)
{
  size_t name_length;

  memset(name_cells, ' ', PRESET_NAME_LENGTH);
  if (!preset)
    return;

  name_length = strnlen(preset->name, PRESET_NAME_LENGTH);
  memcpy(name_cells, preset->name, name_length);
}

/* Copy the editable 20-cell buffer back into the stored preset name while
 * trimming trailing spaces so the runtime preset stays as a normal C string. */
static void PresetEdit_StoreNameCells(Preset_t *preset, const char *name_cells)
{
  int16_t last_non_space_index;

  if (!preset)
    return;

  memset(preset->name, 0, sizeof(preset->name));

  for (last_non_space_index = (int16_t)PRESET_NAME_LENGTH - 1; last_non_space_index >= 0; --last_non_space_index)
  {
    if (name_cells[last_non_space_index] != ' ')
      break;
  }

  if (last_non_space_index < 0)
    return;

  memcpy(preset->name, name_cells, (size_t)last_non_space_index + 1U);
  preset->name[last_non_space_index + 1] = '\0';
}

static uint8_t PresetEdit_AdjustNameCharacter(Preset_t *preset, int8_t delta)
{
  char name_cells[PRESET_NAME_LENGTH];
  uint8_t name_index;
  int16_t current_charset_index;
  int16_t next_charset_index;
  int16_t charset_length = (int16_t)(sizeof(PresetEdit_NameCharset) - 1U);

  if (!preset || delta == 0 || !Display_PresetNameEditIsActive())
    return 0U;

  name_index = Display_PresetNameEditGetCursorIndex();
  if (name_index >= PRESET_NAME_LENGTH)
    return 0U;

  PresetEdit_LoadNameCells(preset, name_cells);
  current_charset_index = PresetEdit_FindNameCharsetIndex(name_cells[name_index]);
  next_charset_index = current_charset_index + (int16_t)delta;

  while (next_charset_index < 0)
    next_charset_index += charset_length;

  while (next_charset_index >= charset_length)
    next_charset_index -= charset_length;

  if (name_cells[name_index] == PresetEdit_NameCharset[next_charset_index])
    return 0U;

  name_cells[name_index] = PresetEdit_NameCharset[next_charset_index];
  PresetEdit_StoreNameCells(preset, name_cells);
  return 1U;
}

static uint8_t PresetEdit_AdjustProgramValue(Preset_t *preset, uint8_t slot, int8_t delta)
{
  const MidiDevice_t *device;
  uint8_t previous_program;
  uint8_t max_program;

  if (!preset || slot >= PRESET_DEVICE_SLOTS)
    return 0U;

  device = MidiDevices_Get(slot);
  max_program = device ? device->max_preset : 127U;

  previous_program = preset->prg[slot].program;

  if (!PresetEdit_AdjustSentinelValue(&preset->prg[slot].program,
                                      PRESET_PROGRAM_NONE,
                                      0U,
                                      max_program,
                                      delta))
  {
    return 0U;
  }

  if (preset->prg[slot].program != previous_program
   && preset->prg[slot].program != PRESET_PROGRAM_NONE
   && device != NULL)
  {
    MIDI_SendProgramChange(device->channel, preset->prg[slot].program);
  }

  return 1U;
}

/* All edit-mode value changes funnel through here. The per-character name
 * editor is treated as a nested submode, so the same edit encoder path can
 * either cycle letters or edit the selected program / CC / relay field. */
static uint8_t PresetEdit_ApplyDelta(int8_t delta)
{
  Preset_t *preset;
  DisplayPresetEditField_t field;

  if (!Display_PresetEditIsActive() || delta == 0)
    return 0U;

  if (!PresetEdit_CurrentPresetIsEditable())
  {
    PresetEdit_Exit();
    return 0U;
  }

  preset = Presets_GetMutable(active_preset_index);
  if (!preset)
    return 0U;

  if (Display_PresetNameEditIsActive())
    return PresetEdit_AdjustNameCharacter(preset, delta);

  field = Display_PresetEditGetField();
  switch (field.type)
  {
  case DISPLAY_PRESET_EDIT_FIELD_NAME:
    return 0U;

  case DISPLAY_PRESET_EDIT_FIELD_PROGRAM:
    return PresetEdit_AdjustProgramValue(preset, field.itemIndex, delta);

  case DISPLAY_PRESET_EDIT_FIELD_RELAY:
    if (field.itemIndex >= PRESET_RELAY_COUNT)
      return 0U;

    {
      uint8_t next_state = (delta > 0) ? PRESET_RELAY_CLOSED : PRESET_RELAY_OPEN;

      if (preset->relay[field.itemIndex] == next_state)
        return 0U;

      preset->relay[field.itemIndex] = next_state;
      return 1U;
    }

  case DISPLAY_PRESET_EDIT_FIELD_CC_CHANNEL:
    if (field.itemIndex >= PRESET_CC_SLOT_COUNT)
      return 0U;
    return PresetEdit_AdjustSentinelValue(&preset->cc[field.itemIndex].channel,
                                          PRESET_CC_CHANNEL_UNUSED,
                                          1U,
                                          16U,
                                          delta);

  case DISPLAY_PRESET_EDIT_FIELD_CC_NUMBER:
    if (field.itemIndex >= PRESET_CC_SLOT_COUNT)
      return 0U;
    return PresetEdit_AdjustSentinelValue(&preset->cc[field.itemIndex].cc_number,
                                          PRESET_CC_NUMBER_UNUSED,
                                          0U,
                                          127U,
                                          delta);

  case DISPLAY_PRESET_EDIT_FIELD_CC_VALUE:
    if (field.itemIndex >= PRESET_CC_SLOT_COUNT)
      return 0U;
    return PresetEdit_AdjustSentinelValue(&preset->cc[field.itemIndex].value,
                                          PRESET_CC_VALUE_UNUSED,
                                          0U,
                                          127U,
                                          delta);

  case DISPLAY_PRESET_EDIT_FIELD_INIT:
    return 0U;

  default:
    return 0U;
  }
}

static uint8_t PresetEdit_Enter(void)
{
  if (Display_MenuIsActive() || Display_PresetEditIsActive() || !PresetEdit_CurrentPresetIsEditable())
    return 0U;

  App_QueueScreensaverWakeEvent();
  Display_PresetEditEnter();
  Display_RefreshPresetEditMode(active_preset, g_bpm);
  return 1U;
}

static void PresetEdit_Exit(void)
{
  if (!Display_PresetEditIsActive())
    return;

  Display_PresetEditExit();
  App_QueueScreensaverActivityEvent();
  Display_RefreshPresetEditMode(App_GetCurrentDisplayPreset(), g_bpm);

  if (Presets_IsDirty())
    App_QueueSaveRequestEvent(APP_EVENT_SAVE_KIND_PRESETS);
}

static uint8_t PresetEdit_SendCurrentPreset(void)
{
  const Preset_t *preset;

  if (!Display_PresetEditIsActive())
    return 0U;

  if (!PresetEdit_CurrentPresetIsEditable())
  {
    PresetEdit_Exit();
    return 1U;
  }

  preset = Presets_Get(active_preset_index);
  if (!preset)
    return 0U;

  Midi_LoadPreset(preset);
  return 1U;
}

static uint8_t PresetEdit_ResetCurrentPresetToDefaults(void)
{
  if (!Display_PresetEditIsActive())
    return 0U;

  if (!PresetEdit_CurrentPresetIsEditable())
  {
    PresetEdit_Exit();
    return 0U;
  }

  Presets_ResetPresetToDefaults(active_preset_index);
  Presets_MarkDirty();
  return 1U;
}

/* ENC3 press behaves like popping a small edit stack: leave per-character name
 * edit first, then leave preset edit entirely on the next press. */
static uint8_t PresetEdit_BackOutOneLevel(void)
{
  if (!Display_PresetEditIsActive())
    return 0U;

  if (Display_PresetNameEditIsActive())
  {
    Display_PresetNameEditExit();
    App_QueueScreensaverActivityEvent();
    if (active_preset)
      Display_PresetEditRefreshCurrentField(active_preset);
    return 1U;
  }

  PresetEdit_Exit();
  return 1U;
}

static const Preset_t *App_GetCurrentDisplayPreset(void)
{
  return active_preset ? active_preset : Presets_Get(current_bank * PRESETS_PER_BANK);
}

static uint8_t App_SaveRequestMaskForKind(uint8_t save_kind)
{
  switch (save_kind)
  {
  case APP_EVENT_SAVE_KIND_RUNTIME_CONFIG:
    return 0x01U;

  case APP_EVENT_SAVE_KIND_PRESETS:
    return 0x02U;

  case APP_EVENT_SAVE_KIND_RUNTIME_STATE:
    return 0x04U;

  default:
    return 0U;
  }
}

static void App_QueueEncoderPressEvent(uint8_t press_mask)
{
  AppEvent_t event;

  if (press_mask == 0U)
    return;

  event.type = APP_EVENT_TYPE_ENCODER_PRESS;
  event.source = APP_EVENT_SOURCE_NONE;
  event.value = (int16_t)press_mask;
  event.tick = HAL_GetTick();
  (void)AppEvent_Push(&event);
}

static void App_QueueEncoderTurnEvent(uint8_t encoder_source, int8_t delta)
{
  AppEvent_t event;

  if (delta == 0)
    return;

  event.type = APP_EVENT_TYPE_ENCODER_TURN;
  event.source = encoder_source;
  event.value = (int16_t)delta;
  event.tick = HAL_GetTick();
  (void)AppEvent_Push(&event);
}

static void App_QueueBankStepEvent(int8_t delta, uint8_t step_mode)
{
  AppEvent_t event;

  if (delta == 0)
    return;

  event.type = APP_EVENT_TYPE_BANK_STEP;
  event.source = step_mode;
  event.value = (int16_t)delta;
  event.tick = HAL_GetTick();
  (void)AppEvent_Push(&event);
}

static void App_QueuePresetActivateEvent(uint8_t preset_index)
{
  AppEvent_t event;

  app_pending_preset_activate_index = preset_index;
  if (app_preset_activate_event_pending)
    return;

  event.type = APP_EVENT_TYPE_PRESET_ACTIVATE;
  event.source = APP_EVENT_SOURCE_NONE;
  event.value = 0;
  event.tick = HAL_GetTick();
  if (AppEvent_Push(&event))
    app_preset_activate_event_pending = 1U;
}

static void App_QueueScreensaverWakeEvent(void)
{
  AppEvent_t event;

  if (app_screensaver_wake_event_pending)
    return;

  event.type = APP_EVENT_TYPE_SCREENSAVER_WAKE;
  event.source = APP_EVENT_SOURCE_NONE;
  event.value = 0;
  event.tick = HAL_GetTick();
  if (AppEvent_Push(&event))
    app_screensaver_wake_event_pending = 1U;
}

static void App_QueueScreensaverActivityEvent(void)
{
  AppEvent_t event;

  if (app_screensaver_wake_event_pending || app_screensaver_activity_event_pending)
    return;

  event.type = APP_EVENT_TYPE_SCREENSAVER_ACTIVITY;
  event.source = APP_EVENT_SOURCE_NONE;
  event.value = 0;
  event.tick = HAL_GetTick();
  if (AppEvent_Push(&event))
    app_screensaver_activity_event_pending = 1U;
}

static void App_QueuePeriodicUiServiceEvent(void)
{
  AppEvent_t event;

  if (app_periodic_ui_service_event_pending)
    return;

  event.type = APP_EVENT_TYPE_PERIODIC_UI_SERVICE;
  event.source = APP_EVENT_SOURCE_NONE;
  event.value = 0;
  event.tick = HAL_GetTick();
  if (AppEvent_Push(&event))
    app_periodic_ui_service_event_pending = 1U;
}

static void App_QueueRedrawMainScreenEvent(void)
{
  AppEvent_t event;

  if (app_redraw_main_screen_event_pending)
    return;

  event.type = APP_EVENT_TYPE_REDRAW_MAIN_SCREEN;
  event.source = APP_EVENT_SOURCE_NONE;
  event.value = 0;
  event.tick = HAL_GetTick();
  if (AppEvent_Push(&event))
    app_redraw_main_screen_event_pending = 1U;
}

static void App_QueueSaveRequestEvent(uint8_t save_kind)
{
  AppEvent_t event;
  uint8_t pending_mask;

  if (save_kind == 0U)
    return;

  pending_mask = App_SaveRequestMaskForKind(save_kind);
  if (pending_mask == 0U || (app_save_request_pending_mask & pending_mask) != 0U)
    return;

  event.type = APP_EVENT_TYPE_SAVE_REQUEST;
  event.source = save_kind;
  event.value = 0;
  event.tick = HAL_GetTick();
  if (AppEvent_Push(&event))
    app_save_request_pending_mask |= pending_mask;
}

static void App_HandleTapPressEvent(uint32_t now)
{
  uint8_t screensaver_was_active = Display_ScreensaverIsActive();

  App_QueueScreensaverWakeEvent();

  if (screensaver_was_active)
  {
    /* The first tap after idle should only wake the UI, not also retime BPM. */
    App_QueueRedrawMainScreenEvent();
    return;
  }

  if (Button_HandleTapPress(now))
  {
    Button_CancelTapBankCombo();
    return;
  }

  if (MidiClockIsExternalSignalPresent())
    return;

  if (tap_count > 0U)
  {
    uint8_t prev = (uint8_t)((tap_head + TAP_BUF_SIZE - 1U) % TAP_BUF_SIZE);
    uint32_t interval = now - tap_ts[prev];

    if (interval > TAP_RESET_INTERVAL_MS)
    {
      tap_count = 0U;
      tap_head = 0U;
    }
    else if (interval < TAP_MIN_INTERVAL_MS)
    {
      return;
    }
  }

  tap_ts[tap_head] = now;
  tap_head = (uint8_t)((tap_head + 1U) % TAP_BUF_SIZE);
  if (tap_count < TAP_BUF_SIZE)
    tap_count++;

  if (tap_count < TAP_MIN_COUNT)
    return;

  uint32_t sum = 0U;
  uint8_t n = tap_count;

  for (uint8_t i = 0U; i < (uint8_t)(n - 1U); i++)
  {
    uint8_t a = (uint8_t)((tap_head + TAP_BUF_SIZE - n + i) % TAP_BUF_SIZE);
    uint8_t b = (uint8_t)((tap_head + TAP_BUF_SIZE - n + i + 1U) % TAP_BUF_SIZE);

    sum += tap_ts[b] - tap_ts[a];
  }

  uint32_t avg_ms = sum / (uint32_t)(n - 1U);
  if (avg_ms == 0U)
    return;

  uint32_t new_bpm = (uint32_t)((60000.0f / (float)avg_ms) + 0.5f);
  if (new_bpm < BPM_MIN || new_bpm > BPM_MAX)
    return;

  g_bpm = (uint16_t)new_bpm;
  MidiClockUseInternalTempo();
  TIM6->ARR = MidiClockTimerPeriodForBpm((uint16_t)new_bpm);
  TIM6->CNT = 0U;
  LED_BeatPulse();

  bpm_dirty = 1U;
  bpm_save_tick = HAL_GetTick() + BPM_SAVE_DELAY_MS;
}

static void App_HandleEncoderPressEvent(uint8_t press_mask)
{
  if (press_mask == 0U)
    return;

  if (Display_ScreensaverIsActive())
  {
    Rotary1_RecordActivity();
    return;
  }

  App_QueueScreensaverActivityEvent();

  if ((press_mask & 0x02U) && Display_MenuIsActive())
  {
    Display_MenuHome();
    Menu_SaveIfDirty();
    return;
  }

  if ((press_mask & 0x04U) && Display_MenuIsActive())
  {
    if (Display_MenuTextEditIsActive())
    {
      Display_MenuTextEditExit();
      return;
    }

    Menu_BackOutOneLevel();
    return;
  }

  if ((press_mask & 0x01U) && Display_MenuIsActive())
  {
    Display_MenuActivate();
    return;
  }

  if ((press_mask & 0x02U) && Display_PresetEditIsActive())
  {
    if (Display_PresetInitConfirmIsActive())
    {
      Display_PresetInitConfirmExit();
      if (PresetEdit_ResetCurrentPresetToDefaults())
        Display_RefreshPresetEditMode(active_preset, g_bpm);
      return;
    }

    PresetEdit_SendCurrentPreset();
    return;
  }

  if ((press_mask & 0x02U) && !Display_PresetEditIsActive())
  {
    Bank_StepUpWithSpillover();
    return;
  }

  if ((press_mask & 0x04U) && Display_PresetEditIsActive())
  {
    if (Display_PresetInitConfirmIsActive())
    {
      Display_PresetInitConfirmExit();
      return;
    }

    PresetEdit_BackOutOneLevel();
    return;
  }

  if ((press_mask & 0x04U) && !Display_PresetEditIsActive())
  {
    Menu_Enter();
    return;
  }

  if ((press_mask & 0x01U) && !Display_PresetEditIsActive())
  {
    PresetEdit_Enter();
  }
  else if ((press_mask & 0x01U) && Display_PresetEditIsActive() && !Display_PresetNameEditIsActive())
  {
    if (Display_PresetInitConfirmIsActive())
      return;

    DisplayPresetEditField_t field = Display_PresetEditGetField();

    if (field.type == DISPLAY_PRESET_EDIT_FIELD_NAME)
    {
      Display_PresetNameEditEnter();
      if (active_preset)
        Display_PresetEditRefreshCurrentField(active_preset);
    }
    else if (field.type == DISPLAY_PRESET_EDIT_FIELD_INIT)
      Display_PresetInitConfirmEnter();
  }
}

static void App_HandleEncoderTurnEvent(uint8_t encoder_source, int8_t delta)
{
  if (delta == 0)
    return;

  switch (encoder_source)
  {
  case APP_EVENT_SOURCE_ENC1:
    if (Display_MenuIsActive())
    {
      if (Display_MenuTextEditIsActive())
        Display_MenuTextEditMoveCursor(delta);
      else
        Display_MenuMoveSelection(delta);
      return;
    }

    if (active_preset == NULL)
      return;

    if (Display_PresetEditIsActive())
    {
      if (!PresetEdit_CurrentPresetIsEditable())
      {
        PresetEdit_Exit();
        return;
      }

      if (Display_PresetInitConfirmIsActive())
        return;

      if (Display_PresetNameEditIsActive())
        Display_PresetNameEditMoveCursor(active_preset, delta);
      else
        Display_PresetEditMoveCursorAndRefresh(active_preset, delta);
      return;
    }

    Display_MainInfoScrollAndRefresh(active_preset, delta);
    return;

  case APP_EVENT_SOURCE_ENC2:
    if (Display_MenuIsActive() || Display_PresetEditIsActive())
      return;

    {
      int16_t bank_base = (int16_t)(current_bank * PRESETS_PER_BANK);
      int16_t slot_index = (int16_t)active_preset_index - bank_base;
      int16_t next_slot = slot_index + (int16_t)delta;

      if (slot_index < 0 || slot_index >= (int16_t)PRESETS_PER_BANK)
        next_slot = 0;

      while (next_slot < 0)
        next_slot += (int16_t)PRESETS_PER_BANK;

      while (next_slot >= (int16_t)PRESETS_PER_BANK)
        next_slot -= (int16_t)PRESETS_PER_BANK;

      App_QueuePresetActivateEvent((uint8_t)(bank_base + next_slot));
    }
    return;

  case APP_EVENT_SOURCE_ENC3:
    if (Display_MenuIsActive())
    {
      Display_MenuAdjustValue(delta);
      return;
    }

    if (Display_PresetEditIsActive())
    {
      if (!PresetEdit_CurrentPresetIsEditable())
      {
        PresetEdit_Exit();
        return;
      }

      if (Display_PresetInitConfirmIsActive())
        return;

      if (PresetEdit_ApplyDelta(delta))
      {
        Presets_MarkDirty();
        Display_PresetEditRefreshCurrentField(active_preset);
      }
      return;
    }

    TempoEncoder_ApplyBpmStep(delta);
    return;

  default:
    return;
  }
}

static void App_HandleBankStepEvent(int8_t delta, uint8_t step_mode)
{
  int16_t next_bank;
  uint8_t preset_slot = 0U;

  if (delta == 0)
    return;

  if (step_mode == APP_EVENT_BANK_STEP_MODE_ACTIVE_SLOT)
    preset_slot = (uint8_t)(active_preset_index % PRESETS_PER_BANK);

  next_bank = (int16_t)current_bank + (int16_t)delta;
  while (next_bank < 0)
    next_bank += (int16_t)PRESET_BANK_COUNT;

  while (next_bank >= (int16_t)PRESET_BANK_COUNT)
    next_bank -= (int16_t)PRESET_BANK_COUNT;

  current_bank = (uint8_t)next_bank;
  App_QueuePresetActivateEvent((uint8_t)(current_bank * PRESETS_PER_BANK + preset_slot));
}

static void App_PreparePresetActivation(uint8_t exit_preset_edit)
{
  if (exit_preset_edit && Display_PresetEditIsActive())
    Display_PresetEditExit();

  Button_ResetSpecialFunctions();
  Display_MainInfoScrollReset();
}

static void App_HandlePresetActivateEvent(uint8_t preset_index)
{
  const Preset_t *preset;

  if (app_preset_activate_event_pending)
  {
    preset_index = app_pending_preset_activate_index;
    app_preset_activate_event_pending = 0U;
  }

  if (preset_index >= Presets_Count())
    return;

  preset = Presets_Get(preset_index);
  if (active_preset == preset)
    return;

  App_PreparePresetActivation(0U);
  App_ActivatePreset(preset_index);
  App_QueueRedrawMainScreenEvent();
}

static void App_HandlePresetActivateRandomEvent(void)
{
  App_PreparePresetActivation(1U);
  Presets_ActivateRandom();
  App_QueueRedrawMainScreenEvent();
}

static void App_HandlePresetActivateMuteEvent(void)
{
  App_PreparePresetActivation(1U);
  Presets_ActivateMute();
  App_QueueRedrawMainScreenEvent();
}

static void App_HandleScreensaverWakeEvent(void)
{
  app_screensaver_wake_event_pending = 0U;
  app_screensaver_activity_event_pending = 0U;
  Display_ScreensaverDismiss();
  Display_ScreensaverActivity();
}

static void App_HandleScreensaverActivityEvent(void)
{
  app_screensaver_activity_event_pending = 0U;
  Display_ScreensaverActivity();
}

static void App_HandlePeriodicUiServiceEvent(void)
{
  app_periodic_ui_service_event_pending = 0U;
  Display_UpdateBPM(g_bpm);
  LED_Update();
  if (Display_ScreensaverUpdate())
    App_QueueRedrawMainScreenEvent();
}

static void App_HandleRedrawActiveDisplayEvent(void)
{
  if (active_preset)
    Display_DrawMainScreen(active_preset, g_bpm);
}

static void App_HandleRedrawMainScreenEvent(void)
{
  app_redraw_main_screen_event_pending = 0U;
  Display_DrawMainScreen(App_GetCurrentDisplayPreset(), g_bpm);
}

static void App_HandleSaveRequestEvent(uint8_t save_kind)
{
  app_save_request_pending_mask &= (uint8_t)~App_SaveRequestMaskForKind(save_kind);

  switch (save_kind)
  {
  case APP_EVENT_SAVE_KIND_RUNTIME_CONFIG:
    if (!RuntimeConfig_IsDirty())
      return;

    Display_ShowSavingPopup();
    RuntimeConfig_SaveIfDirty();
    Display_HideSavingPopup(App_GetCurrentDisplayPreset());
    return;

  case APP_EVENT_SAVE_KIND_PRESETS:
    if (!Presets_IsDirty())
      return;

    Display_ShowSavingPopup();
    Presets_SaveIfDirty();
    Display_HideSavingPopup(App_GetCurrentDisplayPreset());
    return;

  case APP_EVENT_SAVE_KIND_RUNTIME_STATE:
#if BPM_FLASH_WRITES_ENABLED
    RuntimeState_Flash_Save(g_bpm, active_preset_index, current_bank);
    LED_FlashPulse();
#endif
    return;

  default:
    return;
  }
}

static void App_ProcessPendingEvents(void)
{
  AppEvent_t event;

  while (AppEvent_Pop(&event))
  {
    switch (event.type)
    {
    case APP_EVENT_TYPE_TAP_PRESS:
      App_HandleTapPressEvent(event.tick);
      break;

    case APP_EVENT_TYPE_ENCODER_PRESS:
      App_HandleEncoderPressEvent((uint8_t)event.value);
      break;

    case APP_EVENT_TYPE_ENCODER_TURN:
      App_HandleEncoderTurnEvent(event.source, (int8_t)event.value);
      break;

    case APP_EVENT_TYPE_FOOTSWITCH_EDGE:
      if (APP_EVENT_SOURCE_IS_FOOTSWITCH(event.source))
      {
        Button_ProcessInterruptEvent(APP_EVENT_SOURCE_TO_FOOTSWITCH_INDEX(event.source),
                                     (uint8_t)event.value,
                                     event.tick);
      }
      break;

    case APP_EVENT_TYPE_BANK_STEP:
      App_HandleBankStepEvent((int8_t)event.value, event.source);
      break;

    case APP_EVENT_TYPE_PRESET_ACTIVATE:
      App_HandlePresetActivateEvent((uint8_t)event.value);
      break;

    case APP_EVENT_TYPE_PRESET_ACTIVATE_RANDOM:
      App_HandlePresetActivateRandomEvent();
      break;

    case APP_EVENT_TYPE_PRESET_ACTIVATE_MUTE:
      App_HandlePresetActivateMuteEvent();
      break;

    case APP_EVENT_TYPE_SCREENSAVER_WAKE:
      App_HandleScreensaverWakeEvent();
      break;

    case APP_EVENT_TYPE_SCREENSAVER_ACTIVITY:
      App_HandleScreensaverActivityEvent();
      break;

    case APP_EVENT_TYPE_PERIODIC_UI_SERVICE:
      App_HandlePeriodicUiServiceEvent();
      break;

    case APP_EVENT_TYPE_REDRAW_ACTIVE_DISPLAY:
      App_HandleRedrawActiveDisplayEvent();
      break;

    case APP_EVENT_TYPE_REDRAW_MAIN_SCREEN:
      App_HandleRedrawMainScreenEvent();
      break;

    case APP_EVENT_TYPE_SAVE_REQUEST:
      App_HandleSaveRequestEvent(event.source);
      break;

    default:
      break;
    }
  }
}

static uint8_t Bank_StepUpWithSpillover(void)
{
  App_QueueBankStepEvent(1, APP_EVENT_BANK_STEP_MODE_ACTIVE_SLOT);
  return 1U;
}

static void Menu_SaveIfDirty(void)
{
  if (!RuntimeConfig_IsDirty())
    return;

  App_QueueSaveRequestEvent(APP_EVENT_SAVE_KIND_RUNTIME_CONFIG);
}

static uint8_t Menu_BackOutOneLevel(void)
{
  uint8_t sub_editor_active;

  if (!Display_MenuIsActive())
    return 0U;

  sub_editor_active = Display_MenuSubEditorIsActive();
  Display_MenuBack();
  App_QueueScreensaverActivityEvent();
  if (!sub_editor_active)
    Menu_SaveIfDirty();

  if (!Display_MenuIsActive())
    App_QueueRedrawMainScreenEvent();

  return 1U;
}

static uint8_t Menu_Enter(void)
{
  if (Display_MenuIsActive() || Display_PresetEditIsActive())
    return 0U;

  App_QueueScreensaverWakeEvent();
  Display_MenuEnter();
  return 1U;
}

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
  RuntimeConfig_Init();
  AppEvent_Init();
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
    Display_LoadingBar(App_GetStartupLoadingBarDurationMs());
  Display_LoadingBarClear();
  Display_BL_FadeOut();
    ST7796_FillScreen(STARTUP_STATUS_BG_COLOUR);  /* clear while backlight is off ??? invisible */
  Display_BL_FadeIn();
    /* Restore persisted tempo, but always boot into bank 1 / preset 1 so the
     * first main screen is deterministic regardless of the last live state. */
  g_bpm = BPM_Flash_Load();
  if (!BPM_Flash_IsValid())
  {
      /* Flash blank or corrupt ??? using default BPM */
      g_bpm = BPM_DEFAULT;
  }
    current_bank = 0U;
  active_preset_index = 0U;
  MX_TIM6_Init(g_bpm);
  HAL_TIM_Base_Start_IT(&htim6);
  App_ActivatePreset(active_preset_index);
  Display_DrawMainScreen(App_GetCurrentDisplayPreset(), g_bpm);
  bpm_save_tick = 0U;
  Display_ScreensaverActivity();  /* seed inactivity timer from boot */
  Rotary1_Init();
  Encoder2_Init();
  TempoEncoder_Init();
  MX_TIM7_Init();
  HAL_TIM_Base_Start_IT(&htim7);
  EncoderCheck_Init();

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
    Encoder2_ProcessPending();
    TempoEncoder_ProcessPending();
    EncoderCheck_ProcessPending();
    App_ProcessPendingEvents();
    MidiOutputSchedulerService();
    App_QueuePeriodicUiServiceEvent();
    BPM_Service();
    App_ProcessPendingEvents();
    AppEventDiagnosticService();
    MidiClockDiagnosticService();
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

  /* Configure the Nucleo user button as a second falling-edge random trigger. */
  GPIO_InitStruct.Pin = USER_Btn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USER_Btn_GPIO_Port, &GPIO_InitStruct);


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

  /* Rotary 1 A/B stay on plain inputs because EXTI11/12 are already needed by PE11/12. */
  GPIO_InitStruct.Pin = ENC1_CLK_Pin | ENC1_DT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(ENC1_CLK_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = ENC1_SW_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(ENC1_SW_GPIO_Port, &GPIO_InitStruct);

  /* Encoder 2 motion is sampled from TIM7; its switch now uses EXTI4. */
  GPIO_InitStruct.Pin = ENC2_CLK_Pin | ENC2_DT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(ENC2_CLK_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = ENC2_SW_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(ENC2_SW_GPIO_Port, &GPIO_InitStruct);

  /* Encoder 3 motion is sampled from TIM7; its switch now uses EXTI3. */
  GPIO_InitStruct.Pin = ENC3_CLK_Pin | ENC3_DT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = ENC3_SW_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(ENC3_SW_GPIO_Port, &GPIO_InitStruct);
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

  HAL_NVIC_SetPriority(UART4_IRQn, MIDI_OUTPUT_UART_IRQ_PREEMPT_PRIORITY, MIDI_OUTPUT_UART_IRQ_SUBPRIORITY);
  HAL_NVIC_EnableIRQ(UART4_IRQn);
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

static uint32_t MidiClockTimerCountsForPulseIntervalUs(uint32_t pulse_interval_us)
{
  uint64_t pulse_counts;

  if (pulse_interval_us == 0U)
    return 1U;

  pulse_counts = (((uint64_t)TIM6_TICK_HZ * (uint64_t)pulse_interval_us) + 500000ULL) / 1000000ULL;
  if (pulse_counts == 0U)
    pulse_counts = 1U;
  else if (pulse_counts > 0x10000ULL)
    pulse_counts = 0x10000ULL;

  return (uint32_t)pulse_counts;
}

static void MX_TIM6_Init(uint16_t bpm)
{
  __HAL_RCC_TIM6_CLK_ENABLE();
  htim6.Instance               = TIM6;
  htim6.Init.Prescaler         = TIM6_PRESCALER_DIVISOR - 1U;
  htim6.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim6.Init.Period            = MidiClockTimerPeriodForBpm(bpm);
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
    Error_Handler();
  __HAL_TIM_CLEAR_FLAG(&htim6, TIM_FLAG_UPDATE); /* clear UIF set by UG during init */
  HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 2U, 0U);
  HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
}

extern "C" void MidiClockOutputResetPhase(void)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  TIM6->CNT = 0U;
  if (primask == 0U)
    __enable_irq();
}

extern "C" void MidiClockOutputTrackExternalPulse(uint32_t interval_us)
{
  uint32_t pulse_counts = MidiClockTimerCountsForPulseIntervalUs(interval_us);

  if (interval_us != 0U)
  {
    uint64_t denominator = (uint64_t)interval_us * (uint64_t)MIDI_CLOCK_PULSES_PER_QUARTER_NOTE;
    uint32_t external_bpm = (uint32_t)((60000000ULL + (denominator / 2ULL)) / denominator);

    if (external_bpm >= BPM_MIN && external_bpm <= BPM_MAX)
      g_bpm = (uint16_t)external_bpm;
  }

  {
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    TIM6->ARR = pulse_counts - 1U;
    TIM6->CNT = 0U;
    if (primask == 0U)
      __enable_irq();
  }
}

static void MX_TIM7_Init(void)
{
  __HAL_RCC_TIM7_CLK_ENABLE();
  htim7.Instance = TIM7;
  htim7.Init.Prescaler = TIM7_PRESCALER_DIVISOR - 1U;
  htim7.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim7.Init.Period = (TIM7_TICK_HZ / ENCODER_SAMPLE_HZ) - 1U;
  htim7.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim7) != HAL_OK)
    Error_Handler();
  __HAL_TIM_CLEAR_FLAG(&htim7, TIM_FLAG_UPDATE);
  HAL_NVIC_SetPriority(TIM7_IRQn, 3U, 0U);
  HAL_NVIC_EnableIRQ(TIM7_IRQn);
}

static uint8_t Encoder_ReadLevel(GPIO_TypeDef *gpio_port, uint16_t gpio_pin)
{
  return ((gpio_port->IDR & gpio_pin) != 0U) ? 1U : 0U;
}

static uint8_t Encoder2_ReadSwitchLevel(void)
{
  return Encoder_ReadLevel(ENC2_SW_GPIO_Port, ENC2_SW_Pin);
}

static void EncoderCheck_LogTurn(uint8_t encoder_index, int8_t delta)
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

static void AppEventDiagnosticService(void)
{
#if APP_EVENT_DIAGNOSTICS_ENABLED
  static uint32_t last_reported_dropped_count = 0U;
  uint32_t dropped_count = AppEvent_GetDroppedCount();

  if (dropped_count <= last_reported_dropped_count)
    return;

  printf("APPQDIAG dropped=+%lu total=%lu cap=%u\r\n",
         (unsigned long)(dropped_count - last_reported_dropped_count),
         (unsigned long)dropped_count,
         (unsigned)APP_EVENT_QUEUE_CAPACITY);
  last_reported_dropped_count = dropped_count;
#endif
}

static void EncoderCheck_QueueButtonPress(uint8_t encoder_index)
{
  uint8_t event_index = (uint8_t)(encoder_index - 1U);
  encoder_button_press_pending_mask |= (uint8_t)(1U << event_index);
}

/* Accept a button press only after the sampled level has stayed put for the
 * debounce window. That keeps one noisy mechanical click from unwinding both
 * name edit and preset edit in back-to-back foreground iterations. */
static void EncoderCheck_UpdateSwitchState(uint8_t event_index,
                                           uint8_t raw_level,
                                           volatile uint8_t *activity_pending_flag)
{
  uint32_t now = HAL_GetTick();

  if (raw_level != encoder_switch_raw_level[event_index])
  {
    encoder_switch_raw_level[event_index] = raw_level;
    encoder_switch_last_change_tick[event_index] = now;
  }

  if (raw_level == encoder_switch_stable_level[event_index])
    return;

  if ((now - encoder_switch_last_change_tick[event_index]) < ENCODER_SWITCH_DEBOUNCE_MS)
    return;

  encoder_switch_stable_level[event_index] = raw_level;
  if (raw_level == 0U)
  {
    *activity_pending_flag = 1U;
    EncoderCheck_QueueButtonPress((uint8_t)(event_index + 1U));
  }
}

static void EncoderCheck_Init(void)
{
  encoder_switch_raw_level[0] = Encoder_ReadLevel(ENC1_SW_GPIO_Port, ENC1_SW_Pin);
  encoder_switch_raw_level[1] = Encoder2_ReadSwitchLevel();
  encoder_switch_raw_level[2] = Encoder_ReadLevel(ENC3_SW_GPIO_Port, ENC3_SW_Pin);
  encoder_switch_stable_level[0] = encoder_switch_raw_level[0];
  encoder_switch_stable_level[1] = encoder_switch_raw_level[1];
  encoder_switch_stable_level[2] = encoder_switch_raw_level[2];
  encoder_switch_last_change_tick[0] = HAL_GetTick();
  encoder_switch_last_change_tick[1] = HAL_GetTick();
  encoder_switch_last_change_tick[2] = HAL_GetTick();
#if ENCODER_CHECK_SERIAL_ENABLED
  printf("\r\nEncoder check ready on USART3 @ 115200\r\n");
  printf("Turn encoders or press encoder switches to verify wiring.\r\n");
#endif
}

static void EncoderCheck_ProcessPending(void)
{
  uint32_t primask;
  uint8_t press_mask;

  primask = __get_PRIMASK();
  __disable_irq();
  press_mask = encoder_button_press_pending_mask;
  encoder_button_press_pending_mask = 0U;
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

  App_QueueEncoderPressEvent(press_mask);
}

static uint8_t Encoder_ReadState(GPIO_TypeDef *clk_gpio_port, uint16_t clk_gpio_pin,
                                 GPIO_TypeDef *dt_gpio_port, uint16_t dt_gpio_pin)
{
  return (uint8_t)((Encoder_ReadLevel(clk_gpio_port, clk_gpio_pin) << 1U)
                   | Encoder_ReadLevel(dt_gpio_port, dt_gpio_pin));
}

static void EncoderCheck_SampleSwitches(void)
{
  uint8_t encoder1_sw_level = Encoder_ReadLevel(ENC1_SW_GPIO_Port, ENC1_SW_Pin);
  uint8_t encoder2_sw_level = Encoder2_ReadSwitchLevel();
  uint8_t encoder3_sw_level = Encoder_ReadLevel(ENC3_SW_GPIO_Port, ENC3_SW_Pin);

  EncoderCheck_UpdateSwitchState(0U, encoder1_sw_level, &rotary1_activity_pending);
  EncoderCheck_UpdateSwitchState(1U, encoder2_sw_level, &encoder2_activity_pending);
  EncoderCheck_UpdateSwitchState(2U, encoder3_sw_level, &tempo_encoder_activity_pending);
}

static int8_t Encoder_TransitionDelta(uint8_t previous_state, uint8_t current_state)
{
  static const int8_t transition_delta[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
  };

  return transition_delta[(previous_state << 2U) | current_state];
}

static int8_t Encoder_AccumulateTransition(int8_t transition_accum, int8_t transition_delta)
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

static void Encoder_AddPendingDelta(volatile int8_t *pending_delta, int8_t delta)
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

static void Encoder_SampleSimple(GPIO_TypeDef *clk_gpio_port,
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

  current_state = Encoder_ReadState(clk_gpio_port, clk_gpio_pin,
                                    dt_gpio_port, dt_gpio_pin);
  if (current_state == *last_state)
    return;

  *activity_pending = 1U;
  transition_delta = Encoder_TransitionDelta(*last_state, current_state);
  *transition_accum = Encoder_AccumulateTransition(*transition_accum, transition_delta);
  *last_state = current_state;

  if (*transition_accum >= (int8_t)transitions_per_step)
  {
    Encoder_AddPendingDelta(pending_steps, step_sign);
    *transition_accum = 0;
  }
  else if (*transition_accum <= -(int8_t)transitions_per_step)
  {
    Encoder_AddPendingDelta(pending_steps, (int8_t)-step_sign);
    *transition_accum = 0;
  }
}

static int8_t TempoEncoder_ResolveStepMagnitude(uint32_t step_interval_ms)
{
  if (step_interval_ms <= TEMPO_ENCODER_ACCEL_VFAST_MS)
    return TEMPO_ENCODER_STEP_VFAST;
  if (step_interval_ms <= TEMPO_ENCODER_ACCEL_FAST_MS)
    return TEMPO_ENCODER_STEP_FAST;
  if (step_interval_ms <= TEMPO_ENCODER_ACCEL_MID_MS)
    return TEMPO_ENCODER_STEP_MID;

  return 1;
}

static void Encoder_ProcessPendingMotion(volatile uint8_t *activity_pending_flag,
                                         volatile int8_t *pending_delta_flag,
                                         uint8_t encoder_index,
                                         uint8_t event_source)
{
  uint32_t primask;
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

  if (pending_delta != 0)
    EncoderCheck_LogTurn(encoder_index, pending_delta);

  if (!activity_pending && (pending_delta == 0))
    return;

  if (Display_ScreensaverIsActive())
  {
    Rotary1_RecordActivity();
    return;
  }

  App_QueueScreensaverActivityEvent();
  App_QueueEncoderTurnEvent(event_source, pending_delta);
}

static void Rotary1_Init(void)
{
  rotary1_last_state = Encoder_ReadState(ENC1_CLK_GPIO_Port, ENC1_CLK_Pin,
                                         ENC1_DT_GPIO_Port, ENC1_DT_Pin);
  rotary1_transition_accum = 0;
  rotary1_pending_steps = 0;
  rotary1_activity_pending = 0U;
}

static void Rotary1_SampleInterrupt(void)
{
  Encoder_SampleSimple(ENC1_CLK_GPIO_Port,
                       ENC1_CLK_Pin,
                       ENC1_DT_GPIO_Port,
                       ENC1_DT_Pin,
                       &rotary1_last_state,
                       &rotary1_transition_accum,
                       &rotary1_pending_steps,
                       &rotary1_activity_pending,
                       ROTARY1_SCROLL_TRANSITIONS_PER_STEP,
                       (int8_t)-ROTARY1_SCROLL_DIRECTION_SIGN);
}

static void Rotary1_RecordActivity(void)
{
  uint8_t screensaver_was_active = Display_ScreensaverIsActive();

  App_QueueScreensaverWakeEvent();

  if (screensaver_was_active)
  {
    App_QueueRedrawMainScreenEvent();
  }
}

static void Rotary1_ProcessPending(void)
{
  Encoder_ProcessPendingMotion(&rotary1_activity_pending,
                               &rotary1_pending_steps,
                               1U,
                               APP_EVENT_SOURCE_ENC1);
}

static void Encoder2_Init(void)
{
  encoder2_last_state = Encoder_ReadState(ENC2_CLK_GPIO_Port, ENC2_CLK_Pin,
                                          ENC2_DT_GPIO_Port, ENC2_DT_Pin);
  encoder2_transition_accum = 0;
  encoder2_pending_steps = 0;
  encoder2_activity_pending = 0U;
}

static void Encoder2_SampleInterrupt(void)
{
  Encoder_SampleSimple(ENC2_CLK_GPIO_Port,
                       ENC2_CLK_Pin,
                       ENC2_DT_GPIO_Port,
                       ENC2_DT_Pin,
                       &encoder2_last_state,
                       &encoder2_transition_accum,
                       &encoder2_pending_steps,
                       &encoder2_activity_pending,
                       ROTARY1_SCROLL_TRANSITIONS_PER_STEP,
                       1);
}

/* In LIVE mode ENC2 turns through presets within the current bank, while its
 * press action is handled separately in EncoderCheck_ProcessPending() to step banks.
 * It stays out of menu and preset-edit flows where the other encoders already own
 * navigation/value edits. */
static void Encoder2_ProcessPending(void)
{
  Encoder_ProcessPendingMotion(&encoder2_activity_pending,
                               &encoder2_pending_steps,
                               2U,
                               APP_EVENT_SOURCE_ENC2);
}

static void TempoEncoder_Init(void)
{
  tempo_encoder_last_state = Encoder_ReadState(ENC3_CLK_GPIO_Port, ENC3_CLK_Pin,
                                               ENC3_DT_GPIO_Port, ENC3_DT_Pin);
  tempo_encoder_transition_accum = 0;
  tempo_encoder_last_step_tick = 0U;
  tempo_encoder_pending_delta = 0;
  tempo_encoder_activity_pending = 0U;
}

static void TempoEncoder_ApplyBpmStep(int8_t step)
{
  int32_t next_bpm = (int32_t)g_bpm + (int32_t)step;

  if (MidiClockIsExternalSignalPresent())
    return;

  if (next_bpm < (int32_t)BPM_MIN)
    next_bpm = (int32_t)BPM_MIN;
  else if (next_bpm > (int32_t)BPM_MAX)
    next_bpm = (int32_t)BPM_MAX;

  if ((uint16_t)next_bpm == g_bpm)
    return;

  g_bpm = (uint16_t)next_bpm;
  TIM6->ARR = MidiClockTimerPeriodForBpm(g_bpm);
  TIM6->CNT = 0U;
  bpm_dirty = 1U;
  bpm_save_tick = HAL_GetTick() + BPM_SAVE_DELAY_MS;
}

static void TempoEncoder_SampleInterrupt(void)
{
  uint8_t current_state = Encoder_ReadState(ENC3_CLK_GPIO_Port, ENC3_CLK_Pin,
                                            ENC3_DT_GPIO_Port, ENC3_DT_Pin);
  int8_t transition_delta;

  if (current_state == tempo_encoder_last_state)
    return;

  tempo_encoder_activity_pending = 1U;
  transition_delta = Encoder_TransitionDelta(tempo_encoder_last_state, current_state);
  tempo_encoder_transition_accum = Encoder_AccumulateTransition(tempo_encoder_transition_accum, transition_delta);
  tempo_encoder_last_state = current_state;

  if (tempo_encoder_transition_accum >= TEMPO_ENCODER_TRANSITIONS_PER_STEP)
  {
    uint32_t now = HAL_GetTick();
    uint32_t step_interval_ms = (tempo_encoder_last_step_tick == 0U) ? UINT32_MAX : (now - tempo_encoder_last_step_tick);
    int8_t step_size = (int8_t)(TEMPO_ENCODER_DIRECTION_SIGN * TempoEncoder_ResolveStepMagnitude(step_interval_ms));

    tempo_encoder_transition_accum = 0;
    tempo_encoder_last_step_tick = now;
    Encoder_AddPendingDelta(&tempo_encoder_pending_delta, step_size);
  }
  else if (tempo_encoder_transition_accum <= -TEMPO_ENCODER_TRANSITIONS_PER_STEP)
  {
    uint32_t now = HAL_GetTick();
    uint32_t step_interval_ms = (tempo_encoder_last_step_tick == 0U) ? UINT32_MAX : (now - tempo_encoder_last_step_tick);
    int8_t step_size = (int8_t)(-TEMPO_ENCODER_DIRECTION_SIGN * TempoEncoder_ResolveStepMagnitude(step_interval_ms));

    tempo_encoder_transition_accum = 0;
    tempo_encoder_last_step_tick = now;
    Encoder_AddPendingDelta(&tempo_encoder_pending_delta, step_size);
  }
}

/* ENC3 keeps its live-mode tempo role, while its press action is handled
 * separately in EncoderCheck_ProcessPending() to enter MENU from LIVE.
 * It becomes the active value knob in preset edit mode so the right hand can
 * change a field and exit with the same encoder while ENC1 continues to own selection. */
static void TempoEncoder_ProcessPending(void)
{
  Encoder_ProcessPendingMotion(&tempo_encoder_activity_pending,
                               &tempo_encoder_pending_delta,
                               3U,
                               APP_EVENT_SOURCE_ENC3);
}

extern "C" void App_EncoderSampleIRQHandler(void)
{
  if ((TIM7->SR & TIM_SR_UIF) == 0U)
    return;

  TIM7->SR = ~TIM_SR_UIF;
  EncoderCheck_SampleSwitches();
  Rotary1_SampleInterrupt();
  Encoder2_SampleInterrupt();
  TempoEncoder_SampleInterrupt();
}

static void ExternalClockHoldoverMirror_Service(void)
{
  uint16_t external_bpm_x10;
  uint16_t external_bpm;

  /* Mirror a stable external tempo into g_bpm so cable loss can fall through
   * to internal clocking without a large tempo jump. This never sets bpm_dirty
   * and never schedules flash writes, so it is runtime-only holdover state. */
  if (!MidiClockGetExternalBpmX10(&external_bpm_x10))
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
  TIM6->ARR = MidiClockTimerPeriodForBpm(g_bpm);
  TIM6->CNT = 0U;
}
  

/* ?????? USER button EXTI: tap tempo ??????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
 * Records timestamps of the last TAP_BUF_SIZE presses, averages the
 * intervals, and updates TIM6 ARR + display.
 * Resets history if gap > 3 000 ms (< 20 BPM).
 * Ignores taps < 250 ms apart (> 240 BPM / debounce).
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == ENC1_SW_Pin)
  {
    rotary1_activity_pending = 1U;
    return;
  }

  if (GPIO_Pin == ENC2_SW_Pin)
  {
    encoder2_activity_pending = 1U;
    return;
  }

  if (GPIO_Pin == ENC3_SW_Pin)
  {
    tempo_encoder_activity_pending = 1U;
    return;
  }

  /* TAP now publishes an app event so tempo calculation stays in the
   * foreground. The other buttons still use their existing deferred path. */
  if (GPIO_Pin != TAP_Pin)
  {
    Button_HandleInterrupt(GPIO_Pin);
    return;
  }

  AppEvent_t tap_event = {
    APP_EVENT_TYPE_TAP_PRESS,
    APP_EVENT_SOURCE_TAP,
    0,
    HAL_GetTick()
  };

  (void)AppEvent_Push(&tap_event);
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

