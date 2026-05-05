#ifndef PERSISTENT_STORE_LAYOUT_H
#define PERSISTENT_STORE_LAYOUT_H

#include <stdint.h>

#define PERSISTENT_STORE_FLASH_ADDR 0x08100000UL
#define PERSISTENT_STORE_FLASH_SIZE_BYTES (128UL * 1024UL)

#define PERSISTENT_STORE_MAGIC_V1 0x50525331UL
#define PERSISTENT_STORE_MAGIC_V2 0x50525332UL

#define PERSISTENT_STORE_VERSION_PRESETS_ONLY 1UL
#define PERSISTENT_STORE_VERSION_PRESETS_AND_CONFIG 2UL

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t bank_count;
    uint32_t presets_per_bank;
    uint32_t preset_count;
    uint32_t payload_size;
    uint32_t checksum;
    uint32_t reserved;
} PersistentStoreHeaderV1_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t bank_count;
    uint32_t presets_per_bank;
    uint32_t preset_count;
    uint32_t payload_size;
    uint32_t checksum;
    uint32_t reserved;
    uint32_t config_size;
    uint32_t config_checksum;
} PersistentStoreHeaderV2_t;

#endif /* PERSISTENT_STORE_LAYOUT_H */