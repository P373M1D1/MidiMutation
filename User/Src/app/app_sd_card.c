#include "app/app_sd_card.h"

#include "main.h"

#include <stdio.h>
#include <string.h>

#define APP_SD_SPI_GPIO_PORT GPIOA
#define APP_SD_SPI_SCK_PIN GPIO_PIN_5
#define APP_SD_SPI_MISO_PIN GPIO_PIN_6
#define APP_SD_SPI_MOSI_PIN GPIO_PIN_7
#define APP_SD_SPI_AF GPIO_AF5_SPI1

#define APP_SD_CS_GPIO_PORT GPIOG
#define APP_SD_CS_PIN GPIO_PIN_2

#define APP_SD_SPI_TIMEOUT_MS 20U
#define APP_SD_INIT_TIMEOUT_MS 1500U
#define APP_SD_ACMD41_POLL_MS 10U
#define APP_SD_CMD0_ATTEMPTS 12U
#define APP_SD_COMMAND_RESPONSE_POLLS 16U

#define APP_SD_CMD_GO_IDLE_STATE 0U
#define APP_SD_CMD_SEND_IF_COND 8U
#define APP_SD_CMD_APP_CMD 55U
#define APP_SD_CMD_READ_OCR 58U
#define APP_SD_ACMD_SEND_OP_COND 41U
#define APP_SD_CMD_SET_BLOCKLEN 16U
#define APP_SD_CMD_STOP_TRANSMISSION 12U
#define APP_SD_CMD_READ_SINGLE_BLOCK 17U
#define APP_SD_CMD_READ_MULTIPLE_BLOCK 18U
#define APP_SD_CMD_WRITE_BLOCK 24U

#define APP_SD_R1_IDLE_STATE 0x01U
#define APP_SD_R1_ILLEGAL_COMMAND 0x04U
#define APP_SD_R1_READY 0x00U

#define APP_SD_BLOCK_SIZE 512U
#define APP_SD_DATA_TOKEN 0xFEU
#define APP_SD_WRITE_DATA_ACCEPTED 0x05U
#define APP_SD_READ_TOKEN_POLLS 20000U
#define APP_SD_WRITE_BUSY_POLLS 200000U
#define APP_SD_SPI_INIT_PRESCALER SPI_BAUDRATEPRESCALER_256
#define APP_SD_SPI_DATA_PRESCALER SPI_BAUDRATEPRESCALER_4

#define APP_SD_FAT32_EOC 0x0FFFFFF8UL
#define APP_SD_FAT32_EOC_MARK 0x0FFFFFFFUL
#define APP_SD_DIR_ENTRY_SIZE 32U
#define APP_SD_DIR_ATTR_OFFSET 11U
#define APP_SD_DIR_ATTR_LONG_NAME 0x0FU
#define APP_SD_DIR_ATTR_VOLUME_ID 0x08U
#define APP_SD_DIR_ATTR_DIRECTORY 0x10U
#define APP_SD_DIR_ATTR_ARCHIVE 0x20U
#define APP_SD_DIR_WRITE_TIME_OFFSET 22U
#define APP_SD_DIR_WRITE_DATE_OFFSET 24U
#define APP_SD_DIR_FIRST_CLUSTER_HIGH_OFFSET 20U
#define APP_SD_DIR_FIRST_CLUSTER_LOW_OFFSET 26U
#define APP_SD_DIR_FILE_SIZE_OFFSET 28U
#define APP_SD_LFN_ENTRY_CHARS 13U
#define APP_SD_LFN_MAX_CHARS 64U
#define APP_SD_FAT_DATE_1980_01_01 0x0021U

typedef struct
{
    uint32_t partition_lba;
    uint32_t fat_start_lba;
    uint32_t data_start_lba;
    uint32_t root_dir_cluster;
    uint32_t sectors_per_fat;
    uint32_t cluster_count;
    uint8_t sectors_per_cluster;
    uint8_t fat_count;
    uint8_t mounted;
} AppSdCardVolume_t;

extern SPI_HandleTypeDef hspi1;

static uint8_t app_sd_present = 0U;
static uint8_t app_sd_high_capacity = 0U;
static AppSdCardVolume_t app_sd_volume = {0};
static uint8_t app_sd_sector_buffer[APP_SD_BLOCK_SIZE];
static uint8_t app_sd_dummy_tx_buffer[APP_SD_BLOCK_SIZE];
static uint8_t app_sd_dummy_tx_buffer_ready = 0U;
static const char *app_sd_status_text = "not probed";
static uint8_t app_sd_exclusive_access_depth = 0U;
static uint32_t app_sd_exclusive_access_restore_prescaler = SPI_BAUDRATEPRESCALER_2;
static AppSdCardMetrics_t app_sd_metrics;

static uint8_t AppSdCard_SpiTransfer(uint8_t tx_byte);
static uint8_t AppSdCard_ReadFatEntry(uint32_t cluster, uint32_t *next_cluster);

static void AppSdCard_RecordMax(uint32_t *value, uint32_t candidate)
{
    if (value && candidate > *value)
        *value = candidate;
}

static void AppSdCard_RecordReadTokenPolls(uint32_t polls, uint8_t timed_out)
{
    app_sd_metrics.read_token_polls_total += polls;
    AppSdCard_RecordMax(&app_sd_metrics.read_token_polls_max, polls);
    if (timed_out)
        app_sd_metrics.read_token_timeouts++;
}

static void AppSdCard_RecordWriteBusyPolls(uint32_t polls, uint8_t timed_out)
{
    app_sd_metrics.write_busy_polls_total += polls;
    AppSdCard_RecordMax(&app_sd_metrics.write_busy_polls_max, polls);
    if (timed_out)
        app_sd_metrics.write_busy_timeouts++;
}

static void AppSdCard_RecordReadBlockTicks(uint32_t start_tick)
{
    uint32_t elapsed = HAL_GetTick() - start_tick;
    app_sd_metrics.read_block_ticks_total += elapsed;
    AppSdCard_RecordMax(&app_sd_metrics.read_block_ticks_max, elapsed);
}

static void AppSdCard_RecordMultiReadTicks(uint32_t start_tick)
{
    uint32_t elapsed = HAL_GetTick() - start_tick;
    app_sd_metrics.multi_read_ticks_total += elapsed;
    AppSdCard_RecordMax(&app_sd_metrics.multi_read_ticks_max, elapsed);
}

static void AppSdCard_RecordWriteBlockTicks(uint32_t start_tick)
{
    uint32_t elapsed = HAL_GetTick() - start_tick;
    app_sd_metrics.write_block_ticks_total += elapsed;
    AppSdCard_RecordMax(&app_sd_metrics.write_block_ticks_max, elapsed);
}

void AppSdCard_ResetMetrics(void)
{
    memset(&app_sd_metrics, 0, sizeof(app_sd_metrics));
}

void AppSdCard_GetMetrics(AppSdCardMetrics_t *metrics)
{
    if (!metrics)
        return;

    *metrics = app_sd_metrics;
}

static uint8_t AppSdCard_ReadMisoWithPull(uint32_t pull)
{
    GPIO_InitTypeDef gpio_init = {0};

    gpio_init.Pin = APP_SD_SPI_MISO_PIN;
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = pull;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(APP_SD_SPI_GPIO_PORT, &gpio_init);
    HAL_Delay(1U);

    return (HAL_GPIO_ReadPin(APP_SD_SPI_GPIO_PORT, APP_SD_SPI_MISO_PIN) == GPIO_PIN_SET) ? 1U : 0U;
}

