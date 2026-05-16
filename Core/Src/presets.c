#include "presets.h"
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
#define PRESET_ROW(name, programs, cc_slots, relay1, relay2) { name, programs, cc_slots, { relay1, relay2 } } /* compact row helper for the static preset table */
#define PRESET_ROW_EMPTY(name) PRESET_ROW(name, PRESET_PROGRAM_LIST_EMPTY, PRESET_CC_LIST_EMPTY, PRESET_RELAY_OPEN, PRESET_RELAY_OPEN) /* blank preset row used for placeholder banks */
#define PRESET_BANK_EMPTY { PRESET_ROW_EMPTY("Preset 1"), PRESET_ROW_EMPTY("Preset 2"), PRESET_ROW_EMPTY("Preset 3"), PRESET_ROW_EMPTY("Preset 4"), PRESET_ROW_EMPTY("Preset 5"), PRESET_ROW_EMPTY("Preset 6"), PRESET_ROW_EMPTY("Preset 7"), PRESET_ROW_EMPTY("Preset 8") } /* eight blank presets so future banks are immediately editable */
#define PRESET_RANDOM_LCG_SEED                0x6D2B79F5UL /* initial state for the random-preset pseudo-random generator */
#define PRESET_RANDOM_LCG_MULTIPLIER          1664525UL /* LCG multiplier used when generating random preset programs */
#define PRESET_RANDOM_LCG_INCREMENT           1013904223UL /* LCG increment used when generating random preset programs */

const char *Presets_GetBankName(uint8_t bank)
{
    return RuntimeConfig_GetBank(bank)->name;
}

/* ── Application state owned by main.cpp ─────────────────────────────────── */
extern volatile uint16_t  g_bpm;
extern volatile uint32_t  bpm_save_tick;
extern const Preset_t    *active_preset;
extern uint8_t            active_preset_index;
volatile uint8_t          current_bank = 0U;

static const uint32_t preset_flash_slot_addresses[] = {
    PERSISTENT_STORE_SLOT0_FLASH_ADDR,
    PERSISTENT_STORE_SLOT1_FLASH_ADDR,
};

static const uint32_t preset_flash_slot_sectors[] = {
    FLASH_SECTOR_12,
    FLASH_SECTOR_13,
};

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
         && header->payload_size == sizeof(preset_store)) ? 1U : 0U;
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
         && header->payload_size == sizeof(preset_store)
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
            || header->version == PERSISTENT_STORE_VERSION_PRESETS_AND_CONFIG_ATOMIC_USER_THEMES)
         && header->commit_marker == PERSISTENT_STORE_COMMIT_MARKER
         && header->bank_count == PRESET_BANK_COUNT
         && header->presets_per_bank == PRESETS_PER_BANK
         && header->preset_count == PRESET_COUNT
         && header->payload_size == sizeof(preset_store)
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
        return (Presets_FlashChecksum(preset_payload, sizeof(preset_store)) == header_v2->checksum) ? 1U : 0U;
    }

    if (!Presets_FlashHeaderV1IsValid(header_v1))
        return 0U;

    preset_payload = (const uint8_t *)(PERSISTENT_STORE_FLASH_ADDR + sizeof(PersistentStoreHeaderV1_t));
    return (Presets_FlashChecksum(preset_payload, sizeof(preset_store)) == header_v1->checksum) ? 1U : 0U;
}

