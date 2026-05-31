# Copilot Instructions (Repo)

This repository is a real-time MIDI firmware project.

## Priority Rules

1. MIDI clock and transport timing have absolute priority over all other subsystems.
2. Do not introduce jitter, latency, or non-determinism into clock/transport paths.
3. Clock and transport code must remain predictable, minimal, and ISR-safe.
4. If a tradeoff is required, preserve timing correctness before UI or feature behavior.
5. Comment either in line using // or above the relevant code block using /** */ style. Avoid trailing comments on the same line as code. I need to be able to know what functions and code blocks are doing without having to read the code itself, so comments should be descriptive and explain the intent of the code, not just what it does. For example, instead of saying "This function calculates the tempo", say "This function calculates the tempo based on the incoming MIDI clock pulses and updates the transport state accordingly". And if I ever make chagnes, what ios affected and what should I be looking out for, such as timing issues, sync loss, etc.
---

## Core Timing Architecture Rules

1. There is exactly one source of truth for musical time progression (clock → tick → beat → bar).
   - Only this path may advance bar/beat/transport phase.
   - All other modules may only observe or request changes via events.

2. MIDI clock pulse handling must be deterministic:
   - Each valid MIDI clock pulse maps to exactly one transport tick in LOCKED state.
   - Bar/beat progression must be monotonic and gap-free under stable sync.
   - No silent dropping of valid clock pulses.

3. Clock truth vs UI state separation:
   - MIDI clock, transport state, and tempo estimation are authoritative.
   - UI state is strictly derived and must never influence timing decisions.

---

## ISR and High-Frequency Path Rules

1. ISR code must remain minimal and deterministic:
   - Capture timestamps and raw inputs only.
   - Push minimal events or update lock-free counters.
   - No blocking operations, allocations, or heavy logic.

2. ISR must NOT:
   - Mutate transport state (except atomic counters if required)
   - Trigger UI updates or LED updates
   - Perform queue-heavy fanout or indirect service calls

3. All non-critical processing must be deferred out of IRQ context.

---

## Event System Rules

1. No hidden polling in event-driven logic:
   - State changes must come from explicit events or ISR-to-event bridges only.
   - Periodic comparison loops (e.g. UI tick detecting state changes) are not allowed for core state.

2. Event fanout must be controlled:
   - Avoid one input generating unbounded downstream work.
   - Prefer coalesced state-change events over repeated signals.

3. Event handling must not block or delay clock processing paths.

---

## Transport / Sync Rules

1. External clock recovery must preserve continuity:
   - System may re-enter SYNCING from incoming clock without external START.
   - Bar continuity must be preserved unless explicitly reset by STOP.

2. Sync loss handling must not permanently block re-acquisition:
   - Returned clock must re-enable transport progression.

3. Sync estimation must not delay transport state transitions beyond musical usefulness.

---

## Timing Invariants

1. Under stable external clock:
   - Every MIDI clock pulse produces exactly one transport tick.
   - Bar/beat progression is continuous and monotonic.
   - No accumulation of unprocessed clock events in LOCKED state.

2. Timing correctness always takes precedence over visual or UI responsiveness.

---

## Timer / Multi-Domain Rules

1. Only one timer domain may define musical time progression.
   - Secondary timers (e.g. output re-synthesis, UI ticks) must not affect transport truth.

2. TIM2/TIM6 or equivalent multi-timer setups must not create competing time domains.
   - One clock domain = authoritative musical timing source.
   - Others = derived or auxiliary only.

---

## Debugging / Change Discipline

1. Any change to clock, transport, or tempo logic must preserve:
   - Pulse count visibility
   - Transport tick consistency
   - Bar/beat increment correctness
   - Sync state transitions

2. After edits, explicitly evaluate:
   - Lock-in time impact
   - Sync-loss recovery behavior
   - Bar/beat continuity

3. Prefer simple, testable logic over complex heuristics.

4. Build after changes and report:
   - compile status
   - timing-path risk assessment

---

## Absolute Clock Stability Contract

PRIMARY GOAL: ABSOLUTE MIDI CLOCK STABILITY

The internal MIDI clock must:
- have minimal jitter
- maintain deterministic phase progression
- keep stable downbeat alignment under all system load conditions
- never be influenced by UI, LEDs, presets, or event load

Clock stability takes priority over:
- UI responsiveness
- LED updates
- preset switching
- Timebend effects
- save operations
- debug logging

### System Priority Model (Strict)

Tier 0 (hard real-time, never delay or block):
- MIDI clock generation
- phase accumulator updates
- TIM2/TIM6 ISR timing logic

Tier 0 constraints:
- Must execute in constant time
- Must not depend on application/UI/menu state
- Must not call application logic
- Must not access UI, LED, preset, or save systems

Tier 1 (soft real-time, 5-10 ms tolerance):
- preset switching
- Timebend activation/deactivation
- LED updates
- UI updates
- menu navigation

Tier 1 may be delayed slightly but must never affect clock timing.

Tier 2 (non-critical):
- flash saving
- diagnostics logging
- debug output
- UI animations

Tier 2 must be deferred under load.

### Clock Engine Architecture Rule (Absolute)

There is exactly one timing authority: Clock Engine.

Only Clock Engine may:
- update timebase
- maintain phase accumulator
- compute MIDI tick timing
- generate downbeat flags

ISR contract:
- TIM2/TIM6 ISR must only call `ClockEngine_ISR_Update()` (or equivalent single timing-core entry point)
- No other logic is allowed in clock ISR context

### Clock Output Snapshot Model

All non-clock subsystems must consume a derived clock snapshot, for example:

`typedef struct { uint32_t phase; uint32_t tick; bool downbeat; } clock_snapshot_t;`

UI, LED, MIDI output formatting, and Timebend must be derived from snapshots and must never feed back into timing generation.

### Forbidden Behavior

Do not:
- update LEDs inside clock ISR paths
- update UI inside clock ISR paths
- run Timebend logic inside clock ISR paths
- modify preset state inside clock logic
- use event queues to drive clock timing
- block ISR for anything except timing-core update

Do not allow UI/menu/preset state to influence clock generation.

### LED/UI Rule

LEDs and UI are purely derived systems.

They must:
- reflect clock snapshots and runtime state
- never influence timing

Downbeat LED rule:
- must follow clock phase only
- must not interrupt or delay timing events

### Timebend Rule

Timebend is an overlay system:
- does not replace preset state
- does not influence clock generation
- does not alter phase timing

Timebend only modifies interpretation/output after clock generation.

### Event System Rule

Event queues:
- may drop non-critical events under load
- must never be used for clock timing
- must not influence ISR timing behavior

Clock path must bypass all event queues.

### Acceptance Criteria

- MIDI clock remains stable under maximum system load
- LED and bar counter follow downbeat correctly
- UI latency does not affect timing
- Timebend does not influence clock generation
- ISR execution remains minimal and deterministic
