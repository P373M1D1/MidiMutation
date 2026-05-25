#include "presets.h"
#include "app/app_state.h"
#include "app/app_board_init.h"
#include "led_functions.h"
#include "midi_devices.h"
#include "midi_functions.h"
#include "bpm_functions.h"
#include "persistent_store_layout.h"
#include "runtime_config.h"
#include "stm32f4xx_hal.h"
#include <string.h>

/* ── Bank names ─────────────────────────────────────────────────────────────
 * You can change these to any theme: "A/B", "Clean/Dirty", etc. */
#define PRESET_CC(channel, cc_number, value)  {channel, cc_number, value} /* helper for writing compact CC slot literals */
/* Shared "send nothing" CC list used by synthetic presets that exist only as
 * safe fallbacks or runtime overlays, not as real pedal-program recall data. */
#define PRESET_CC_NONE                        PRESET_CC(PRESET_CC_CHANNEL_UNUSED, PRESET_CC_NUMBER_UNUSED, PRESET_CC_VALUE_UNUSED) /* empty CC initializer stored as an explicit 0xFF/0xFF/0xFF triple */
#define PRESET_CC_EMPTY                       PRESET_CC_NONE /* backward-compatible alias for older table entries */
#define PRESET_CC_LIST_8(c0, c1, c2, c3, c4, c5, c6, c7) { c0, c1, c2, c3, c4, c5, c6, c7 } /* eight-slot CC initializer matching cc[0]..cc[7] */
#define PRESET_CC_LIST_EMPTY                  PRESET_CC_LIST_8(PRESET_CC_NONE, PRESET_CC_NONE, PRESET_CC_NONE, PRESET_CC_NONE, PRESET_CC_NONE, PRESET_CC_NONE, PRESET_CC_NONE, PRESET_CC_NONE) /* eight-slot initializer for presets with no extra CC messages */
#define PRESET_PROGRAM_SLOT(program_number)   {program_number} /* helper for one per-device Program Change slot */
#define PRESET_PROGRAM_LIST_8(p0, p1, p2, p3, p4, p5, p6, p7) { PRESET_PROGRAM_SLOT(p0), PRESET_PROGRAM_SLOT(p1), PRESET_PROGRAM_SLOT(p2), PRESET_PROGRAM_SLOT(p3), PRESET_PROGRAM_SLOT(p4), PRESET_PROGRAM_SLOT(p5), PRESET_PROGRAM_SLOT(p6), PRESET_PROGRAM_SLOT(p7) } /* eight-slot Program Change initializer matching preset/device slot order */
#define PRESET_PROGRAM_LIST_EMPTY PRESET_PROGRAM_LIST_8(PRESET_PROGRAM_NONE, PRESET_PROGRAM_NONE, PRESET_PROGRAM_NONE, PRESET_PROGRAM_NONE, PRESET_PROGRAM_NONE, PRESET_PROGRAM_NONE, PRESET_PROGRAM_NONE, PRESET_PROGRAM_NONE) /* initializer for presets with no program changes */
#define PRESET_FUNCTION_BUTTON_PROGRAM_MESSAGE_EMPTY { PRESET_CC_CHANNEL_UNUSED, PRESET_PROGRAM_NONE }
#define PRESET_FUNCTION_BUTTON_PROGRAM_MESSAGE_LIST_EMPTY { PRESET_FUNCTION_BUTTON_PROGRAM_MESSAGE_EMPTY, PRESET_FUNCTION_BUTTON_PROGRAM_MESSAGE_EMPTY, PRESET_FUNCTION_BUTTON_PROGRAM_MESSAGE_EMPTY, PRESET_FUNCTION_BUTTON_PROGRAM_MESSAGE_EMPTY }
#define PRESET_FUNCTION_BUTTON_CC_MESSAGE_LIST_EMPTY { PRESET_CC_NONE, PRESET_CC_NONE, PRESET_CC_NONE, PRESET_CC_NONE }
#define PRESET_FUNCTION_BUTTON_DEFAULT \
    { \
        .name = "SpcBtn", \
        .active_label = "active", \
        .inactive_label = "bypass", \
        .active_programs = PRESET_FUNCTION_BUTTON_PROGRAM_MESSAGE_LIST_EMPTY, \
        .active_cc = PRESET_FUNCTION_BUTTON_CC_MESSAGE_LIST_EMPTY, \
        .inactive_programs = PRESET_FUNCTION_BUTTON_PROGRAM_MESSAGE_LIST_EMPTY, \
        .inactive_cc = PRESET_FUNCTION_BUTTON_CC_MESSAGE_LIST_EMPTY, \
    }