static void AppSdCard_DebugProbePins(void)
{
    GPIO_InitTypeDef gpio_init = {0};
    uint8_t cs_read_high;
    uint8_t cs_read_low;
    uint8_t miso_with_pulldown;
    uint8_t miso_with_pullup;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    gpio_init.Pin = APP_SD_CS_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(APP_SD_CS_GPIO_PORT, &gpio_init);

    gpio_init.Pin = APP_SD_SPI_SCK_PIN | APP_SD_SPI_MOSI_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(APP_SD_SPI_GPIO_PORT, &gpio_init);

    HAL_GPIO_WritePin(APP_SD_SPI_GPIO_PORT, APP_SD_SPI_SCK_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(APP_SD_SPI_GPIO_PORT, APP_SD_SPI_MOSI_PIN, GPIO_PIN_SET);

    HAL_GPIO_WritePin(APP_SD_CS_GPIO_PORT, APP_SD_CS_PIN, GPIO_PIN_SET);
    HAL_Delay(1U);
    cs_read_high = (HAL_GPIO_ReadPin(APP_SD_CS_GPIO_PORT, APP_SD_CS_PIN) == GPIO_PIN_SET) ? 1U : 0U;

    HAL_GPIO_WritePin(APP_SD_CS_GPIO_PORT, APP_SD_CS_PIN, GPIO_PIN_RESET);
    HAL_Delay(1U);
    cs_read_low = (HAL_GPIO_ReadPin(APP_SD_CS_GPIO_PORT, APP_SD_CS_PIN) == GPIO_PIN_SET) ? 1U : 0U;

    HAL_GPIO_WritePin(APP_SD_CS_GPIO_PORT, APP_SD_CS_PIN, GPIO_PIN_SET);

    miso_with_pulldown = AppSdCard_ReadMisoWithPull(GPIO_PULLDOWN);
    miso_with_pullup = AppSdCard_ReadMisoWithPull(GPIO_PULLUP);

    printf("[sd] GPIO probe: CS high/read=%u low/read=%u, MISO deselected pulldown=%u pullup=%u\r\n",
           cs_read_high,
           cs_read_low,
           miso_with_pulldown,
           miso_with_pullup);

    if (miso_with_pulldown == 0U && miso_with_pullup != 0U)
        printf("[sd] MISO floats while deselected; this is normal for many SD modules\r\n");
}

static void AppSdCard_InitGpio(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    HAL_GPIO_WritePin(APP_SD_CS_GPIO_PORT, APP_SD_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ST7796_CS_GPIO_Port, ST7796_CS_Pin, GPIO_PIN_SET);

    gpio_init.Pin = APP_SD_CS_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(APP_SD_CS_GPIO_PORT, &gpio_init);

    gpio_init.Pin = ST7796_CS_Pin;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ST7796_CS_GPIO_Port, &gpio_init);

    gpio_init.Pin = APP_SD_SPI_SCK_PIN | APP_SD_SPI_MISO_PIN | APP_SD_SPI_MOSI_PIN;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = APP_SD_SPI_AF;
    HAL_GPIO_Init(APP_SD_SPI_GPIO_PORT, &gpio_init);
}

static void AppSdCard_SetSpi1Prescaler(uint32_t prescaler)
{
    while (__HAL_SPI_GET_FLAG(&hspi1, SPI_FLAG_BSY) != RESET) {
    }

    __HAL_SPI_DISABLE(&hspi1);
    MODIFY_REG(hspi1.Instance->CR1, SPI_CR1_BR, prescaler);
    hspi1.Init.BaudRatePrescaler = prescaler;
}

