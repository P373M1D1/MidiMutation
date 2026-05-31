#ifndef PERSISTENT_STORE_LAYOUT_H
#define PERSISTENT_STORE_LAYOUT_H

#include <stdint.h>

/* Flash layout contract for the editable preset/runtime-config store.
 *
 * Sector 12 and sector 13 are used as alternating full-image slots. The newer
 * V3 format writes the target slot completely, then commits it by programming
 * the final commit_marker word last so the previous slot remains bootable until
 * the replacement image is complete. */

#define PERSISTENT_STORE_FLASH_ADDR 0x08100000UL
#define PERSISTENT_STORE_FLASH_SIZE_BYTES (128UL * 1024UL)
#define PERSISTENT_STORE_SLOT0_FLASH_ADDR PERSISTENT_STORE_FLASH_ADDR
#define PERSISTENT_STORE_SLOT1_FLASH_ADDR 0x08120000UL

#define PERSISTENT_STORE_MAGIC_V1 0x50525331UL
#define PERSISTENT_STORE_MAGIC_V2 0x50525332UL
#define PERSISTENT_STORE_MAGIC_V3 0x50525333UL

#define PERSISTENT_STORE_VERSION_PRESETS_ONLY 1UL
#define PERSISTENT_STORE_VERSION_PRESETS_AND_CONFIG 2UL
#define PERSISTENT_STORE_VERSION_PRESETS_AND_CONFIG_ATOMIC 3UL
#define PERSISTENT_STORE_VERSION_PRESETS_AND_CONFIG_ATOMIC_COMPACT_DISPLAY_MODES 4UL
#define PERSISTENT_STORE_VERSION_PRESETS_AND_CONFIG_ATOMIC_USER_THEMES 5UL
#define PERSISTENT_STORE_VERSION_PRESETS_AND_CONFIG_ATOMIC_METRONOME 6UL
#define PERSISTENT_STORE_COMMIT_MARKER 0x434D4954UL

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
    uint32_t generation;
    uint32_t commit_marker;
} PersistentStoreHeaderV3_t;

/* Prefix size excludes the commit word so saves can program the header body
 * first and only mark the slot as valid in the very last flash write. */
#define PERSISTENT_STORE_HEADER_V3_PREFIX_SIZE (sizeof(PersistentStoreHeaderV3_t) - sizeof(uint32_t))

#endif /* PERSISTENT_STORE_LAYOUT_H */