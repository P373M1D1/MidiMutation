# Preset Editing Brainstorm

## Current constraints from the codebase

- `Preset_t` already contains the full editable payload: `name[21]`, `prg[8]`, `cc[8]`, and `relay[2]`.
- Real presets currently live in `static const preset_table[PRESET_BANK_COUNT][PRESETS_PER_BANK]` in `Core/Src/presets.c`, so the current runtime has no mutable preset store.
- `active_preset` is a `const Preset_t *`. Random and mute are runtime overlays, not normal editable presets.

Encoder behavior today:

- Encoder 1 turn scrolls the 3-row main-info viewport.
- Encoder 2 turn is sampled and queued, but currently unused.
- Encoder 3 turn changes BPM.
- All 3 encoder switch presses are detected, but they are only logged for test output right now.

Footswitch behavior today:

- Buttons 1-8 select presets in the current bank.
- Tap + mute step banks.
- Random, special, and mute already have dedicated behavior.
- Persistence today is only BPM, bank, and preset index. Flash writes have already caused timing concerns, so preset saving should be explicit, infrequent, and batched.

## Recommended direction

I would make preset editing a modal, non-performance UI.

- Enter edit mode explicitly.
- Work on a RAM copy.
- Save only on explicit confirmation.
- Keep live mode untouched and fast.

Recommended entry:

- Hold encoder 2 button for about 700 ms on the main screen to edit the current real preset.
- If the current screen is random or mute, show `Cannot edit overlay` and return.

Why this entry path makes sense:

- Encoder 2 is already wired but unused.
- A hold is safer than a short press during live use.
- It avoids overloading the footswitch logic that already handles presets, bank changes, mute, and random.
- It fits the footer language you already have: left = scroll, center = value/menu, right = tempo/exit.

## Mental model

Use a dedicated editor state machine, not small exceptions inside the existing main screen.

States:

1. Main screen
2. Edit overview
3. Name editor
4. Program Change editor
5. CC editor
6. Relay editor
7. Save / discard confirm
8. Flash write busy / done

## Control mapping

### Global edit rules

| Control | Meaning in edit mode |
|---|---|
| Encoder 1 turn | Move cursor or selected row |
| Encoder 1 press | Enter selected item or confirm current field |
| Encoder 2 turn | Fine value change |
| Encoder 2 press | Toggle enable/off, or run context action |
| Encoder 3 turn | Coarse change or secondary axis |
| Encoder 3 press | Back, or open save/discard prompt |

This gives the controls a stable meaning:

- left = where am I
- middle = change the thing
- right = scope or exit

## Screen flow

```text
Main screen
  -> hold ENC2
Edit overview
  -> Name editor
  -> Program editor
  -> CC editor
  -> Relay editor
  -> Save / Discard
  -> back to Main screen
```

## Edit overview screen

Suggested layout:

```text
Preset 03   [Strain I]
Soft Reverb *

> Name
  Programs
  CC messages
  Relays
  Save
  Discard
```

Suggestions:

- Show a dirty marker `*` once the working copy differs from the original.

Quick summary data could sit on the right or bottom:

- `PRG used: 3/8`
- `CC used: 1/8`
- `Relays: 1 0`
- Encoder 1 turn selects the row.
- Encoder 1 press enters the selected editor.
- Encoder 3 press immediately opens `Save / Discard / Cancel`.

## Name editor

This should be predictable, not clever.

### Recommended v1: character slots

- Display all 20 character positions, or a 10-character window into the 20-character buffer.
- Encoder 1 selects the character position.
- Encoder 2 changes the character.
- Encoder 3 changes the character-set page: uppercase, lowercase, numbers, symbols, or space.
- Encoder 2 press inserts a space or toggles the current slot to blank.
- Encoder 1 press moves to the next character slot.
- Encoder 3 press goes back to the overview.

Example concept:

```text
SOFT REVERB_________
    ^

Set: ABC
```

Why I would start here:

- It is slow, but it is very learnable.
- It does not require a full keyboard UI.
- It maps cleanly onto 3 encoders.

## Program Change editor

I would not force this onto the current performance screen layout. The display is large enough for a denser editor view.

Each row could look like this:

```text
DEV1  CH1   PRG 011
DEV2  CH2   PRG OFF
DEV3  CH3   PRG 000
```

Behavior:

- Encoder 1 selects the device row.
- Encoder 2 fine edits the program value.
- Valid values are `OFF` and `0..max_preset` for that device.
- Encoder 3 coarse edits by `+/-10`.
- Encoder 2 press toggles `OFF <-> last value`.
- Optional: Encoder 1 press auditions only that device's current program.

Notes:

- `MidiDevice_t` currently exposes channel and `max_preset`, but not a device display name. Even a short 4-8 character label would make this screen much clearer.
- Shared-program highlighting already exists on the main screen. The editor could reuse that to warn when a program number is reused in more than one preset.

## CC editor

A CC slot has four pieces of state: used/unused, channel, CC number, and value. I would treat it as a row editor with a field focus.

Each row could look like this:

```text
CC1   ON   CH 01   CC 065   VAL 127
CC2   OFF
CC3   ON   CH 12   CC 027   VAL 127
```

Behavior:

- Encoder 1 selects slot 1..8.
- Encoder 1 press cycles field focus between enabled, channel, CC number, and value.
- Encoder 2 fine edits the focused field.
- Encoder 3 coarse edits the focused field: channel by `+/-1`, CC number by `+/-10`, and value by `+/-10`.
- Encoder 2 press toggles slot used/unused.
- Optional: pressing Encoder 1 or Encoder 2 can transmit the focused CC as a preview.

Important detail:

- `unused` should map directly to the existing sentinel values: `channel = PRESET_CC_CHANNEL_UNUSED`, `cc_number = PRESET_CC_NUMBER_UNUSED`, and `value = PRESET_CC_VALUE_UNUSED`.

## Relay editor

This can stay very simple.

```text
Relay 1: Open
Relay 2: Closed
```

Behavior:

- Encoder 1 selects relay 1 or 2.
- Encoder 2 turn or press toggles open/closed.
- Encoder 3 press goes back.

## Save model

This is the important architecture piece.

### Recommended write path

- Keep the compiled presets as defaults.
- Copy them into a mutable RAM store at boot.
- Edit only a working copy while the user is in the editor.

On save:

- copy the working copy into the live RAM preset store
- if the edited preset is active, optionally re-send MIDI using the updated data
- write the persisted preset blob to flash once
- show a blocking `Saving...` screen until the write completes

### Why not save on every detent?

- Flash writes on this target can stall the main loop.
- You already added safeguards around persistence because of missed input concerns.
- Editing a name or CC value can generate many rapid changes, and autosave would be the worst possible write pattern.

### Storage size

With the current `Preset_t` shape, the whole preset set is small.

- about `32 presets * ~55 bytes` = about `1.8 KB` raw payload
- a small header for version, size, and CRC still fits comfortably in one flash sector

That suggests a simple persisted format:

- header: magic, version, size, CRC
- payload: flat array of `Preset_t`

## Code structure that would make this clean

### Data layer

Add a mutable preset store.

- Keep the current compiled table as defaults.
- Add `static Preset_t preset_store[PRESET_COUNT];`
- Boot flow: copy defaults into `preset_store`, then overlay from flash if valid.
- Change `Presets_Get()` to return entries from `preset_store`
- Add helpers like `Preset_t *Presets_GetMutable(uint8_t index);`, `bool Presets_SaveAll(void);`, and `void Presets_LoadDefaults(void);`.

### Editor session

Create a dedicated editor session struct.

- `uint8_t preset_index`
- `Preset_t original`
- `Preset_t working`
- `uint8_t dirty`
- cursor, focused field, and scroll state
- optional caches like `last_program_value[8]` to support `OFF <-> last value`

### Input routing

Right now the encoder pending handlers are hardwired:

- encoder 1 -> scroll main info
- encoder 2 -> currently unused
- encoder 3 -> tempo

I would keep the TIM7 sampling exactly as it is and route only the foreground handlers by UI mode.

```c
switch (ui_mode) {
case UI_MODE_MAIN:
    ...
    break;
case UI_MODE_PRESET_EDIT_OVERVIEW:
case UI_MODE_PRESET_EDIT_NAME:
case UI_MODE_PRESET_EDIT_PROGRAM:
case UI_MODE_PRESET_EDIT_CC:
case UI_MODE_PRESET_EDIT_RELAY:
    ...
    break;
}
```

That avoids touching the low-level encoder sampling path more than necessary.

### Display

Add dedicated editor drawing functions instead of stretching `Display_DrawMainScreen()`.

Possible split:

- `Display_DrawPresetEditOverview(...)`
- `Display_DrawPresetNameEditor(...)`
- `Display_DrawPresetProgramEditor(...)`
- `Display_DrawPresetCcEditor(...)`
- `Display_DrawPresetRelayEditor(...)`
- `Display_DrawPresetSaveDialog(...)`

### Do not overload the existing special-function mode bit

`special_functions_active` already means something specific in `button_functions.c`. I would keep edit mode separate from that and give it its own UI state.

## Likely code touch points

- `Core/Src/presets.c` for mutable preset storage, load/save, and active-preset access
- `Core/Inc/presets.h` for editor-facing APIs
- `Core/Src/main.cpp` for encoder button hold detection and UI mode routing
- `Core/Src/display_functions.c` and `Core/Inc/display_functions.h` for the edit screens
- a new persistence helper if you want preset flash storage separate from BPM flash storage

## Live-behavior rules worth deciding early

- Editing should target only real presets, not random or mute overlays.
- Tempo encoder should stop changing BPM while edit mode is active.
- Normal preset footswitches should probably be ignored while editing, unless you explicitly want them to become preset-slot shortcuts.
- Mute should probably remain an emergency escape: either abort edit and enter mute, or be ignored only while the flash write is in progress.
- If save fails, do not lose the RAM working copy. Show retry or discard.

## Best v1 scope

If the goal is a first useful version without turning the firmware inside out, I would do this in phases.

### Phase 1

- Enter and exit edit mode
- Edit preset name
- Edit program changes only
- RAM working copy only
- No flash persistence yet

### Phase 2

- Add CC slot editing
- Add relay editing
- Add save/discard dialog
- Persist the full preset blob to flash

### Phase 3

- Add per-row MIDI preview
- Add editing a non-active preset
- Add device labels
- Add copy/paste preset or copy-bank-slot tools

## Open questions

- Should editing always target the currently active preset, or should you be able to choose a different preset slot first?
- Should saving immediately re-activate the edited preset and re-send its MIDI?
- Is a full 20-character onboard name editor worth it, or would a shorter visible limit feel better in practice?
- Should CC preview transmit live while you turn the value, or only when you confirm?
- Do you want the first version to be performance-safe first, or feature-rich first?

## My recommendation

If the goal is `usable on the pedalboard without becoming fragile`, I would build this path:

1. Mutable RAM preset store loaded from defaults plus flash
2. Modal editor entered with encoder 2 long press
3. Dedicated edit overview screen
4. Name editor
5. Program editor
6. Explicit save/discard
7. CC editor after that foundation is solid

That gets you to a reliable on-device editor without fighting the current live screen and without turning flash writes into a hidden real-time problem.
