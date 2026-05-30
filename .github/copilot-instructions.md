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
