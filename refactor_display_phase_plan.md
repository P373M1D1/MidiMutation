# MidiMutation Refactor Phase Plan

## Purpose

Reduce architectural risk and improve maintainability by replacing monolithic modules (especially `display_functions.c`) with coherent subsystem boundaries while preserving current firmware behavior.

## Rules Of Execution

1. One phase per PR/branch.
2. Every phase is behavior-preserving unless explicitly marked as behavior change.
3. Keep existing public APIs as wrappers until the final cleanup phase.
4. Build `ChatTest` at phase end and run the same smoke checklist every time.
5. No unrelated feature work inside refactor phases.

## Baseline Smoke Checklist (Run Every Phase)

1. Boot completes and UI appears without white-flash regressions.
2. Live screen draws preset name, bank name, footer labels, BPM area.
3. Preset switching updates MIDI output and active preset LED.
4. Preset edit mode enter/exit works and save popup appears.
5. Menu enter, navigate, activate, back, home all work.
6. Encoder actions still match mode-dependent behavior.
7. Build target `ChatTest` succeeds with no new warnings introduced by the phase.

---

## Phase 0 - Inventory And Freeze

### Goal
Create a factual baseline so all later refactors can be verified quickly.

### Scope
1. Document current display subsystem responsibilities.
2. Add a short architecture map of "who calls what" among main, presets, button, display, runtime config.
3. Capture known module boundaries and known boundary violations.

### Deliverables
1. `docs/architecture/display-current-map.md` (or equivalent path if docs folder is not used).
2. A one-page call-flow summary for Live, Menu, and Preset Edit.

### Exit Criteria
1. Team agrees baseline map reflects actual code.
2. No code behavior changes made.

---

## Phase 1 - Boundary Definition And API Grouping

### Goal
Make ownership explicit before moving logic.

### Scope
1. Keep `Core/Inc/display_functions.h` as stable public facade only.
2. Introduce internal headers for display-only internals.
3. Group public API into clear sections:
   - backlight
   - main screen rendering
   - preset edit
   - menu control

### Deliverables
1. Public header contains only externally used functions/types.
2. New private/internal header(s) in display subsystem path.
3. Comment block in header describing ownership and non-goals.

### Exit Criteria
1. No caller outside display subsystem includes internal headers.
2. Build unchanged, smoke checklist passes.

---

## Phase 2 - Extract Backlight

### Goal
Move the least coupled logic out of `display_functions.c` first.

### Scope
1. Move DAC backlight init/fade logic into dedicated module.
2. Preserve existing external function names via wrappers if needed.

### Suggested Files
1. `Core/Src/display/display_backlight.c`
2. `Core/Inc/display/display_backlight_internal.h`

### Exit Criteria
1. No functional differences in fade timing/wake behavior.
2. `display_functions.c` line count reduced and responsibilities narrowed.

---

## Phase 3 - Consolidate Display State

### Goal
Replace many scattered `static` globals with a single coherent state model.

### Scope
1. Introduce `DisplayState` struct for UI state.
2. Move menu/preset-edit/main-screen state members into this struct.
3. Add explicit init/reset routine(s) for deterministic startup and mode transitions.

### Suggested Files
1. `Core/Src/display/display_state.c`
2. `Core/Inc/display/display_state_internal.h`

### Exit Criteria
1. Display state is inspectable in one place.
2. State transitions are centralized and easier to reason about.

---

## Phase 4 - Extract Layout, Theme, And Copy

### Goal
Separate static UI configuration from render/control logic.

### Scope
1. Move layout constants (positions, dimensions) into layout module.
2. Move colors/font aliases into theme module.
3. Move user-visible static labels into one place.

### Suggested Files
1. `Core/Src/display/display_layout.c`
2. `Core/Inc/display/display_layout_internal.h`
3. `Core/Src/display/display_theme.c`
4. `Core/Inc/display/display_theme_internal.h`
5. `Core/Src/display/display_strings.c`
6. `Core/Inc/display/display_strings_internal.h`