static void AppSdCard_BeginTransfer(uint32_t prescaler)
{
    if (app_sd_exclusive_access_depth != 0U)
        return;

    app_sd_metrics.transfer_windows++;
    HAL_GPIO_WritePin(ST7796_CS_GPIO_Port, ST7796_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(APP_SD_CS_GPIO_PORT, APP_SD_CS_PIN, GPIO_PIN_SET);
    AppSdCard_SetSpi1Prescaler(prescaler);
}

static void AppSdCard_EndTransfer(uint32_t restore_prescaler)
{
    if (app_sd_exclusive_access_depth != 0U)
        return;

    HAL_GPIO_WritePin(APP_SD_CS_GPIO_PORT, APP_SD_CS_PIN, GPIO_PIN_SET);
    (void)AppSdCard_SpiTransfer(0xFFU);
    AppSdCard_SetSpi1Prescaler(restore_prescaler);
}

void AppSdCard_BeginExclusiveAccess(void)
{
    if (app_sd_exclusive_access_depth == 0U)
    {
        app_sd_metrics.exclusive_windows++;
        AppSdCard_InitGpio();
        app_sd_exclusive_access_restore_prescaler = hspi1.Init.BaudRatePrescaler;
        HAL_GPIO_WritePin(ST7796_CS_GPIO_Port, ST7796_CS_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(APP_SD_CS_GPIO_PORT, APP_SD_CS_PIN, GPIO_PIN_SET);
        AppSdCard_SetSpi1Prescaler(APP_SD_SPI_DATA_PRESCALER);
    }

    if (app_sd_exclusive_access_depth < 0xFFU)
        app_sd_exclusive_access_depth++;
}

void AppSdCard_EndExclusiveAccess(void)
{
    if (app_sd_exclusive_access_depth == 0U)
        return;

    app_sd_exclusive_access_depth--;
    if (app_sd_exclusive_access_depth != 0U)
        return;

    HAL_GPIO_WritePin(APP_SD_CS_GPIO_PORT, APP_SD_CS_PIN, GPIO_PIN_SET);
    (void)AppSdCard_SpiTransfer(0xFFU);
    AppSdCard_SetSpi1Prescaler(app_sd_exclusive_access_restore_prescaler);
}

static uint16_t AppSdCard_ReadLe16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t AppSdCard_ReadLe32(const uint8_t *data)
{
    return (uint32_t)data[0]
        | ((uint32_t)data[1] << 8)
        | ((uint32_t)data[2] << 16)
        | ((uint32_t)data[3] << 24);
}

static void AppSdCard_WriteLe16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void AppSdCard_WriteLe32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static uint32_t AppSdCard_BlockAddress(uint32_t block_lba)
{
    return app_sd_high_capacity ? block_lba : (block_lba * APP_SD_BLOCK_SIZE);
}

static uint8_t AppSdCard_SpiTransfer(uint8_t tx_byte)
{
    uint8_t rx_byte = 0xFFU;

    if (HAL_SPI_TransmitReceive(&hspi1,
                                &tx_byte,
                                &rx_byte,
                                1U,
                                APP_SD_SPI_TIMEOUT_MS) != HAL_OK)
    {
        return 0xFFU;
    }

    return rx_byte;
}

static void AppSdCard_EnsureDummyTxBuffer(void)
{
    if (app_sd_dummy_tx_buffer_ready)
        return;

    memset(app_sd_dummy_tx_buffer, 0xFF, sizeof(app_sd_dummy_tx_buffer));
    app_sd_dummy_tx_buffer_ready = 1U;
}

static uint8_t AppSdCard_ReadBytes(uint8_t *buffer, uint16_t length)
{
    if (!buffer || length == 0U)
        return 0U;

    AppSdCard_EnsureDummyTxBuffer();
    return (HAL_SPI_TransmitReceive(&hspi1,
                                    app_sd_dummy_tx_buffer,
                                    buffer,
                                    length,
                                    APP_SD_SPI_TIMEOUT_MS) == HAL_OK) ? 1U : 0U;
}

static uint8_t AppSdCard_WriteBytes(const uint8_t *buffer, uint16_t length)
{
    if (!buffer || length == 0U)
        return 0U;

    return (HAL_SPI_Transmit(&hspi1,
                             (uint8_t *)buffer,
                             length,
                             APP_SD_SPI_TIMEOUT_MS) == HAL_OK) ? 1U : 0U;
}

static void AppSdCard_Select(void)
{
    HAL_GPIO_WritePin(APP_SD_CS_GPIO_PORT, APP_SD_CS_PIN, GPIO_PIN_RESET);
}

static void AppSdCard_Deselect(void)
{
    HAL_GPIO_WritePin(APP_SD_CS_GPIO_PORT, APP_SD_CS_PIN, GPIO_PIN_SET);
    (void)AppSdCard_SpiTransfer(0xFFU);
}

static void AppSdCard_SendIdleClocks(void)
{
    AppSdCard_Deselect();

    for (uint8_t index = 0U; index < 10U; ++index)
        (void)AppSdCard_SpiTransfer(0xFFU);
}

static uint8_t AppSdCard_SendCommandSelected(uint8_t command,
                                             uint32_t argument,
                                             uint8_t crc,
                                             uint8_t *tail,
                                             uint8_t tail_size,
                                             uint8_t keep_selected)
{
    uint8_t response = 0xFFU;

    AppSdCard_Deselect();
    AppSdCard_Select();
    (void)AppSdCard_SpiTransfer(0xFFU);

    (void)AppSdCard_SpiTransfer((uint8_t)(0x40U | command));
    (void)AppSdCard_SpiTransfer((uint8_t)(argument >> 24));
    (void)AppSdCard_SpiTransfer((uint8_t)(argument >> 16));
    (void)AppSdCard_SpiTransfer((uint8_t)(argument >> 8));
    (void)AppSdCard_SpiTransfer((uint8_t)argument);
    (void)AppSdCard_SpiTransfer(crc);

    for (uint8_t attempt = 0U; attempt < APP_SD_COMMAND_RESPONSE_POLLS; ++attempt)
    {
        response = AppSdCard_SpiTransfer(0xFFU);
        if ((response & 0x80U) == 0U)
            break;
    }

    for (uint8_t index = 0U; index < tail_size; ++index)
        tail[index] = AppSdCard_SpiTransfer(0xFFU);

    if (!keep_selected)
        AppSdCard_Deselect();

    return response;
}

static uint8_t AppSdCard_SendCommand(uint8_t command,
                                     uint32_t argument,
                                     uint8_t crc,
                                     uint8_t *tail,
                                     uint8_t tail_size)
{
    return AppSdCard_SendCommandSelected(command,
                                         argument,
                                         crc,
                                         tail,
                                         tail_size,
                                         0U);
}

static uint8_t AppSdCard_SendAppCommand(uint8_t command,
                                        uint32_t argument,
                                        uint8_t crc,
                                        uint8_t *tail,
                                        uint8_t tail_size)
{
    uint8_t response = AppSdCard_SendCommand(APP_SD_CMD_APP_CMD,
                                             0U,
                                             0x65U,
                                             NULL,
                                             0U);

    if (response > APP_SD_R1_IDLE_STATE)
        return response;

    return AppSdCard_SendCommand(command,
                                 argument,
                                 crc,
                                 tail,
                                 tail_size);
}

static uint8_t AppSdCard_ReadBlockRaw(uint32_t block_lba, uint8_t *buffer)
{
    uint8_t response;
    uint8_t token = 0xFFU;
    uint32_t start_tick = HAL_GetTick();
    uint32_t token_polls = 0U;

    if (!buffer)
    {
        app_sd_metrics.read_block_failures++;
        return 0U;
    }

    response = AppSdCard_SendCommandSelected(APP_SD_CMD_READ_SINGLE_BLOCK,
                                             AppSdCard_BlockAddress(block_lba),
                                             0xFFU,
                                             NULL,
                                             0U,
                                             1U);
    if (response != APP_SD_R1_READY)
    {
        AppSdCard_Deselect();
        app_sd_metrics.read_block_failures++;
        AppSdCard_RecordReadBlockTicks(start_tick);
        return 0U;
    }

    for (uint32_t poll = 0U; poll < APP_SD_READ_TOKEN_POLLS; ++poll)
    {
        token = AppSdCard_SpiTransfer(0xFFU);
        token_polls = poll + 1U;
        if (token != 0xFFU)
            break;
    }
    AppSdCard_RecordReadTokenPolls(token_polls, (token != APP_SD_DATA_TOKEN) ? 1U : 0U);

    if (token != APP_SD_DATA_TOKEN)
    {
        AppSdCard_Deselect();
        app_sd_metrics.read_block_failures++;
        AppSdCard_RecordReadBlockTicks(start_tick);
        return 0U;
    }

    if (!AppSdCard_ReadBytes(buffer, APP_SD_BLOCK_SIZE))
    {
        AppSdCard_Deselect();
        app_sd_metrics.read_block_failures++;
        AppSdCard_RecordReadBlockTicks(start_tick);
        return 0U;
    }

    (void)AppSdCard_SpiTransfer(0xFFU);
    (void)AppSdCard_SpiTransfer(0xFFU);
    AppSdCard_Deselect();

    app_sd_metrics.single_block_reads++;
    app_sd_metrics.bytes_read += APP_SD_BLOCK_SIZE;
    AppSdCard_RecordReadBlockTicks(start_tick);
    return 1U;
}

static uint8_t AppSdCard_WaitWhileBusySelected(void)
{
    for (uint32_t poll = 0U; poll < APP_SD_WRITE_BUSY_POLLS; ++poll)
    {
        if (AppSdCard_SpiTransfer(0xFFU) == 0xFFU)
        {
            AppSdCard_RecordWriteBusyPolls(poll + 1U, 0U);
            return 1U;
        }
    }

    AppSdCard_RecordWriteBusyPolls(APP_SD_WRITE_BUSY_POLLS, 1U);
    return 0U;
}

static uint8_t AppSdCard_WriteBlockRaw(uint32_t block_lba, const uint8_t *buffer)
{
    uint8_t response;
    uint8_t data_response;
    uint32_t start_tick = HAL_GetTick();

    if (!buffer)
    {
        app_sd_metrics.write_block_failures++;
        return 0U;
    }

    response = AppSdCard_SendCommandSelected(APP_SD_CMD_WRITE_BLOCK,
                                             AppSdCard_BlockAddress(block_lba),
                                             0xFFU,
                                             NULL,
                                             0U,
                                             1U);
    if (response != APP_SD_R1_READY)
    {
        AppSdCard_Deselect();
        app_sd_metrics.write_block_failures++;
        AppSdCard_RecordWriteBlockTicks(start_tick);
        return 0U;
    }

    (void)AppSdCard_SpiTransfer(0xFFU);
    (void)AppSdCard_SpiTransfer(APP_SD_DATA_TOKEN);
    if (!AppSdCard_WriteBytes(buffer, APP_SD_BLOCK_SIZE))
    {
        AppSdCard_Deselect();
        app_sd_metrics.write_block_failures++;
        AppSdCard_RecordWriteBlockTicks(start_tick);
        return 0U;
    }

    (void)AppSdCard_SpiTransfer(0xFFU);
    (void)AppSdCard_SpiTransfer(0xFFU);
    data_response = (uint8_t)(AppSdCard_SpiTransfer(0xFFU) & 0x1FU);
    if (data_response != APP_SD_WRITE_DATA_ACCEPTED)
    {
        AppSdCard_Deselect();
        app_sd_metrics.write_block_failures++;
        AppSdCard_RecordWriteBlockTicks(start_tick);
        return 0U;
    }

    if (!AppSdCard_WaitWhileBusySelected())
    {
        AppSdCard_Deselect();
        app_sd_metrics.write_block_failures++;
        AppSdCard_RecordWriteBlockTicks(start_tick);
        return 0U;
    }

    AppSdCard_Deselect();
    app_sd_metrics.single_block_writes++;
    app_sd_metrics.bytes_written += APP_SD_BLOCK_SIZE;
    AppSdCard_RecordWriteBlockTicks(start_tick);
    return 1U;
}

static uint8_t AppSdCard_WaitForDataToken(void)
{
    uint8_t token = 0xFFU;
    uint32_t token_polls = 0U;

    for (uint32_t poll = 0U; poll < APP_SD_READ_TOKEN_POLLS; ++poll)
    {
        token = AppSdCard_SpiTransfer(0xFFU);
        token_polls = poll + 1U;
        if (token != 0xFFU)
            break;
    }

    AppSdCard_RecordReadTokenPolls(token_polls, (token != APP_SD_DATA_TOKEN) ? 1U : 0U);
    return (token == APP_SD_DATA_TOKEN) ? 1U : 0U;
}

static uint8_t AppSdCard_StopMultipleBlockReadSelected(void)
{
    uint8_t response = 0xFFU;

    (void)AppSdCard_SpiTransfer((uint8_t)(0x40U | APP_SD_CMD_STOP_TRANSMISSION));
    (void)AppSdCard_SpiTransfer(0U);
    (void)AppSdCard_SpiTransfer(0U);
    (void)AppSdCard_SpiTransfer(0U);
    (void)AppSdCard_SpiTransfer(0U);
    (void)AppSdCard_SpiTransfer(0xFFU);

    for (uint8_t attempt = 0U; attempt < APP_SD_COMMAND_RESPONSE_POLLS; ++attempt)
    {
        response = AppSdCard_SpiTransfer(0xFFU);
        if ((response & 0x80U) == 0U)
            break;
    }

    for (uint32_t poll = 0U; poll < APP_SD_READ_TOKEN_POLLS; ++poll)
    {
        if (AppSdCard_SpiTransfer(0xFFU) == 0xFFU)
            break;
    }

    return (response == APP_SD_R1_READY) ? 1U : 0U;
}

static uint8_t AppSdCard_ReadBlocksRawSingle(uint32_t start_lba,
                                             uint8_t *buffer,
                                             uint16_t block_count)
{
    for (uint16_t block = 0U; block < block_count; ++block)
    {
        if (!AppSdCard_ReadBlockRaw(start_lba + block,
                                    &buffer[(uint32_t)block * APP_SD_BLOCK_SIZE]))
        {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t AppSdCard_ReadBlocksRaw(uint32_t start_lba,
                                       uint8_t *buffer,
                                       uint16_t block_count)
{
    uint8_t response;
    uint32_t start_tick = HAL_GetTick();

    if (!buffer || block_count == 0U)
        return 0U;

    if (block_count == 1U)
        return AppSdCard_ReadBlockRaw(start_lba, buffer);

    response = AppSdCard_SendCommandSelected(APP_SD_CMD_READ_MULTIPLE_BLOCK,
                                             AppSdCard_BlockAddress(start_lba),
                                             0xFFU,
                                             NULL,
                                             0U,
                                             1U);
    if (response != APP_SD_R1_READY)
    {
        AppSdCard_Deselect();
        return AppSdCard_ReadBlocksRawSingle(start_lba, buffer, block_count);
    }

    for (uint16_t block = 0U; block < block_count; ++block)
    {
        if (!AppSdCard_WaitForDataToken())
        {
            (void)AppSdCard_StopMultipleBlockReadSelected();
            AppSdCard_Deselect();
            return AppSdCard_ReadBlocksRawSingle(start_lba, buffer, block_count);
        }

        if (!AppSdCard_ReadBytes(&buffer[(uint32_t)block * APP_SD_BLOCK_SIZE],
                                 APP_SD_BLOCK_SIZE))
        {
            (void)AppSdCard_StopMultipleBlockReadSelected();
            AppSdCard_Deselect();
            return AppSdCard_ReadBlocksRawSingle(start_lba, buffer, block_count);
        }

        (void)AppSdCard_SpiTransfer(0xFFU);
        (void)AppSdCard_SpiTransfer(0xFFU);
    }

    if (!AppSdCard_StopMultipleBlockReadSelected())
    {
        AppSdCard_Deselect();
        return AppSdCard_ReadBlocksRawSingle(start_lba, buffer, block_count);
    }

    AppSdCard_Deselect();
    app_sd_metrics.multi_block_read_commands++;
    app_sd_metrics.multi_block_read_blocks += block_count;
    app_sd_metrics.bytes_read += (uint32_t)block_count * APP_SD_BLOCK_SIZE;
    AppSdCard_RecordMultiReadTicks(start_tick);
    return 1U;
}

static uint32_t AppSdCard_ClusterToLba(uint32_t cluster)
{
    return app_sd_volume.data_start_lba
        + ((cluster - 2UL) * (uint32_t)app_sd_volume.sectors_per_cluster);
}

static uint8_t AppSdCard_IsEndOfClusterChain(uint32_t cluster)
{
    return (cluster >= APP_SD_FAT32_EOC) ? 1U : 0U;
}

static uint8_t AppSdCard_ReadFatEntry(uint32_t cluster, uint32_t *next_cluster)
{
    uint32_t fat_offset;
    uint32_t fat_sector_lba;
    uint16_t entry_offset;

    if (!next_cluster)
        return 0U;

    fat_offset = cluster * 4UL;
    fat_sector_lba = app_sd_volume.fat_start_lba + (fat_offset / APP_SD_BLOCK_SIZE);
    entry_offset = (uint16_t)(fat_offset % APP_SD_BLOCK_SIZE);

    if (!AppSdCard_ReadBlockRaw(fat_sector_lba, app_sd_sector_buffer))
        return 0U;

    *next_cluster = AppSdCard_ReadLe32(&app_sd_sector_buffer[entry_offset]) & 0x0FFFFFFFUL;
    app_sd_metrics.fat_entry_reads++;
    return 1U;
}

static uint8_t AppSdCard_WriteFatEntry(uint32_t cluster, uint32_t next_cluster)
{
    uint32_t fat_offset;
    uint32_t fat_sector_offset;
    uint16_t entry_offset;

    if (cluster < 2UL || cluster >= (app_sd_volume.cluster_count + 2UL))
        return 0U;

    fat_offset = cluster * 4UL;
    fat_sector_offset = fat_offset / APP_SD_BLOCK_SIZE;
    entry_offset = (uint16_t)(fat_offset % APP_SD_BLOCK_SIZE);
    next_cluster &= 0x0FFFFFFFUL;

    for (uint8_t fat_index = 0U; fat_index < app_sd_volume.fat_count; ++fat_index)
    {
        uint32_t fat_sector_lba = app_sd_volume.fat_start_lba
            + ((uint32_t)fat_index * app_sd_volume.sectors_per_fat)
            + fat_sector_offset;

        if (!AppSdCard_ReadBlockRaw(fat_sector_lba, app_sd_sector_buffer))
            return 0U;

        AppSdCard_WriteLe32(&app_sd_sector_buffer[entry_offset], next_cluster);
        if (!AppSdCard_WriteBlockRaw(fat_sector_lba, app_sd_sector_buffer))
            return 0U;
    }

    app_sd_metrics.fat_entry_writes++;
    return 1U;
}

static uint8_t AppSdCard_FindFreeCluster(uint32_t *cluster_out)
{
    uint32_t cluster_limit = app_sd_volume.cluster_count + 2UL;

    if (!cluster_out)
        return 0U;

    for (uint32_t cluster = 2UL; cluster < cluster_limit; ++cluster)
    {
        uint32_t value;

        app_sd_metrics.free_cluster_scan_steps++;
        if (!AppSdCard_ReadFatEntry(cluster, &value))
            return 0U;

        if (value == 0UL)
        {
            *cluster_out = cluster;
            return 1U;
        }
    }

    return 0U;
}

static uint8_t AppSdCard_AllocateCluster(uint32_t *cluster_out)
{
    uint32_t cluster;

    if (!cluster_out || !AppSdCard_FindFreeCluster(&cluster))
        return 0U;

    if (!AppSdCard_WriteFatEntry(cluster, APP_SD_FAT32_EOC_MARK))
        return 0U;

    *cluster_out = cluster;
    app_sd_metrics.clusters_allocated++;
    return 1U;
}

static uint8_t AppSdCard_FreeClusterChain(uint32_t first_cluster)
{
    uint32_t cluster = first_cluster;
    uint32_t guard = app_sd_volume.cluster_count + 1UL;

    while (cluster >= 2UL
        && cluster < (app_sd_volume.cluster_count + 2UL)
        && guard > 0UL)
    {
        uint32_t next_cluster;

        if (!AppSdCard_ReadFatEntry(cluster, &next_cluster))
            return 0U;

        if (!AppSdCard_WriteFatEntry(cluster, 0UL))
            return 0U;

        app_sd_metrics.clusters_freed++;
        if (AppSdCard_IsEndOfClusterChain(next_cluster))
            return 1U;

        cluster = next_cluster;
        --guard;
    }

    return (first_cluster < 2UL) ? 1U : 0U;
}

static uint8_t AppSdCard_ParseBootSector(uint32_t partition_lba)
{
    uint16_t bytes_per_sector;
    uint16_t reserved_sectors;
    uint16_t root_entry_count;
    uint32_t total_sectors;
    uint32_t data_sectors;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;

    if (!AppSdCard_ReadBlockRaw(partition_lba, app_sd_sector_buffer))
        return 0U;

    if (app_sd_sector_buffer[510] != 0x55U || app_sd_sector_buffer[511] != 0xAAU)
        return 0U;

    bytes_per_sector = AppSdCard_ReadLe16(&app_sd_sector_buffer[11]);
    if (bytes_per_sector != APP_SD_BLOCK_SIZE)
        return 0U;

    total_sectors = AppSdCard_ReadLe16(&app_sd_sector_buffer[19]);
    if (total_sectors == 0UL)
        total_sectors = AppSdCard_ReadLe32(&app_sd_sector_buffer[32]);

    root_entry_count = AppSdCard_ReadLe16(&app_sd_sector_buffer[17]);
    sectors_per_fat = AppSdCard_ReadLe32(&app_sd_sector_buffer[36]);
    root_cluster = AppSdCard_ReadLe32(&app_sd_sector_buffer[44]);

    if (root_entry_count != 0U || total_sectors == 0UL || sectors_per_fat == 0U || root_cluster < 2UL)
        return 0U;

    app_sd_volume.partition_lba = partition_lba;
    app_sd_volume.sectors_per_cluster = app_sd_sector_buffer[13];
    app_sd_volume.fat_count = app_sd_sector_buffer[16];
    app_sd_volume.sectors_per_fat = sectors_per_fat;
    app_sd_volume.root_dir_cluster = root_cluster;

    reserved_sectors = AppSdCard_ReadLe16(&app_sd_sector_buffer[14]);
    app_sd_volume.fat_start_lba = partition_lba + reserved_sectors;
    app_sd_volume.data_start_lba = app_sd_volume.fat_start_lba
        + ((uint32_t)app_sd_volume.fat_count * app_sd_volume.sectors_per_fat);
    if (app_sd_volume.data_start_lba < partition_lba
     || app_sd_volume.data_start_lba >= (partition_lba + total_sectors)
     || app_sd_volume.sectors_per_cluster == 0U)
    {
        return 0U;
    }

    data_sectors = (partition_lba + total_sectors) - app_sd_volume.data_start_lba;
    app_sd_volume.cluster_count = data_sectors / app_sd_volume.sectors_per_cluster;
    app_sd_volume.mounted = (app_sd_volume.sectors_per_cluster != 0U
                          && app_sd_volume.fat_count != 0U
                          && app_sd_volume.cluster_count > 0UL) ? 1U : 0U;

    return app_sd_volume.mounted;
}

static uint8_t AppSdCard_MountFat32(void)
{
    uint32_t partition_lba = 0U;

    memset(&app_sd_volume, 0, sizeof(app_sd_volume));

    if (!AppSdCard_ReadBlockRaw(0U, app_sd_sector_buffer))
        return 0U;

    if (app_sd_sector_buffer[510] != 0x55U || app_sd_sector_buffer[511] != 0xAAU)
        return 0U;

    if (AppSdCard_ReadLe16(&app_sd_sector_buffer[11]) == APP_SD_BLOCK_SIZE
     && app_sd_sector_buffer[13] != 0U)
    {
        partition_lba = 0U;
    }
    else
    {
        for (uint8_t entry = 0U; entry < 4U; ++entry)
        {
            const uint8_t *partition = &app_sd_sector_buffer[446U + ((uint16_t)entry * 16U)];
            uint8_t partition_type = partition[4];

            if (partition_type != 0U)
            {
                partition_lba = AppSdCard_ReadLe32(&partition[8]);
                break;
            }
        }
    }

    return AppSdCard_ParseBootSector(partition_lba);
}

static uint8_t AppSdCard_FormatFatName(const char *filename, uint8_t fat_name[11])
{
    uint8_t name_index = 0U;
    uint8_t ext_index = 0U;
    uint8_t in_extension = 0U;

    if (!filename || !fat_name)
        return 0U;

    memset(fat_name, ' ', 11U);

    while (*filename)
    {
        char ch = *filename++;

        if (ch == '.')
        {
            if (in_extension)
                return 0U;

            in_extension = 1U;
            continue;
        }

        if (ch >= 'a' && ch <= 'z')
            ch = (char)(ch - ('a' - 'A'));

        if (!in_extension)
        {
            if (name_index >= 8U)
                return 0U;

            fat_name[name_index++] = (uint8_t)ch;
        }
        else
        {
            if (ext_index >= 3U)
                return 0U;

            fat_name[8U + ext_index++] = (uint8_t)ch;
        }
    }

    return (name_index > 0U) ? 1U : 0U;
}

static char AppSdCard_ToLowerAscii(char ch)
{
    if (ch >= 'A' && ch <= 'Z')
        return (char)(ch + ('a' - 'A'));

    return ch;
}

static uint8_t AppSdCard_FilenameEqualsIgnoreCase(const char *left, const char *right)
{
    if (!left || !right)
        return 0U;

    while (*left && *right)
    {
        if (AppSdCard_ToLowerAscii(*left) != AppSdCard_ToLowerAscii(*right))
            return 0U;

        ++left;
        ++right;
    }

    return (*left == '\0' && *right == '\0') ? 1U : 0U;
}

static uint8_t AppSdCard_DecodeLfnChar(const uint8_t *entry,
                                       uint16_t entry_offset,
                                       char *destination)
{
    uint16_t codepoint = AppSdCard_ReadLe16(&entry[entry_offset]);

    if (codepoint == 0x0000U)
        return 0U;
    if (codepoint == 0xFFFFU)
        return 1U;

    *destination = (codepoint < 0x80U) ? (char)codepoint : '?';
    return 1U;
}

static void AppSdCard_DecodeLfnEntry(const uint8_t *entry,
                                     char *lfn_buffer,
                                     uint8_t lfn_buffer_size)
{
    static const uint8_t lfn_offsets[APP_SD_LFN_ENTRY_CHARS] = {
        1U, 3U, 5U, 7U, 9U,
        14U, 16U, 18U, 20U, 22U, 24U,
        28U, 30U,
    };
    uint8_t sequence = (uint8_t)(entry[0] & 0x1FU);
    uint8_t char_base;

    if (!entry || !lfn_buffer || sequence == 0U)
        return;

    char_base = (uint8_t)((sequence - 1U) * APP_SD_LFN_ENTRY_CHARS);
    if (char_base >= lfn_buffer_size)
        return;

    for (uint8_t index = 0U; index < APP_SD_LFN_ENTRY_CHARS; ++index)
    {
        uint8_t target = (uint8_t)(char_base + index);

        if (target >= lfn_buffer_size)
            return;

        if (!AppSdCard_DecodeLfnChar(entry, lfn_offsets[index], &lfn_buffer[target]))
        {
            lfn_buffer[target] = '\0';
            return;
        }
    }
}

static uint8_t AppSdCard_FileFromDirectoryEntry(AppSdCardFile_t *file,
                                                const uint8_t *entry)
{
    uint32_t cluster_high;
    uint32_t cluster_low;

    if (!file || !entry)
        return 0U;

    cluster_high = AppSdCard_ReadLe16(&entry[APP_SD_DIR_FIRST_CLUSTER_HIGH_OFFSET]);
    cluster_low = AppSdCard_ReadLe16(&entry[APP_SD_DIR_FIRST_CLUSTER_LOW_OFFSET]);

    memset(file, 0, sizeof(*file));
    file->first_cluster = (cluster_high << 16) | cluster_low;
    file->current_cluster = file->first_cluster;
    file->file_size = AppSdCard_ReadLe32(&entry[APP_SD_DIR_FILE_SIZE_OFFSET]);

    return (file->first_cluster >= 2UL) ? 1U : 0U;
}

static void AppSdCard_FileFromDirectoryEntryForWrite(AppSdCardFile_t *file,
                                                     const uint8_t *entry)
{
    uint32_t cluster_high;
    uint32_t cluster_low;

    if (!file || !entry)
        return;

    cluster_high = AppSdCard_ReadLe16(&entry[APP_SD_DIR_FIRST_CLUSTER_HIGH_OFFSET]);
    cluster_low = AppSdCard_ReadLe16(&entry[APP_SD_DIR_FIRST_CLUSTER_LOW_OFFSET]);

    memset(file, 0, sizeof(*file));
    file->first_cluster = (cluster_high << 16) | cluster_low;
    file->current_cluster = file->first_cluster;
    file->file_size = AppSdCard_ReadLe32(&entry[APP_SD_DIR_FILE_SIZE_OFFSET]);
}

static uint8_t AppSdCard_FindRootEntryForWrite(const uint8_t fat_name[11],
                                               AppSdCardFile_t *existing_file,
                                               uint32_t *entry_lba,
                                               uint16_t *entry_offset,
                                               uint8_t *found_existing)
{
    uint32_t cluster;
    uint8_t free_found = 0U;
    uint32_t free_lba = 0UL;
    uint16_t free_offset = 0U;

    if (!fat_name || !entry_lba || !entry_offset || !found_existing)
        return 0U;

    *found_existing = 0U;
    cluster = app_sd_volume.root_dir_cluster;
    while (!AppSdCard_IsEndOfClusterChain(cluster))
    {
        for (uint8_t sector = 0U; sector < app_sd_volume.sectors_per_cluster; ++sector)
        {
            uint32_t lba = AppSdCard_ClusterToLba(cluster) + sector;

            if (!AppSdCard_ReadBlockRaw(lba, app_sd_sector_buffer))
                return 0U;

            app_sd_metrics.root_dir_block_reads++;
            for (uint16_t offset = 0U;
                 offset < APP_SD_BLOCK_SIZE;
                 offset = (uint16_t)(offset + APP_SD_DIR_ENTRY_SIZE))
            {
                const uint8_t *entry = &app_sd_sector_buffer[offset];
                uint8_t first_byte = entry[0];
                uint8_t attr = entry[APP_SD_DIR_ATTR_OFFSET];

                if (first_byte == 0x00U)
                {
                    *entry_lba = free_found ? free_lba : lba;
                    *entry_offset = free_found ? free_offset : offset;
                    return 1U;
                }

                if (first_byte == 0xE5U)
                {
                    if (!free_found)
                    {
                        free_found = 1U;
                        free_lba = lba;
                        free_offset = offset;
                    }
                    continue;
                }

                if (attr == APP_SD_DIR_ATTR_LONG_NAME
                 || (attr & (APP_SD_DIR_ATTR_VOLUME_ID | APP_SD_DIR_ATTR_DIRECTORY)) != 0U)
                {
                    continue;
                }

                if (memcmp(entry, fat_name, 11U) == 0)
                {
                    if (existing_file)
                        AppSdCard_FileFromDirectoryEntryForWrite(existing_file, entry);

                    *entry_lba = lba;
                    *entry_offset = offset;
                    *found_existing = 1U;
                    return 1U;
                }
            }
        }

        if (!AppSdCard_ReadFatEntry(cluster, &cluster))
            return 0U;
    }

    if (free_found)
    {
        *entry_lba = free_lba;
        *entry_offset = free_offset;
        return 1U;
    }

    return 0U;
}

static uint8_t AppSdCard_WriteDirectoryEntry(const AppSdCardFile_t *file,
                                             const uint8_t fat_name[11])
{
    uint8_t *entry;

    if (!file || !fat_name || file->directory_entry_lba == 0UL)
        return 0U;

    if (!AppSdCard_ReadBlockRaw(file->directory_entry_lba, app_sd_sector_buffer))
        return 0U;

    app_sd_metrics.root_dir_block_reads++;
    entry = &app_sd_sector_buffer[file->directory_entry_offset];
    memset(entry, 0, APP_SD_DIR_ENTRY_SIZE);
    memcpy(entry, fat_name, 11U);
    entry[APP_SD_DIR_ATTR_OFFSET] = APP_SD_DIR_ATTR_ARCHIVE;
    AppSdCard_WriteLe16(&entry[APP_SD_DIR_WRITE_TIME_OFFSET], 0U);
    AppSdCard_WriteLe16(&entry[APP_SD_DIR_WRITE_DATE_OFFSET], APP_SD_FAT_DATE_1980_01_01);
    AppSdCard_WriteLe16(&entry[APP_SD_DIR_FIRST_CLUSTER_HIGH_OFFSET],
                        (uint16_t)(file->first_cluster >> 16));
    AppSdCard_WriteLe16(&entry[APP_SD_DIR_FIRST_CLUSTER_LOW_OFFSET],
                        (uint16_t)file->first_cluster);
    AppSdCard_WriteLe32(&entry[APP_SD_DIR_FILE_SIZE_OFFSET], file->file_size);

    if (!AppSdCard_WriteBlockRaw(file->directory_entry_lba, app_sd_sector_buffer))
        return 0U;

    app_sd_metrics.root_dir_block_writes++;
    return 1U;
}

void AppSdCard_InitAndProbe(void)
{
    uint8_t response;
    uint8_t cmd8_tail[4] = {0};
    uint8_t ocr[4] = {0};
    uint8_t version2_card = 0U;
    uint32_t start_tick;
    uint32_t original_spi1_prescaler;

    app_sd_present = 0U;
    app_sd_high_capacity = 0U;
    memset(&app_sd_volume, 0, sizeof(app_sd_volume));
    app_sd_status_text = "not present";

    printf("\r\n[sd] TFT-slot SD probe on SPI1 PA5=SCK PA6=MISO PA7=MOSI PG2=CS\r\n");

    AppSdCard_DebugProbePins();
    AppSdCard_InitGpio();
    original_spi1_prescaler = hspi1.Init.BaudRatePrescaler;
    AppSdCard_SetSpi1Prescaler(APP_SD_SPI_INIT_PRESCALER);

    AppSdCard_SendIdleClocks();

    AppSdCard_Deselect();
    printf("[sd] idle byte with CS high: 0x%02X\r\n", AppSdCard_SpiTransfer(0xFFU));
    AppSdCard_Select();
    printf("[sd] idle byte with CS low : 0x%02X\r\n", AppSdCard_SpiTransfer(0xFFU));
    AppSdCard_Deselect();

    response = 0xFFU;
    for (uint8_t attempt = 1U; attempt <= APP_SD_CMD0_ATTEMPTS; ++attempt)
    {
        response = AppSdCard_SendCommand(APP_SD_CMD_GO_IDLE_STATE, 0U, 0x95U, NULL, 0U);
        printf("[sd] CMD0 attempt %u R1=0x%02X\r\n", attempt, response);
        if (response == APP_SD_R1_IDLE_STATE)
            break;

        HAL_Delay(5U);
    }

    if (response != APP_SD_R1_IDLE_STATE)
    {
        app_sd_status_text = "no idle response";
        printf("[sd] no card idle response: R1=0x%02X\r\n", response);
        AppSdCard_SetSpi1Prescaler(original_spi1_prescaler);
        return;
    }

    response = AppSdCard_SendCommand(APP_SD_CMD_SEND_IF_COND,
                                     0x000001AAUL,
                                     0x87U,
                                     cmd8_tail,
                                     (uint8_t)sizeof(cmd8_tail));
    if (response == APP_SD_R1_IDLE_STATE)
    {
        if (cmd8_tail[2] != 0x01U || cmd8_tail[3] != 0xAAU)
        {
            app_sd_status_text = "CMD8 voltage/check mismatch";
            printf("[sd] CMD8 mismatch: R7=%02X %02X %02X %02X\r\n",
                   cmd8_tail[0],
                   cmd8_tail[1],
                   cmd8_tail[2],
                   cmd8_tail[3]);
            AppSdCard_SetSpi1Prescaler(original_spi1_prescaler);
            return;
        }

        version2_card = 1U;
    }
    else if ((response & APP_SD_R1_ILLEGAL_COMMAND) != 0U)
    {
        version2_card = 0U;
    }
    else
    {
        app_sd_status_text = "CMD8 failed";
        printf("[sd] CMD8 failed: R1=0x%02X\r\n", response);
        AppSdCard_SetSpi1Prescaler(original_spi1_prescaler);
        return;
    }

    start_tick = HAL_GetTick();
    do
    {
        response = AppSdCard_SendAppCommand(APP_SD_ACMD_SEND_OP_COND,
                                            version2_card ? 0x40000000UL : 0U,
                                            0x77U,
                                            NULL,
                                            0U);
        if (response == APP_SD_R1_READY)
            break;

        HAL_Delay(APP_SD_ACMD41_POLL_MS);
    } while ((HAL_GetTick() - start_tick) < APP_SD_INIT_TIMEOUT_MS);

    if (response != APP_SD_R1_READY)
    {
        app_sd_status_text = "ACMD41 timeout";
        printf("[sd] ACMD41 timeout/fail: R1=0x%02X\r\n", response);
        AppSdCard_SetSpi1Prescaler(original_spi1_prescaler);
        return;
    }

    if (version2_card)
    {
        response = AppSdCard_SendCommand(APP_SD_CMD_READ_OCR,
                                         0U,
                                         0xFDU,
                                         ocr,
                                         (uint8_t)sizeof(ocr));
        if (response != APP_SD_R1_READY)
        {
            app_sd_status_text = "CMD58 failed";
            printf("[sd] CMD58 failed: R1=0x%02X\r\n", response);
            AppSdCard_SetSpi1Prescaler(original_spi1_prescaler);
            return;
        }

        app_sd_high_capacity = ((ocr[0] & 0x40U) != 0U) ? 1U : 0U;
    }
    else
    {
        response = AppSdCard_SendCommand(APP_SD_CMD_SET_BLOCKLEN,
                                         APP_SD_BLOCK_SIZE,
                                         0xFFU,
                                         NULL,
                                         0U);
        if (response != APP_SD_R1_READY)
        {
            app_sd_status_text = "CMD16 failed";
            printf("[sd] CMD16 failed: R1=0x%02X\r\n", response);
            AppSdCard_SetSpi1Prescaler(original_spi1_prescaler);
            return;
        }
    }

    app_sd_present = 1U;
    app_sd_status_text = app_sd_high_capacity ? "SDHC/SDXC ready" : "SDSC ready";
    printf("[sd] card ready: %s\r\n", app_sd_status_text);
    AppSdCard_SetSpi1Prescaler(APP_SD_SPI_DATA_PRESCALER);
    if (AppSdCard_MountFat32())
        printf("[sd] FAT32 mounted: partition LBA=%lu, cluster=%u sectors\r\n",
               (unsigned long)app_sd_volume.partition_lba,
               app_sd_volume.sectors_per_cluster);
    else
        printf("[sd] FAT32 mount failed; use an MBR/FAT32 card for assets\r\n");

    AppSdCard_SetSpi1Prescaler(original_spi1_prescaler);
}

uint8_t AppSdCard_IsPresent(void)
{
    return app_sd_present;
}

uint8_t AppSdCard_IsHighCapacity(void)
{
    return app_sd_high_capacity;
}

uint8_t AppSdCard_IsFilesystemReady(void)
{
    return app_sd_volume.mounted;
}

const char *AppSdCard_GetStatusText(void)
{
    return app_sd_status_text;
}

uint8_t AppSdCard_OpenFile(AppSdCardFile_t *file, const char *filename)
{
    uint8_t fat_name[11];
    uint32_t cluster;
    uint32_t restore_prescaler;
    char lfn_buffer[APP_SD_LFN_MAX_CHARS + 1U];
    uint8_t lfn_valid = 0U;
    uint8_t short_name_valid;

    if (!file || !app_sd_present || !app_sd_volume.mounted)
        return 0U;

    short_name_valid = AppSdCard_FormatFatName(filename, fat_name);
    memset(lfn_buffer, 0, sizeof(lfn_buffer));

    restore_prescaler = hspi1.Init.BaudRatePrescaler;
    AppSdCard_BeginTransfer(APP_SD_SPI_DATA_PRESCALER);

    cluster = app_sd_volume.root_dir_cluster;
    while (!AppSdCard_IsEndOfClusterChain(cluster))
    {
        for (uint8_t sector = 0U; sector < app_sd_volume.sectors_per_cluster; ++sector)
        {
            uint32_t lba = AppSdCard_ClusterToLba(cluster) + sector;

            if (!AppSdCard_ReadBlockRaw(lba, app_sd_sector_buffer))
            {
                AppSdCard_EndTransfer(restore_prescaler);
                return 0U;
            }

            app_sd_metrics.root_dir_block_reads++;
            for (uint16_t offset = 0U;
                 offset < APP_SD_BLOCK_SIZE;
                 offset = (uint16_t)(offset + APP_SD_DIR_ENTRY_SIZE))
            {
                const uint8_t *entry = &app_sd_sector_buffer[offset];
                uint8_t first_byte = entry[0];
                uint8_t attr = entry[APP_SD_DIR_ATTR_OFFSET];

                if (first_byte == 0x00U)
                {
                    AppSdCard_EndTransfer(restore_prescaler);
                    return 0U;
                }

                if (attr == APP_SD_DIR_ATTR_LONG_NAME)
                {
                    if ((first_byte & 0x40U) != 0U)
                    {
                        memset(lfn_buffer, 0, sizeof(lfn_buffer));
                        lfn_valid = 1U;
                    }

                    if (lfn_valid)
                        AppSdCard_DecodeLfnEntry(entry, lfn_buffer, APP_SD_LFN_MAX_CHARS);

                    continue;
                }

                if (first_byte == 0xE5U
                 || (attr & (APP_SD_DIR_ATTR_VOLUME_ID | 0x10U)) != 0U)
                {
                    memset(lfn_buffer, 0, sizeof(lfn_buffer));
                    lfn_valid = 0U;
                    continue;
                }

                if ((short_name_valid && memcmp(entry, fat_name, sizeof(fat_name)) == 0)
                 || (lfn_valid && AppSdCard_FilenameEqualsIgnoreCase(lfn_buffer, filename)))
                {
                    uint8_t opened = AppSdCard_FileFromDirectoryEntry(file, entry);
                    AppSdCard_EndTransfer(restore_prescaler);
                    return opened;
                }

                memset(lfn_buffer, 0, sizeof(lfn_buffer));
                lfn_valid = 0U;
            }
        }

        if (!AppSdCard_ReadFatEntry(cluster, &cluster))
        {
            AppSdCard_EndTransfer(restore_prescaler);
            return 0U;
        }
    }

    AppSdCard_EndTransfer(restore_prescaler);
    return 0U;
}

uint8_t AppSdCard_SeekFile(AppSdCardFile_t *file, uint32_t position)
{
    uint32_t restore_prescaler;
    uint32_t cluster_size_bytes;
    uint32_t clusters_to_skip;
    uint32_t cluster_offset;
    uint32_t cluster;

    if (!file || !app_sd_present || !app_sd_volume.mounted)
        return 0U;

    if (position > file->file_size || file->first_cluster < 2UL)
        return 0U;

    cluster_size_bytes = (uint32_t)app_sd_volume.sectors_per_cluster * APP_SD_BLOCK_SIZE;
    if (cluster_size_bytes == 0U)
        return 0U;

    clusters_to_skip = position / cluster_size_bytes;
    cluster_offset = position % cluster_size_bytes;
    cluster = file->first_cluster;

    restore_prescaler = hspi1.Init.BaudRatePrescaler;
    AppSdCard_BeginTransfer(APP_SD_SPI_DATA_PRESCALER);

    while (clusters_to_skip > 0U)
    {
        if (!AppSdCard_ReadFatEntry(cluster, &cluster)
         || AppSdCard_IsEndOfClusterChain(cluster))
        {
            AppSdCard_EndTransfer(restore_prescaler);
            return 0U;
        }

        --clusters_to_skip;
    }

    AppSdCard_EndTransfer(restore_prescaler);

    file->current_cluster = cluster;
    file->position = position;
    file->sector_in_cluster = (uint8_t)(cluster_offset / APP_SD_BLOCK_SIZE);
    file->byte_offset_in_sector = (uint16_t)(cluster_offset % APP_SD_BLOCK_SIZE);

    return 1U;
}

uint32_t AppSdCard_ReadFile(AppSdCardFile_t *file, uint8_t *buffer, uint32_t length)
{
    uint32_t bytes_read = 0U;
    uint32_t restore_prescaler;

    if (!file || !buffer || length == 0U || !app_sd_present || !app_sd_volume.mounted)
        return 0U;

    if (file->position >= file->file_size)
        return 0U;

    if (length > (file->file_size - file->position))
        length = file->file_size - file->position;

    restore_prescaler = hspi1.Init.BaudRatePrescaler;
    AppSdCard_BeginTransfer(APP_SD_SPI_DATA_PRESCALER);

    while (length > 0U && !AppSdCard_IsEndOfClusterChain(file->current_cluster))
    {
        uint32_t sector_lba = AppSdCard_ClusterToLba(file->current_cluster)
            + file->sector_in_cluster;
        uint16_t sector_offset = file->byte_offset_in_sector;
        uint32_t bytes_available = (uint32_t)(APP_SD_BLOCK_SIZE - sector_offset);
        uint32_t chunk = (length > bytes_available) ? bytes_available : length;
        uint8_t *source = app_sd_sector_buffer;

        if (sector_offset == 0U && length >= (APP_SD_BLOCK_SIZE * 2UL))
        {
            uint32_t sectors_left_in_cluster = (uint32_t)app_sd_volume.sectors_per_cluster
                - file->sector_in_cluster;
            uint32_t full_sectors = length / APP_SD_BLOCK_SIZE;

            if (full_sectors > sectors_left_in_cluster)
                full_sectors = sectors_left_in_cluster;

            if (full_sectors > 1UL)
            {
                if (!AppSdCard_ReadBlocksRaw(sector_lba,
                                             buffer,
                                             (uint16_t)full_sectors))
                {
                    break;
                }

                chunk = full_sectors * APP_SD_BLOCK_SIZE;
                buffer += chunk;
                bytes_read += chunk;
                length -= chunk;
                file->position += chunk;
                file->sector_in_cluster = (uint8_t)(file->sector_in_cluster + full_sectors);

                if (file->sector_in_cluster >= app_sd_volume.sectors_per_cluster)
                {
                    uint32_t next_cluster;

                    file->sector_in_cluster = 0U;
                    if (!AppSdCard_ReadFatEntry(file->current_cluster, &next_cluster))
                        break;

                    file->current_cluster = next_cluster;
                }

                continue;
            }
        }

        if (sector_offset == 0U && chunk == APP_SD_BLOCK_SIZE)
        {
            if (!AppSdCard_ReadBlockRaw(sector_lba, buffer))
                break;
        }
        else
        {
            if (!AppSdCard_ReadBlockRaw(sector_lba, app_sd_sector_buffer))
                break;

            source = &app_sd_sector_buffer[sector_offset];
            memcpy(buffer, source, chunk);
        }

        buffer += chunk;
        bytes_read += chunk;
        length -= chunk;
        file->position += chunk;
        file->byte_offset_in_sector = (uint16_t)(file->byte_offset_in_sector + (uint16_t)chunk);

        if (file->byte_offset_in_sector >= APP_SD_BLOCK_SIZE)
        {
            file->byte_offset_in_sector = 0U;
            file->sector_in_cluster++;

            if (file->sector_in_cluster >= app_sd_volume.sectors_per_cluster)
            {
                uint32_t next_cluster;

                file->sector_in_cluster = 0U;
                if (!AppSdCard_ReadFatEntry(file->current_cluster, &next_cluster))
                    break;

                file->current_cluster = next_cluster;
            }
        }
    }

    AppSdCard_EndTransfer(restore_prescaler);
    return bytes_read;
}

static uint8_t AppSdCard_EnsureWritableSector(AppSdCardFile_t *file)
{
    if (!file)
        return 0U;

    if (file->first_cluster < 2UL)
    {
        uint32_t cluster;

        if (!AppSdCard_AllocateCluster(&cluster))
            return 0U;

        file->first_cluster = cluster;
        file->current_cluster = cluster;
        file->sector_in_cluster = 0U;
        file->byte_offset_in_sector = 0U;
        memset(app_sd_sector_buffer, 0, APP_SD_BLOCK_SIZE);
        return 1U;
    }

    if (file->current_cluster < 2UL)
        file->current_cluster = file->first_cluster;

    if (file->sector_in_cluster >= app_sd_volume.sectors_per_cluster)
    {
        uint32_t cluster;

        if (!AppSdCard_AllocateCluster(&cluster))
            return 0U;

        if (!AppSdCard_WriteFatEntry(file->current_cluster, cluster))
            return 0U;

        file->current_cluster = cluster;
        file->sector_in_cluster = 0U;
        file->byte_offset_in_sector = 0U;
        memset(app_sd_sector_buffer, 0, APP_SD_BLOCK_SIZE);
    }

    return 1U;
}

uint8_t AppSdCard_OpenFileForWrite(AppSdCardFile_t *file, const char *filename)
{
    uint8_t fat_name[11];
    AppSdCardFile_t existing_file;
    uint32_t restore_prescaler;
    uint32_t entry_lba = 0UL;
    uint16_t entry_offset = 0U;
    uint8_t found_existing = 0U;

    if (!file || !app_sd_present || !app_sd_volume.mounted)
        return 0U;

    if (!AppSdCard_FormatFatName(filename, fat_name))
        return 0U;

    memset(&existing_file, 0, sizeof(existing_file));
    restore_prescaler = hspi1.Init.BaudRatePrescaler;
    AppSdCard_BeginTransfer(APP_SD_SPI_DATA_PRESCALER);

    if (!AppSdCard_FindRootEntryForWrite(fat_name,
                                         &existing_file,
                                         &entry_lba,
                                         &entry_offset,
                                         &found_existing))
    {
        AppSdCard_EndTransfer(restore_prescaler);
        return 0U;
    }

    if (found_existing && existing_file.first_cluster >= 2UL)
    {
        if (!AppSdCard_FreeClusterChain(existing_file.first_cluster))
        {
            AppSdCard_EndTransfer(restore_prescaler);
            return 0U;
        }
    }

    memset(file, 0, sizeof(*file));
    file->directory_entry_lba = entry_lba;
    file->directory_entry_offset = entry_offset;
    memcpy(file->fat_name, fat_name, sizeof(file->fat_name));
    file->write_open = 1U;

    if (!AppSdCard_WriteDirectoryEntry(file, file->fat_name))
    {
        AppSdCard_EndTransfer(restore_prescaler);
        memset(file, 0, sizeof(*file));
        return 0U;
    }

    AppSdCard_EndTransfer(restore_prescaler);
    return 1U;
}

uint32_t AppSdCard_WriteFile(AppSdCardFile_t *file, const uint8_t *buffer, uint32_t length)
{
    uint32_t bytes_written = 0U;
    uint32_t restore_prescaler;

    if (!file || !buffer || length == 0U || !file->write_open
     || !app_sd_present || !app_sd_volume.mounted)
    {
        return 0U;
    }

    restore_prescaler = hspi1.Init.BaudRatePrescaler;
    AppSdCard_BeginTransfer(APP_SD_SPI_DATA_PRESCALER);

    while (length > 0U)
    {
        uint32_t sector_lba;
        uint16_t sector_offset;
        uint32_t bytes_available;
        uint32_t chunk;

        if (!AppSdCard_EnsureWritableSector(file))
            break;

        sector_lba = AppSdCard_ClusterToLba(file->current_cluster)
            + file->sector_in_cluster;
        sector_offset = file->byte_offset_in_sector;
        bytes_available = (uint32_t)(APP_SD_BLOCK_SIZE - sector_offset);
        chunk = (length > bytes_available) ? bytes_available : length;

        if (sector_offset == 0U && chunk == APP_SD_BLOCK_SIZE)
        {
            if (!AppSdCard_WriteBlockRaw(sector_lba, buffer))
                break;
        }
        else
        {
            if (sector_offset == 0U)
                memset(app_sd_sector_buffer, 0, APP_SD_BLOCK_SIZE);

            memcpy(&app_sd_sector_buffer[sector_offset], buffer, chunk);
            if ((sector_offset + chunk) == APP_SD_BLOCK_SIZE)
            {
                if (!AppSdCard_WriteBlockRaw(sector_lba, app_sd_sector_buffer))
                    break;
            }
        }

        buffer += chunk;
        bytes_written += chunk;
        length -= chunk;
        file->position += chunk;
        file->file_size = file->position;
        file->byte_offset_in_sector = (uint16_t)(sector_offset + (uint16_t)chunk);

        if (file->byte_offset_in_sector >= APP_SD_BLOCK_SIZE)
        {
            file->byte_offset_in_sector = 0U;
            file->sector_in_cluster++;
        }
    }

    AppSdCard_EndTransfer(restore_prescaler);
    return bytes_written;
}

uint8_t AppSdCard_CloseWrittenFile(AppSdCardFile_t *file)
{
    uint32_t restore_prescaler;
    uint8_t ok = 1U;

    if (!file || !file->write_open || !app_sd_present || !app_sd_volume.mounted)
        return 0U;

    restore_prescaler = hspi1.Init.BaudRatePrescaler;
    AppSdCard_BeginTransfer(APP_SD_SPI_DATA_PRESCALER);

    if (file->byte_offset_in_sector != 0U && file->current_cluster >= 2UL)
    {
        uint32_t sector_lba = AppSdCard_ClusterToLba(file->current_cluster)
            + file->sector_in_cluster;

        if (!AppSdCard_WriteBlockRaw(sector_lba, app_sd_sector_buffer))
            ok = 0U;
    }

    if (ok && !AppSdCard_WriteDirectoryEntry(file, file->fat_name))
        ok = 0U;

    AppSdCard_EndTransfer(restore_prescaler);
    file->write_open = 0U;
    return ok;
}