#define PRESET_ROW(name, programs, cc_slots, relay1, relay2) { name, programs, cc_slots, { relay1, relay2 }, PRESET_FUNCTION_BUTTON_DEFAULT } /* compact row helper for the static preset table */
#define PRESET_ROW_EMPTY(name) PRESET_ROW(name, PRESET_PROGRAM_LIST_EMPTY, PRESET_CC_LIST_EMPTY, PRESET_RELAY_OPEN, PRESET_RELAY_OPEN) /* blank preset row used for placeholder banks */
#define PRESET_BANK_EMPTY { PRESET_ROW_EMPTY("Preset 1"), PRESET_ROW_EMPTY("Preset 2"), PRESET_ROW_EMPTY("Preset 3"), PRESET_ROW_EMPTY("Preset 4"), PRESET_ROW_EMPTY("Preset 5"), PRESET_ROW_EMPTY("Preset 6"), PRESET_ROW_EMPTY("Preset 7"), PRESET_ROW_EMPTY("Preset 8") } /* eight blank presets so future banks are immediately editable */
#define PRESET_RANDOM_LCG_SEED                0x6D2B79F5UL /* initial state for the random-preset pseudo-random generator */
#define PRESET_RANDOM_LCG_MULTIPLIER          1664525UL /* LCG multiplier used when generating random preset programs */
#define PRESET_RANDOM_LCG_INCREMENT           1013904223UL /* LCG increment used when generating random preset programs */

const char *Presets_GetBankName(uint8_t bank)
{
    return RuntimeConfig_GetBank(bank)->name;
}

static const uint32_t preset_flash_slot_addresses[] = {
    PERSISTENT_STORE_SLOT0_FLASH_ADDR,
    PERSISTENT_STORE_SLOT1_FLASH_ADDR,
};

static const uint32_t preset_flash_slot_sectors[] = {
    FLASH_SECTOR_12,
    FLASH_SECTOR_13,
};

typedef struct {
    char           name[PRESET_NAME_LENGTH + 1U];
    PresetDevice_t prg[PRESET_DEVICE_SLOTS];
    PresetCCSlot_t cc[PRESET_CC_SLOT_COUNT];
    uint8_t        relay[PRESET_RELAY_COUNT];
} PresetLegacy_t;

/* ── Preset table ────────────────────────────────────────────────────────────
 *
 * Each row: { "Name", {prg...}, {cc slots...}, {relay1, relay2} }
 *
 * Presets are stored banked so bank 2 / preset 1 is visually distinct in the
 * source from bank 1 / preset 1. External code still uses flat indices for now.
 *
 *   prg[0] → device slot 0 (currently channel 1)
 *   prg[1] → device slot 1 (currently channel 2)
 *   prg[2] → device slot 2 (currently channel 3)
 *   prg[3] → device slot 3 (currently channel 4)
 *   prg[4] → device slot 4 (currently channel 5)
 *   prg[5] → device slot 5 (currently channel 6)
 *   prg[6] → device slot 6 (currently channel 7)
 *   prg[7] → device slot 7 (currently channel 8)
 *
 *   pg      = Program Change number to send on load  (0xFF = skip)
 *   cc[N]   = extra CC messages sent on preset load (0xFF / 0xFF / 0xFF = skip)
 *   relay   = relay state  0=open/bypass  1=closed/engaged
 *
 * ─────────────────────────────────────────────────────────────────────────── */