### Exit Criteria
1. Render functions consume config from modules rather than local macros.
2. UI copy/layout changes require minimal code edits.

---

## Phase 5 - Split Menu By Page And Controller

### Goal
Decompose large menu logic into page-specific units with one controller.

### Scope
1. Keep one menu controller dispatching by `DisplayMenuPage_t`.
2. Move page render/update logic into dedicated modules.
3. Extract text-edit helpers and row-compose helpers from monolith.

### Suggested Files
1. `Core/Src/display/menu/display_menu_controller.c`
2. `Core/Src/display/menu/display_menu_root.c`
3. `Core/Src/display/menu/display_menu_banks.c`
4. `Core/Src/display/menu/display_menu_devices.c`
5. `Core/Src/display/menu/display_menu_function_button.c`
6. `Core/Src/display/menu/display_menu_global.c`
7. `Core/Src/display/menu/display_menu_textedit.c`

### Exit Criteria
1. Each page can be changed without touching unrelated page code.
2. Menu selection/activation/back behavior remains unchanged.

---

## Phase 6 - Split Main Screen Rendering Pipeline

### Goal
Separate content formatting from drawing operations.

### Scope
1. Extract main header/preset/bank/info/footer drawing into submodules.
2. Add formatter helpers that produce row content independent of drawing backend.
3. Keep draw functions focused on composition/blit only.

### Suggested Files
1. `Core/Src/display/main/display_main_render.c`
2. `Core/Src/display/main/display_main_format.c`
3. `Core/Src/display/main/display_main_footer.c`
4. `Core/Src/display/main/display_main_bpm.c`

### Exit Criteria
1. Main screen code path is readable end-to-end.
2. String/content tweaks do not require changing low-level draw code.

---

## Phase 7 - Preset Edit Subsystem Cleanup

### Goal
Isolate preset-edit cursor, field mapping, and render/update behavior.

### Scope
1. Move preset-edit field mapping/state transitions to dedicated module.
2. Move preset-name editor logic to dedicated module.
3. Keep public calls intact for main dispatcher.

### Suggested Files
1. `Core/Src/display/edit/display_preset_edit_controller.c`
2. `Core/Src/display/edit/display_preset_name_editor.c`
3. `Core/Src/display/edit/display_preset_edit_render.c`

### Exit Criteria
1. Preset-edit behavior remains identical.
2. Cursor and field logic no longer mixed with menu/main rendering.

---

## Phase 8 - Final API Tightening And Documentation

### Goal
Conclude with a minimal, coherent, documented API surface.

### Scope
1. Remove temporary wrappers no longer needed.
2. Ensure comments/docstrings match actual behavior.
3. Add subsystem README with ownership and extension rules.

### Deliverables
1. `Core/Inc/display_functions.h` slim stable API.
2. `Core/Src/display/README.md` with architecture and coding rules.

### Exit Criteria
1. No stale/misleading comments remain.
2. New contributor can find where to add menu/main changes in under 5 minutes.

---

## CMake And Include Hygiene Checklist (Apply In Every Extraction Phase)

1. New source files added to the firmware target in CMake.
2. Public includes remain in `Core/Inc` (or approved include roots).
3. Internal headers are not pulled into non-display modules.
4. Include order standardized: module header, local headers, external headers, C stdlib.
5. No circular include dependencies introduced.

---

## Risk Register

1. Hidden coupling through static globals.
   - Mitigation: move one cluster at a time; keep wrappers.
2. Rendering regressions from row composition side effects.
   - Mitigation: same smoke script + visual check after each phase.
3. Input-mode regressions (menu/edit/live behavior drift).
   - Mitigation: explicit mode transition checklist in smoke tests.
4. Merge conflicts during long-running refactor.
   - Mitigation: phase-by-phase short branches and frequent integration.

---

## Suggested Immediate Next Action

Start Phase 1 with a no-behavior-change PR that does only:

1. Public header regrouping and cleanup.
2. Internal header introduction.
3. Minimal file scaffolding for phase 2 modules.

If this PR builds and passes smoke checks, proceed directly to Phase 2 extraction.