static uint8_t Presets_FlashLoadRuntimeStore(void)
{
    uint32_t slot_address = 0U;
    const PersistentStoreHeaderV1_t *header_v1 = (const PersistentStoreHeaderV1_t *)PERSISTENT_STORE_FLASH_ADDR;
    const PersistentStoreHeaderV2_t *header_v2 = (const PersistentStoreHeaderV2_t *)PERSISTENT_STORE_FLASH_ADDR;
    const uint8_t *preset_payload;

    if (Presets_FlashFindLatestV3Store(&slot_address, NULL))
    {
        preset_payload = (const uint8_t *)(slot_address + sizeof(PersistentStoreHeaderV3_t));
        memcpy(preset_store, preset_payload, sizeof(preset_store));
        return 1U;
    }

    if (Presets_FlashHeaderV2IsValid(header_v2))
    {
        preset_payload = (const uint8_t *)(PERSISTENT_STORE_FLASH_ADDR + sizeof(PersistentStoreHeaderV2_t));
        if (Presets_FlashChecksum(preset_payload, sizeof(preset_store)) != header_v2->checksum)
            return 0U;

        memcpy(preset_store, preset_payload, sizeof(preset_store));

        return 1U;
    }

    if (!Presets_FlashHeaderV1IsValid(header_v1))
        return 0U;

    preset_payload = (const uint8_t *)(PERSISTENT_STORE_FLASH_ADDR + sizeof(PersistentStoreHeaderV1_t));
    if (Presets_FlashChecksum(preset_payload, sizeof(preset_store)) != header_v1->checksum)
        return 0U;

    memcpy(preset_store, preset_payload, sizeof(preset_store));
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
    const RuntimeConfig_t *config = RuntimeConfig_Get();
    uint32_t latest_slot_address = 0U;
    uint32_t latest_generation = 0U;
    uint32_t sector_error = 0U;
    uint32_t target_address;
    uint32_t target_sector;
    uint8_t saved = 0U;
    uint8_t target_slot_index;

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
    header.version = PERSISTENT_STORE_VERSION_PRESETS_AND_CONFIG_ATOMIC_USER_THEMES;
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
};

/* Overlay preset used by Presets_ActivateMute().
 * Mute should not recall pedal patches or fire extra CCs, so every device slot
 * stays PRESET_PROGRAM_UNUSED and the CC list is empty. Both relays default to
 * open/bypass so engaging mute leaves the hardware path in the safest neutral state.
 */
static const Preset_t mute_preset = {
    .name  = "Mute / Bypass",
    .prg   = PRESET_PROGRAM_LIST_EMPTY,
    .cc    = PRESET_CC_LIST_EMPTY,
    .relay = { PRESET_RELAY_OPEN, PRESET_RELAY_OPEN },
};

static uint8_t Presets_CurrentBankUsesWetDry(void)
{
    const RuntimeConfigBank_t *bank = RuntimeConfig_GetBank(current_bank);

    return (bank && bank->wet_dry_enabled) ? 1U : 0U;
}

static void Presets_SendWetDryMuteLevels(void)
{
    for (uint8_t device_index = 0U; device_index < PRESET_DEVICE_SLOTS; device_index++)
    {
        const RuntimeConfigDevice_t *device = RuntimeConfig_GetDevice(device_index);

        if (!device || device->level.cc == PRESET_CC_NUMBER_UNUSED)
            continue;

        MIDI_SendCC(device->channel, device->level.cc, 0U);
    }
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
        active_preset_index = idx;
        LED_SetPresetIndicator((uint8_t)(idx % PRESETS_PER_BANK));
    }

    active_preset = preset;
    Midi_LoadPreset(active_preset);

    if (update_index) {
        bpm_save_tick = HAL_GetTick() + BPM_SAVE_DELAY_MS;
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
    if (index >= PRESET_COUNT)
        return &blank_preset;
    return Presets_GetFlat(index);
}

Preset_t *Presets_GetMutable(uint8_t index)
{
    Presets_EnsureRuntimeStore();

    if (index >= PRESET_COUNT)
        return NULL;

    return &preset_store[index];
}

void Presets_ResetPresetToDefaults(uint8_t index)
{
    uint8_t bank_index;
    uint8_t preset_index;

    Presets_EnsureRuntimeStore();

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
    if (active_preset == preset)
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
    if (Presets_CurrentBankUsesWetDry())
    {
        active_preset = &mute_preset;
        Presets_SendWetDryMuteLevels();
    }
    else
    {
        App_ActivatePresetData(&mute_preset, 0U, 0U);
    }

    LED_SetActiveButtonIndicator(10U);
}
