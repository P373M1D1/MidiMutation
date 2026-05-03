#include "presets.h"
#include "button_functions.h"
#include "display_functions.h"
#include "midi_devices.h"
#include "midi_functions.h"
#include "bpm_functions.h"
#include "stm32f4xx_hal.h"
#include <string.h>

/* ── Bank names ─────────────────────────────────────────────────────────────
 * You can change these to any theme: "A/B", "Clean/Dirty", etc. */
#define PRESET_BANK_INVALID_NAME      "(bank?)" /* fallback name returned for an out-of-range bank index */
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
#define PRESET_RANDOM_LCG_SEED                0x6D2B79F5UL /* initial state for the random-preset pseudo-random generator */
#define PRESET_RANDOM_LCG_MULTIPLIER          1664525UL /* LCG multiplier used when generating random preset programs */
#define PRESET_RANDOM_LCG_INCREMENT           1013904223UL /* LCG increment used when generating random preset programs */

const char * const bank_names[PRESET_BANK_COUNT] = {
    "[Strain I]",
    "[Strain II]",
    "[Strain III]",
    "[Strain IV]"
};

const char *Presets_GetBankName(uint8_t bank)
{
    if (bank < PRESET_BANK_COUNT)
        return bank_names[bank];
    return PRESET_BANK_INVALID_NAME;
}

/* ── Application state owned by main.cpp ─────────────────────────────────── */
extern volatile uint16_t  g_bpm;
extern volatile uint32_t  bpm_save_tick;
extern const Preset_t    *active_preset;
extern uint8_t            active_preset_index;
volatile uint8_t          current_bank = 0U;

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
};

static Preset_t preset_store[PRESET_COUNT];
static uint8_t preset_store_initialized = 0U;

static void Presets_EnsureRuntimeStore(void)
{
    if (preset_store_initialized)
        return;

    memcpy(preset_store, preset_table, sizeof(preset_store));
    preset_store_initialized = 1U;
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

/* -------------------------------------------------------------------------- */

/* Shared activation path for normal presets, random preset, and mute preset.
 * update_index controls whether this activation should become the persisted
 * "current preset" or just temporarily repaint/run an overlay preset. */
static void App_ActivatePresetData(const Preset_t *preset, uint8_t update_index, uint8_t idx)
{
    if (!preset)
        return;

    if (Display_PresetEditIsActive() && !update_index)
        Display_PresetEditExit();

    Button_ResetSpecialFunctions();
    Display_ScreensaverDismiss();
    Display_MainInfoScrollReset();

    if (update_index)
        active_preset_index = idx;

    active_preset = preset;
    Midi_LoadPreset(active_preset);
    Display_DrawMainScreen(active_preset, g_bpm);
    Display_ScreensaverActivity();

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
}

void Presets_RedrawActiveDisplay(void)
{
    /* Special-functions state lives in button_functions.c; presets only need
     * to redraw the current screen so the right-side status text changes. */
    if (active_preset)
        Display_DrawMainScreen(active_preset, g_bpm);
}

void Presets_ActivateMute(void)
{
    App_ActivatePresetData(&mute_preset, 0U, 0U);
}