// Each preset block below mirrors the display scroll layout:
// 8 program rows, then 8 CC rows, then 2 relay rows.
//
// Template for adding or editing a preset row:
// PRESET_ROW(
//     "Preset Name",
//     PRESET_PROGRAM_LIST_8(
//         PRESET_PROGRAM_NONE, /* CH 1 */
//         PRESET_PROGRAM_NONE, /* CH 2 */
//         PRESET_PROGRAM_NONE, /* CH 3 */
//         PRESET_PROGRAM_NONE, /* CH 4 */
//         PRESET_PROGRAM_NONE, /* CH 5 */
//         PRESET_PROGRAM_NONE, /* CH 6 */
//         PRESET_PROGRAM_NONE, /* CH 7 */
//         PRESET_PROGRAM_NONE  /* CH 8 */
//     ),
//     PRESET_CC_LIST_8(
//         PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
//         PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
//         PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
//         PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
//         PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
//         PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
//         PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
//         PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
//     ),
//     PRESET_RELAY_OPEN, /* Relay 1 */
//     PRESET_RELAY_OPEN  /* Relay 2 */
// ),
//
// Replace PRESET_PROGRAM_NONE with a real Program Change number for that MIDI channel.
// Replace PRESET_CC(0xFFU, 0xFFU, 0xFFU) with PRESET_CC(channel, cc_number, value) for any CC you want to send on preset load.
static const Preset_t preset_table[PRESET_BANK_COUNT][PRESETS_PER_BANK] = {
    {
        PRESET_ROW(
            "Soft Reverb",
            PRESET_PROGRAM_LIST_8(
                11,                    /* CH 1 */
                11,                    /* CH 2 */
                0,                     /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(1, 65, 127), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_CLOSED, /* Relay 1 */
            PRESET_RELAY_OPEN    /* Relay 2 */
        ),
        PRESET_ROW(
            "Perfect Tape",
            PRESET_PROGRAM_LIST_8(
                7,                     /* CH 1 */
                11,                    /* CH 2 */
                0,                     /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_CLOSED, /* Relay 1 */
            PRESET_RELAY_OPEN    /* Relay 2 */
        ),
        PRESET_ROW(
            "Deep Cave",
            PRESET_PROGRAM_LIST_8(
                11,                    /* CH 1 */
                12,                    /* CH 2 */
                0,                     /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "Press Tap to Hold",
            PRESET_PROGRAM_LIST_8(
                0,                     /* CH 1 */
                0,                     /* CH 2 */
                0,                     /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "Stars at Night",
            PRESET_PROGRAM_LIST_8(
                0,                     /* CH 1 */
                0,                     /* CH 2 */
                0,                     /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(12, 27, 127), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "Fade to Pad",
            PRESET_PROGRAM_LIST_8(
                0,                     /* CH 1 */
                0,                     /* CH 2 */
                0,                     /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "Psyco Shred",
            PRESET_PROGRAM_LIST_8(
                0,                     /* CH 1 */
                0,                     /* CH 2 */
                0,                     /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "Third Eye Open",
            PRESET_PROGRAM_LIST_8(
                0,                     /* CH 1 */
                0,                     /* CH 2 */
                0,                     /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
    },
    {
        PRESET_ROW(
            "Yoooo",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "Paaaaaaaa ",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "triiickkk!!",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "was ",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "geeeeeeeeeht",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "aaaaaaaabb!!",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "Preset 7",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "Preset 8",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
    },
    {
        PRESET_ROW(
            "und",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "ey",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "...",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "external tempo geht",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "sogar mit error",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "woohooo!!",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "Preset 7",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "Preset 8",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
    },
    {
        PRESET_ROW(
            "Preset 1",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "Preset 2",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "Preset 3",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "Preset 4",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "Preset 5",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "Preset 6",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "Preset 7",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
        PRESET_ROW(
            "Preset 8",
            PRESET_PROGRAM_LIST_8(
                PRESET_PROGRAM_NONE, /* CH 1 */
                PRESET_PROGRAM_NONE, /* CH 2 */
                PRESET_PROGRAM_NONE, /* CH 3 */
                PRESET_PROGRAM_NONE, /* CH 4 */
                PRESET_PROGRAM_NONE, /* CH 5 */
                PRESET_PROGRAM_NONE, /* CH 6 */
                PRESET_PROGRAM_NONE, /* CH 7 */
                PRESET_PROGRAM_NONE  /* CH 8 */
            ),
            PRESET_CC_LIST_8(
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 1 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 2 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 3 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 4 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 5 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 6 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU), /* CC 7 */
                PRESET_CC(0xFFU, 0xFFU, 0xFFU)  /* CC 8 */
            ),
            PRESET_RELAY_OPEN, /* Relay 1 */
            PRESET_RELAY_OPEN  /* Relay 2 */
        ),
    },
    PRESET_BANK_EMPTY,
    PRESET_BANK_EMPTY,
    PRESET_BANK_EMPTY,
    PRESET_BANK_EMPTY,
};

static Preset_t preset_store[PRESET_COUNT];
static uint8_t preset_store_initialized = 0U;
static uint8_t preset_store_dirty = 0U;

static size_t Presets_GetLegacyPayloadSize(void)
{
    return sizeof(PresetLegacy_t) * PRESET_COUNT;
}

static uint8_t Presets_PayloadSizeIsSupported(size_t payload_size)
{
    return (payload_size == sizeof(preset_store) || payload_size == Presets_GetLegacyPayloadSize()) ? 1U : 0U;
}

static void Presets_SetFunctionButtonDefaults(RuntimeConfigFunctionButton_t *function_button)
{
    static const RuntimeConfigFunctionButton_t default_function_button = PRESET_FUNCTION_BUTTON_DEFAULT;

    if (!function_button)
        return;

    *function_button = default_function_button;
}

static void Presets_CopyLegacyStore(const PresetLegacy_t *legacy_store)
{
    if (!legacy_store)
        return;

    memcpy(preset_store, preset_table, sizeof(preset_store));

    for (uint8_t index = 0U; index < PRESET_COUNT; ++index)
    {
        memcpy(preset_store[index].name, legacy_store[index].name, sizeof(legacy_store[index].name));
        memcpy(preset_store[index].prg, legacy_store[index].prg, sizeof(legacy_store[index].prg));
        memcpy(preset_store[index].cc, legacy_store[index].cc, sizeof(legacy_store[index].cc));
        memcpy(preset_store[index].relay, legacy_store[index].relay, sizeof(legacy_store[index].relay));
        Presets_SetFunctionButtonDefaults(&preset_store[index].function_button);
    }
}

static uint32_t Presets_FlashChecksum(const uint8_t *data, size_t size)
{
    uint32_t hash = 2166136261UL;

    for (size_t index = 0U; index < size; ++index)
    {
        hash ^= data[index];
        hash *= 16777619UL;
    }

    return hash;
}

static uint8_t Presets_FlashGenerationIsNewer(uint32_t candidate, uint32_t reference)
{
    return ((int32_t)(candidate - reference) > 0) ? 1U : 0U;
}

static uint8_t Presets_FlashHeaderV1IsValid(const PersistentStoreHeaderV1_t *header)
{
    if (!header)
        return 0U;

    return (header->magic == PERSISTENT_STORE_MAGIC_V1
         && header->version == PERSISTENT_STORE_VERSION_PRESETS_ONLY
         && header->bank_count == PRESET_BANK_COUNT
         && header->presets_per_bank == PRESETS_PER_BANK
         && header->preset_count == PRESET_COUNT
         && Presets_PayloadSizeIsSupported(header->payload_size)) ? 1U : 0U;
}

static uint8_t Presets_FlashHeaderV2IsValid(const PersistentStoreHeaderV2_t *header)
{
    if (!header)
        return 0U;

     return (header->magic == PERSISTENT_STORE_MAGIC_V2
         && header->version == PERSISTENT_STORE_VERSION_PRESETS_AND_CONFIG
         && header->bank_count == PRESET_BANK_COUNT
         && header->presets_per_bank == PRESETS_PER_BANK
         && header->preset_count == PRESET_COUNT
            && Presets_PayloadSizeIsSupported(header->payload_size)
            && header->config_size > 0U
         && ((sizeof(PersistentStoreHeaderV2_t)
            + header->payload_size
            + header->config_size) <= PERSISTENT_STORE_FLASH_SIZE_BYTES)) ? 1U : 0U;
}

static uint8_t Presets_FlashHeaderV3IsValid(const PersistentStoreHeaderV3_t *header)
{
    if (!header)
        return 0U;

    /* commit_marker must already be present here, so partially written slots
     * are rejected before any payload checks happen. */
    return (header->magic == PERSISTENT_STORE_MAGIC_V3
           && (header->version == PERSISTENT_STORE_VERSION_PRESETS_AND_CONFIG_ATOMIC
            || header->version == PERSISTENT_STORE_VERSION_PRESETS_AND_CONFIG_ATOMIC_COMPACT_DISPLAY_MODES
            || header->version == PERSISTENT_STORE_VERSION_PRESETS_AND_CONFIG_ATOMIC_USER_THEMES
            || header->version == PERSISTENT_STORE_VERSION_PRESETS_AND_CONFIG_ATOMIC_METRONOME)
         && header->commit_marker == PERSISTENT_STORE_COMMIT_MARKER
         && header->bank_count == PRESET_BANK_COUNT
         && header->presets_per_bank == PRESETS_PER_BANK
         && header->preset_count == PRESET_COUNT
            && Presets_PayloadSizeIsSupported(header->payload_size)
         && header->config_size > 0U
         && ((sizeof(PersistentStoreHeaderV3_t)
            + header->payload_size
            + header->config_size) <= PERSISTENT_STORE_FLASH_SIZE_BYTES)) ? 1U : 0U;
}

static uint8_t Presets_FlashV3ImageIsValid(uint32_t slot_address,
                                           const PersistentStoreHeaderV3_t **header_out)
{
    const PersistentStoreHeaderV3_t *header = (const PersistentStoreHeaderV3_t *)slot_address;
    const uint8_t *preset_payload;
    const uint8_t *config_payload;

    if (!Presets_FlashHeaderV3IsValid(header))
        return 0U;

    /* The combined image is validated end-to-end: header, preset payload, and
     * runtime-config payload must all match their stored checksums. */
    preset_payload = (const uint8_t *)(slot_address + sizeof(PersistentStoreHeaderV3_t));
    config_payload = preset_payload + header->payload_size;

    if (Presets_FlashChecksum(preset_payload, header->payload_size) != header->checksum)
        return 0U;

    if (Presets_FlashChecksum(config_payload, header->config_size) != header->config_checksum)
        return 0U;

    if (header_out)
        *header_out = header;

    return 1U;
}

static uint8_t Presets_FlashFindLatestV3Store(uint32_t *slot_address_out,
                                              uint32_t *generation_out)
{
    uint8_t found = 0U;
    uint32_t selected_address = 0U;
    uint32_t selected_generation = 0U;

    /* At boot we prefer the highest valid generation rather than a fixed slot,
     * so either sector can survive as the last good image after power loss. */
    for (size_t slot_index = 0U; slot_index < (sizeof(preset_flash_slot_addresses) / sizeof(preset_flash_slot_addresses[0])); ++slot_index)
    {
        const PersistentStoreHeaderV3_t *header = NULL;

        if (!Presets_FlashV3ImageIsValid(preset_flash_slot_addresses[slot_index], &header))
            continue;

        if (!found || Presets_FlashGenerationIsNewer(header->generation, selected_generation))
        {
            found = 1U;
            selected_address = preset_flash_slot_addresses[slot_index];
            selected_generation = header->generation;
        }
    }

    if (slot_address_out)
        *slot_address_out = selected_address;

    if (generation_out)
        *generation_out = selected_generation;

    return found;
}

static uint8_t Presets_FlashLegacyStoreIsValid(void)
{
    const PersistentStoreHeaderV1_t *header_v1 = (const PersistentStoreHeaderV1_t *)PERSISTENT_STORE_FLASH_ADDR;
    const PersistentStoreHeaderV2_t *header_v2 = (const PersistentStoreHeaderV2_t *)PERSISTENT_STORE_FLASH_ADDR;
    const uint8_t *preset_payload;

    if (Presets_FlashHeaderV2IsValid(header_v2))
    {
        preset_payload = (const uint8_t *)(PERSISTENT_STORE_FLASH_ADDR + sizeof(PersistentStoreHeaderV2_t));
        return (Presets_FlashChecksum(preset_payload, header_v2->payload_size) == header_v2->checksum) ? 1U : 0U;
    }

    if (!Presets_FlashHeaderV1IsValid(header_v1))
        return 0U;

    preset_payload = (const uint8_t *)(PERSISTENT_STORE_FLASH_ADDR + sizeof(PersistentStoreHeaderV1_t));
    return (Presets_FlashChecksum(preset_payload, header_v1->payload_size) == header_v1->checksum) ? 1U : 0U;
}

static uint8_t Presets_FlashLoadRuntimeStore(void)
{
    uint32_t slot_address = 0U;
    const PersistentStoreHeaderV1_t *header_v1 = (const PersistentStoreHeaderV1_t *)PERSISTENT_STORE_FLASH_ADDR;
    const PersistentStoreHeaderV2_t *header_v2 = (const PersistentStoreHeaderV2_t *)PERSISTENT_STORE_FLASH_ADDR;
    const uint8_t *preset_payload;

    if (Presets_FlashFindLatestV3Store(&slot_address, NULL))
    {
        const PersistentStoreHeaderV3_t *header = (const PersistentStoreHeaderV3_t *)slot_address;

        preset_payload = (const uint8_t *)(slot_address + sizeof(PersistentStoreHeaderV3_t));
        if (header->payload_size == sizeof(preset_store))
            memcpy(preset_store, preset_payload, sizeof(preset_store));
        else
            Presets_CopyLegacyStore((const PresetLegacy_t *)preset_payload);
        return 1U;
    }

    if (Presets_FlashHeaderV2IsValid(header_v2))
    {
        preset_payload = (const uint8_t *)(PERSISTENT_STORE_FLASH_ADDR + sizeof(PersistentStoreHeaderV2_t));
        if (Presets_FlashChecksum(preset_payload, header_v2->payload_size) != header_v2->checksum)
            return 0U;

        if (header_v2->payload_size == sizeof(preset_store))
            memcpy(preset_store, preset_payload, sizeof(preset_store));
        else
            Presets_CopyLegacyStore((const PresetLegacy_t *)preset_payload);

        return 1U;
    }

    if (!Presets_FlashHeaderV1IsValid(header_v1))
        return 0U;

    preset_payload = (const uint8_t *)(PERSISTENT_STORE_FLASH_ADDR + sizeof(PersistentStoreHeaderV1_t));
    if (Presets_FlashChecksum(preset_payload, header_v1->payload_size) != header_v1->checksum)
        return 0U;

    if (header_v1->payload_size == sizeof(preset_store))
        memcpy(preset_store, preset_payload, sizeof(preset_store));
    else
        Presets_CopyLegacyStore((const PresetLegacy_t *)preset_payload);
    return 1U;
}

static uint8_t Presets_FlashProgramBuffer(uint32_t address,
                                          const uint8_t *data,
                                          size_t size)
{
    for (size_t offset = 0U; offset < size; offset += sizeof(uint32_t))
    {
        uint32_t word = 0xFFFFFFFFUL;
        size_t chunk = size - offset;

        if (chunk > sizeof(uint32_t))
            chunk = sizeof(uint32_t);

        memcpy(&word, data + offset, chunk);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + offset, word) != HAL_OK)
            return 0U;
    }

    return 1U;
}

static uint8_t Presets_FlashSaveRuntimeStore(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    PersistentStoreHeaderV3_t header;
    RuntimeConfig_t config_snapshot;
    const RuntimeConfig_t *config = &config_snapshot;
    uint32_t latest_slot_address = 0U;
    uint32_t latest_generation = 0U;
    uint32_t sector_error = 0U;
    uint32_t target_address;
    uint32_t target_sector;
    uint8_t saved = 0U;
    uint8_t target_slot_index;

    RuntimeConfig_CopyPersistentSaveSnapshot(&config_snapshot);

    if ((sizeof(PersistentStoreHeaderV3_t) + sizeof(preset_store) + sizeof(RuntimeConfig_t)) > PERSISTENT_STORE_FLASH_SIZE_BYTES)
        return 0U;

    /* Always write the opposite slot from the newest valid image. That keeps
     * one complete bootable copy intact until the replacement image is fully
     * programmed and committed. */
    if (Presets_FlashFindLatestV3Store(&latest_slot_address, &latest_generation))
    {
        target_slot_index = (latest_slot_address == preset_flash_slot_addresses[0]) ? 1U : 0U;
        header.generation = latest_generation + 1U;
        if (header.generation == 0U)
            header.generation = 1U;
    }
    else
    {
        target_slot_index = Presets_FlashLegacyStoreIsValid() ? 1U : 0U;
        header.generation = 1U;
    }

    target_address = preset_flash_slot_addresses[target_slot_index];
    target_sector = preset_flash_slot_sectors[target_slot_index];

    header.magic = PERSISTENT_STORE_MAGIC_V3;
    header.version = PERSISTENT_STORE_VERSION_PRESETS_AND_CONFIG_ATOMIC_METRONOME;
    header.bank_count = PRESET_BANK_COUNT;
    header.presets_per_bank = PRESETS_PER_BANK;
    header.preset_count = PRESET_COUNT;
    header.payload_size = sizeof(preset_store);
    header.checksum = Presets_FlashChecksum((const uint8_t *)preset_store, sizeof(preset_store));
    header.reserved = 0U;
    header.config_size = sizeof(RuntimeConfig_t);
    header.config_checksum = Presets_FlashChecksum((const uint8_t *)config, sizeof(RuntimeConfig_t));
    header.commit_marker = PERSISTENT_STORE_COMMIT_MARKER;

    if (HAL_FLASH_Unlock() != HAL_OK)
        return 0U;

#if defined(FLASH_FLAG_RDERR)
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR | FLASH_FLAG_RDERR);
#else
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
#endif

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector = target_sector;
    erase.NbSectors = 1U;

    if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK)
        goto done;

    if (!Presets_FlashProgramBuffer(target_address + sizeof(PersistentStoreHeaderV3_t),
                                    (const uint8_t *)preset_store,
                                    sizeof(preset_store)))
        goto done;

    if (!Presets_FlashProgramBuffer(target_address + sizeof(PersistentStoreHeaderV3_t) + sizeof(preset_store),
                                    (const uint8_t *)config,
                                    sizeof(RuntimeConfig_t)))
        goto done;

    /* Program the header body first, then the commit marker word last. A reset
     * before the final write leaves the new slot invalid, so boot falls back to
     * the previous generation instead of a half-written replacement. */
    if (!Presets_FlashProgramBuffer(target_address,
                                    (const uint8_t *)&header,
                                    PERSISTENT_STORE_HEADER_V3_PREFIX_SIZE))
        goto done;

    if (!Presets_FlashProgramBuffer(target_address + PERSISTENT_STORE_HEADER_V3_PREFIX_SIZE,
                                    (const uint8_t *)&header.commit_marker,
                                    sizeof(header.commit_marker)))
        goto done;

    saved = 1U;

done:
    HAL_FLASH_Lock();
    return saved;
}

static void Presets_ApplyFactoryDefaults(Preset_t *preset, uint8_t index)
{
    static const char * const preset_default_names[PRESETS_PER_BANK] = {
        "Preset 1",
        "Preset 2",
        "Preset 3",
        "Preset 4",
        "Preset 5",
        "Preset 6",
        "Preset 7",
        "Preset 8",
    };
    uint8_t preset_index;
    size_t name_length;

    if (!preset || index >= PRESET_COUNT)
        return;

    for (uint8_t slot = 0U; slot < PRESET_DEVICE_SLOTS; ++slot)
        preset->prg[slot].program = PRESET_PROGRAM_NONE;

    for (uint8_t cc_index = 0U; cc_index < PRESET_CC_SLOT_COUNT; ++cc_index)
    {
        preset->cc[cc_index].channel = PRESET_CC_CHANNEL_UNUSED;
        preset->cc[cc_index].cc_number = PRESET_CC_NUMBER_UNUSED;
        preset->cc[cc_index].value = PRESET_CC_VALUE_UNUSED;
    }

    for (uint8_t relay_index = 0U; relay_index < PRESET_RELAY_COUNT; ++relay_index)
        preset->relay[relay_index] = PRESET_RELAY_OPEN;

    Presets_SetFunctionButtonDefaults(&preset->function_button);

    preset_index = (uint8_t)(index % PRESETS_PER_BANK);
    memset(preset->name, 0, sizeof(preset->name));
    name_length = strlen(preset_default_names[preset_index]);
    if (name_length > PRESET_NAME_LENGTH)
        name_length = PRESET_NAME_LENGTH;

    memcpy(preset->name, preset_default_names[preset_index], name_length);
}

static void Presets_EnsureRuntimeStore(void)
{
    if (preset_store_initialized)
        return;

    memcpy(preset_store, preset_table, sizeof(preset_store));
    Presets_FlashLoadRuntimeStore();
    preset_store_initialized = 1U;
    preset_store_dirty = 0U;
}

/* Fallback returned when index is out of range.
 * Every per-device program slot is explicitly marked PRESET_PROGRAM_UNUSED
 * because Preset_t has no single "disabled" flag for the whole preset: each
 * device slot must say "send nothing" on its own. Keeping CCs empty and both
 * relays open makes an out-of-range lookup visually harmless and side-effect free.
 */
static const Preset_t blank_preset = {
    .name  = "---",
    .prg   = PRESET_PROGRAM_LIST_EMPTY,
    .cc    = PRESET_CC_LIST_EMPTY,
    .relay = { PRESET_RELAY_OPEN, PRESET_RELAY_OPEN },
    .function_button = PRESET_FUNCTION_BUTTON_DEFAULT,
};

/* Runtime-built shell used by Presets_ActivateRandom().
 * It also starts fully "unused" for the same reason: until Presets_ActivateRandom()
 * fills the real device slots, this preset must not accidentally send stale
 * Program Changes, CCs, or relay changes. The remaining slots stay unused
 * because random mode currently mutates only the real pedals.
 */
static Preset_t random_preset = {
    .name  = "Screw this gig!", /* placeholder name shown while the real random preset is being built at runtime */
    .prg   = PRESET_PROGRAM_LIST_EMPTY,
    .cc    = PRESET_CC_LIST_EMPTY,
    .relay = { PRESET_RELAY_OPEN, PRESET_RELAY_OPEN },
    .function_button = PRESET_FUNCTION_BUTTON_DEFAULT,
};

static uint8_t Presets_CurrentBankUsesWetDry(void)
{
    const RuntimeConfigBank_t *bank = RuntimeConfig_GetBank(current_bank);

    return (bank && bank->wet_dry_enabled) ? 1U : 0U;
}

static void Presets_ApplyRelayOutputs(const Preset_t *preset)
{
    if (!preset)
        return;

    for (uint8_t relay_index = 0U; relay_index < PRESET_RELAY_COUNT; ++relay_index)
        AppBoard_SetRelayState(relay_index, preset->relay[relay_index] ? 1U : 0U);
}

const Preset_t *Presets_GetGlobalBypassPreset(void)
{
    return RuntimeConfig_GetGlobalBypassPreset();
}

Preset_t *Presets_GetMutableGlobalBypassPreset(void)
{
    return RuntimeConfig_GetMutableGlobalBypassPreset();
}

const Preset_t *Presets_GetGlobalMutePreset(void)
{
    return RuntimeConfig_GetGlobalMutePreset();
}

Preset_t *Presets_GetMutableGlobalMutePreset(void)
{
    return RuntimeConfig_GetMutableGlobalMutePreset();
}

uint8_t Presets_IsGlobalBypassPreset(const Preset_t *preset)
{
    return (preset && preset == RuntimeConfig_GetGlobalBypassPreset()) ? 1U : 0U;
}

uint8_t Presets_IsGlobalMutePreset(const Preset_t *preset)
{
    return (preset && preset == RuntimeConfig_GetGlobalMutePreset()) ? 1U : 0U;
}

static const Preset_t *Presets_GetButton11PresetForCurrentBank(void)
{
    if (Presets_CurrentBankUsesWetDry())
        return Presets_GetGlobalMutePreset();

    return Presets_GetGlobalBypassPreset();
}

/* -------------------------------------------------------------------------- */

/* Shared activation path for normal presets, random preset, and mute preset.
 * update_index controls whether this activation should become the persisted
 * "current preset" or just temporarily repaint/run an overlay preset. */
static void App_ActivatePresetData(const Preset_t *preset, uint8_t update_index, uint8_t idx)
{
    if (!preset)
        return;

    if (update_index)
    {
        AppState_ActivatePresetSelection(preset, idx);
        LED_SetPresetIndicator((uint8_t)(idx % PRESETS_PER_BANK));
    }
    else
    {
        AppState_SetActiveOverlayPreset(preset);
    }

    Midi_LoadPreset(preset);
    Presets_ApplyRelayOutputs(preset);

    if (update_index) {
        AppState_ScheduleRuntimeStateSaveAt(HAL_GetTick() + BPM_SAVE_DELAY_MS);
    }
}

static uint8_t Presets_NextRandomProgram(uint8_t max_preset)
{
    /* Simple on-device LCG mixed with the current HAL tick so successive
     * button presses do not walk the exact same short sequence after boot. */
    static uint32_t random_state = PRESET_RANDOM_LCG_SEED;

    random_state = (random_state * PRESET_RANDOM_LCG_MULTIPLIER)
                 + PRESET_RANDOM_LCG_INCREMENT
                 + HAL_GetTick();
    return (uint8_t)(random_state % ((uint32_t)max_preset + 1UL));
}

static const Preset_t *Presets_GetFlat(uint8_t index)
{
    /* Presets are stored banked for readability in the source table, but most
     * runtime code still addresses them through a flat 0..PRESET_COUNT-1 index. */
    Presets_EnsureRuntimeStore();

    if (index >= PRESET_COUNT)
        return &blank_preset;

    return &preset_store[index];
}

/* -------------------------------------------------------------------------- */

bool Presets_DeviceProgramIsShared(uint8_t slot, uint8_t program)
{
    if (program == PRESET_PROGRAM_UNUSED || slot >= PRESET_DEVICE_SLOTS)
        return false;

    Presets_EnsureRuntimeStore();

    uint8_t count = 0U;
    for (uint8_t index = 0U; index < PRESET_COUNT; index++)
    {
        if (preset_store[index].prg[slot].program == program)
            count++;

        if (count > 1U)
            return true;
    }

    return false;
}

/* -------------------------------------------------------------------------- */

const Preset_t *Presets_Get(uint8_t index)
{
    if (index == PRESET_GLOBAL_BYPASS_INDEX)
        return Presets_GetGlobalBypassPreset();

    if (index == PRESET_GLOBAL_MUTE_INDEX)
        return Presets_GetGlobalMutePreset();

    if (index >= PRESET_COUNT)
        return &blank_preset;
    return Presets_GetFlat(index);
}

Preset_t *Presets_GetMutable(uint8_t index)
{
    Presets_EnsureRuntimeStore();

    if (index == PRESET_GLOBAL_BYPASS_INDEX)
        return Presets_GetMutableGlobalBypassPreset();

    if (index == PRESET_GLOBAL_MUTE_INDEX)
        return Presets_GetMutableGlobalMutePreset();

    if (index >= PRESET_COUNT)
        return NULL;

    return &preset_store[index];
}

const RuntimeConfigFunctionButton_t *Presets_GetFunctionButton(uint8_t index)
{
    const Preset_t *preset = Presets_Get(index);

    return preset ? &preset->function_button : NULL;
}

RuntimeConfigFunctionButton_t *Presets_GetMutableFunctionButton(uint8_t index)
{
    Preset_t *preset = Presets_GetMutable(index);

    return preset ? &preset->function_button : NULL;
}

const RuntimeConfigFunctionButton_t *Presets_GetActiveFunctionButton(void)
{
    const Preset_t *preset = AppState_GetActivePreset();

    return preset ? &preset->function_button : NULL;
}

void Presets_ResetPresetToDefaults(uint8_t index)
{
    uint8_t bank_index;
    uint8_t preset_index;

    Presets_EnsureRuntimeStore();

    if (index == PRESET_GLOBAL_BYPASS_INDEX)
    {
        RuntimeConfig_ResetGlobalBypassPresetToDefaults();
        return;
    }

    if (index == PRESET_GLOBAL_MUTE_INDEX)
    {
        RuntimeConfig_ResetGlobalMutePresetToDefaults();
        return;
    }

    if (index >= PRESET_COUNT)
        return;

    bank_index = (uint8_t)(index / PRESETS_PER_BANK);
    preset_index = (uint8_t)(index % PRESETS_PER_BANK);
    preset_store[index] = preset_table[bank_index][preset_index];
    Presets_ApplyFactoryDefaults(&preset_store[index], index);
}

void Presets_MarkDirty(void)
{
    Presets_EnsureRuntimeStore();
    preset_store_dirty = 1U;
}

uint8_t Presets_IsDirty(void)
{
    Presets_EnsureRuntimeStore();
    return preset_store_dirty;
}

uint8_t Presets_SaveIfDirty(void)
{
    Presets_EnsureRuntimeStore();

    if (!preset_store_dirty && !RuntimeConfig_IsDirty())
        return 1U;

    if (!Presets_FlashSaveRuntimeStore())
        return 0U;

    preset_store_dirty = 0U;
    RuntimeConfig_ClearDirty();
    LED_FlashPulse();
    return 1U;
}

uint8_t Presets_Count(void)
{
    return PRESET_COUNT;
}

/* -------------------------------------------------------------------------- */

void App_ActivatePreset(uint8_t idx)
{
    const Preset_t *preset = NULL;

    if (idx >= Presets_Count())
        return;

    preset = Presets_Get(idx);
    if (AppState_IsActivePreset(preset))
        return;

    App_ActivatePresetData(preset, 1U, idx);
}

void Presets_ActivateRandom(void)
{
    const MidiDevice_t *first_device = MidiDevices_Get(0U);
    const MidiDevice_t *second_device = MidiDevices_Get(1U);

    /* Random preset picks one legal program per real device and leaves the
     * remaining slots intentionally unused. */
    random_preset.prg[0].program = Presets_NextRandomProgram(first_device->max_preset);
    random_preset.prg[1].program = Presets_NextRandomProgram(second_device->max_preset);
    for (uint8_t slot = 2U; slot < PRESET_DEVICE_SLOTS; slot++)
    {
        random_preset.prg[slot].program = PRESET_PROGRAM_UNUSED;
    }

    App_ActivatePresetData(&random_preset, 0U, 0U);
    LED_SetActiveButtonIndicator(8U);
}

void Presets_ActivateMute(void)
{
    const Preset_t *overlay_preset = Presets_GetButton11PresetForCurrentBank();

    App_ActivatePresetData(overlay_preset, 0U, 0U);

    LED_SetActiveButtonIndicator(10U);
}
