#ifndef APP_APP_CONFIG_BACKUP_H
#define APP_APP_CONFIG_BACKUP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_CONFIG_BACKUP_RESULT_FAILED = 0,
    APP_CONFIG_BACKUP_RESULT_OK,
    APP_CONFIG_BACKUP_RESULT_PARTIAL,
    APP_CONFIG_BACKUP_RESULT_NO_SD_CARD,
    APP_CONFIG_BACKUP_RESULT_SD_UNAVAILABLE,
    APP_CONFIG_BACKUP_RESULT_BLOCKED_DEFAULTS,
    APP_CONFIG_BACKUP_RESULT_FACTORY_DEFAULT,
} AppConfigBackupResult_t;

typedef enum {
    APP_CONFIG_RESTORE_RESULT_FAILED = 0,
    APP_CONFIG_RESTORE_RESULT_OK,
    APP_CONFIG_RESTORE_RESULT_NO_SD_CARD,
    APP_CONFIG_RESTORE_RESULT_SD_UNAVAILABLE,
    APP_CONFIG_RESTORE_RESULT_FILE_NOT_FOUND,
    APP_CONFIG_RESTORE_RESULT_BAD_FORMAT,
} AppConfigRestoreResult_t;

uint8_t AppConfigBackup_WriteSdSnapshot(const char *reason);
AppConfigBackupResult_t AppConfigBackup_WriteSdSnapshotDetailed(const char *reason);
uint8_t AppConfigBackup_RestoreSdSnapshot(void);
AppConfigRestoreResult_t AppConfigBackup_RestoreSdSnapshotDetailed(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_CONFIG_BACKUP_H */
