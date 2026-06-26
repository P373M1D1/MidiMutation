#ifndef APP_APP_SD_CARD_H
#define APP_APP_SD_CARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t first_cluster;
    uint32_t current_cluster;
    uint32_t file_size;
    uint32_t position;
    uint32_t directory_entry_lba;
    uint16_t directory_entry_offset;
    uint16_t byte_offset_in_sector;
    uint8_t fat_name[11];
    uint8_t sector_in_cluster;
    uint8_t write_open;
} AppSdCardFile_t;

typedef struct
{
    uint32_t single_block_reads;
    uint32_t multi_block_read_commands;
    uint32_t multi_block_read_blocks;
    uint32_t single_block_writes;
    uint32_t bytes_read;
    uint32_t bytes_written;
    uint32_t read_block_failures;
    uint32_t write_block_failures;
    uint32_t read_token_polls_total;
    uint32_t read_token_polls_max;
    uint32_t read_token_timeouts;
    uint32_t write_busy_polls_total;
    uint32_t write_busy_polls_max;
    uint32_t write_busy_timeouts;
    uint32_t read_block_ticks_total;
    uint32_t read_block_ticks_max;
    uint32_t multi_read_ticks_total;
    uint32_t multi_read_ticks_max;
    uint32_t write_block_ticks_total;
    uint32_t write_block_ticks_max;
    uint32_t fat_entry_reads;
    uint32_t fat_entry_writes;
    uint32_t root_dir_block_reads;
    uint32_t root_dir_block_writes;
    uint32_t clusters_allocated;
    uint32_t clusters_freed;
    uint32_t free_cluster_scan_steps;
    uint32_t transfer_windows;
    uint32_t exclusive_windows;
} AppSdCardMetrics_t;

void AppSdCard_InitAndProbe(void);
uint8_t AppSdCard_IsPresent(void);
uint8_t AppSdCard_IsHighCapacity(void);
uint8_t AppSdCard_IsFilesystemReady(void);
const char *AppSdCard_GetStatusText(void);
void AppSdCard_ResetMetrics(void);
void AppSdCard_GetMetrics(AppSdCardMetrics_t *metrics);
void AppSdCard_BeginExclusiveAccess(void);
void AppSdCard_EndExclusiveAccess(void);
uint8_t AppSdCard_OpenFile(AppSdCardFile_t *file, const char *filename);
uint8_t AppSdCard_SeekFile(AppSdCardFile_t *file, uint32_t position);
uint32_t AppSdCard_ReadFile(AppSdCardFile_t *file, uint8_t *buffer, uint32_t length);
uint8_t AppSdCard_OpenFileForWrite(AppSdCardFile_t *file, const char *filename);
uint32_t AppSdCard_WriteFile(AppSdCardFile_t *file, const uint8_t *buffer, uint32_t length);
uint8_t AppSdCard_CloseWrittenFile(AppSdCardFile_t *file);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_SD_CARD_H */
