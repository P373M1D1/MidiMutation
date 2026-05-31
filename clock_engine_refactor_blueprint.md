# Clock Engine Refactor Blueprint

## Objective

Establish a single immutable clock domain with strict ISR isolation so MIDI clock timing remains deterministic under all load.

## Current Fragmentation (Code Spread Map)

### A) Clock generation and phase ownership are spread across multiple modules

- `Core/Src/midi/midi_clock.c`
  - TIM6 IRQ clock pulse generation and internal phase alignment helpers.
- `Core/Src/midi/midi_transport_sync.c`
  - External clock pulse handling, quarter-note edge work, estimator updates, and beat callbacks.
- `Core/Src/midi/midi_transport_events.c`
  - START/CONTINUE/STOP transport event transitions, phase handoff/reset behavior.
- `Core/Src/midi/midi_transport_observer.c`
  - External clock timeout/loss detection and recovery hint logic.
- `Core/Src/midi/midi_transport_state.c`
  - Sync lifecycle state machine and adaptive estimator control.

Impact: timing authority is conceptually split across five transport/clock files plus estimator coupling, which makes invariants hard to enforce.

### B) TIM2 ISR fanout owns multiple domains

- `User/Src/app/app_board_init.c`
  - `AppBoard_HandleTimingCounterIrq()` fans out into:
    - `MidiHandleTimingCounterIrq()`
    - `LED_HandleTimingCounterIrq()`
    - `AppMetronome_HandleTimingCounterIrq()`
- `Core/Src/led_functions.c`
  - TIM2 CC3 scheduling and edge handling.
- `User/Src/app/app_metronome.c`
  - TIM2 CC1 scheduling and edge handling.
- `Core/Src/midi/midi_output.c`
  - TIM2 CC4 scheduling/timebend output compare handling.

Impact: one IRQ path services unrelated timing consumers and allows non-clock concerns to expand ISR latency.

### C) Sync lifecycle update cadence is API-call-driven

- `Core/Src/midi/midi_transport_observer.c`
  - `MidiTransport_UpdateSyncState()`
- `Core/Src/midi/midi_transport_state.c`
  - many query APIs call `MidiTransport_UpdateSyncState()` before returning values.

Impact: sync maintenance cadence depends on query frequency, not a fixed service schedule.

### D) Queue and scheduler pressure can affect derivative behavior

- Central app queue and budget:
  - `User/Inc/app_event.h` (`APP_EVENT_QUEUE_CAPACITY = 32`)
  - `User/Src/app/app_dispatch.c` (`APP_DISPATCH_MAX_EVENTS_PER_CALL = 8`)
- MIDI queues:
  - `Core/Src/midi/midi_input.c` (`MIDI_REALTIME_QUEUE_SIZE = 64`)
  - `Core/Src/midi/midi_output.c` (`MIDI_OUTPUT_CLOCK_QUEUE_SIZE = 16`, `MIDI_OUTPUT_MESSAGE_QUEUE_SIZE = 128`)

Impact: non-clock event pressure can still delay or drop derivative consumer updates.

## Immediate Architectural Target

### Clock domain contract

Only `ClockEngine` may:
- own tick/phase/downbeat state
- process authoritative clock edges
- emit immutable snapshots

Only ISR entry points for timing should call a single clock update entry:
- TIM6 -> `ClockEngine_ISR_UpdateInternalPulse()`
- USART2 realtime clock/start/continue/stop -> `ClockEngine_ISR_OnExternalRealtime(byte, timestamp)`

TIM2 ISR should not execute UI/LED/metronome policy; it should only execute clock-owned timing compare work or raise minimal deferred flags.

## Proposed Module Structure

Create a new module pair:

- `Core/Inc/midi/clock_engine.h`
- `Core/Src/midi/clock_engine.c`

### Public API (first pass)

- `void ClockEngine_Init(uint16_t startup_bpm);`
- `void ClockEngine_ISR_OnInternalPulse(uint32_t now_us);`
- `void ClockEngine_ISR_OnExternalRealtime(uint8_t byte, uint32_t now_us);`
- `void ClockEngine_Service10ms(void);`  // non-ISR deferred work
- `uint8_t ClockEngine_GetSnapshot(clock_snapshot_t *out);`

Snapshot:

```c
typedef struct {
    uint32_t phase;
    uint32_t tick;
    uint8_t downbeat;
    uint8_t running;
    uint8_t source;   // internal/external
} clock_snapshot_t;
```

## Refactor Stages

### Stage 0: Guardrails (no behavior change)

1. Add compile-time guard macros in timing ISR files:
   - Ban direct calls to UI/render/save APIs from timing ISR code.
2. Add a simple ISR duration probe around TIM6 and TIM2 handlers (counter max only).
3. Keep existing behavior unchanged.

Files touched:
- `Core/Src/stm32f4xx_it.c`
- `User/Src/app/app_board_init.c`
- new small diagnostics helper if needed.

### Stage 1: Introduce ClockEngine wrappers (behavior-preserving)

1. Implement `clock_engine.c` as a thin wrapper that forwards to current transport/clock functions.
2. Replace direct calls in IRQ handlers with ClockEngine wrappers.

Example:
- TIM6 IRQ: `MidiClockOutputIrqHandler()` -> `ClockEngine_ISR_OnInternalPulse(TIM2->CNT)`
- USART2 realtime: `MidiTransport_HandleRealtimeByteFast(...)` path wrapped through `ClockEngine_ISR_OnExternalRealtime(...)`

Files touched:
- new: `Core/Src/midi/clock_engine.c`, `Core/Inc/midi/clock_engine.h`
- `Core/Src/stm32f4xx_it.c`
- `Core/Src/midi/midi_input.c`
- `Core/Src/midi/midi_clock.c` (only wiring)

### Stage 2: Move authoritative state into ClockEngine

1. Relocate tick/phase/downbeat authority from transport spread to ClockEngine-owned state.
2. Convert transport modules into consumers/adapters.
3. Expose snapshot reads for LED/UI/metronome.

Files incrementally migrated:
- from `midi_transport_sync.c`, `midi_transport_events.c`, `midi_transport_observer.c`
- into `clock_engine.c`

### Stage 3: Remove non-clock ISR fanout

1. Replace TIM2 fanout behavior with:
   - clock compare handling only in clock domain
   - deferred flags for LED/metronome service in foreground (or dedicated lower-priority IRQ/timer)
2. Keep LED/metronome as snapshot consumers.

Files touched:
- `User/Src/app/app_board_init.c`
- `Core/Src/led_functions.c`
- `User/Src/app/app_metronome.c`
- `Core/Src/midi/midi_output.c`

### Stage 4: Deterministic sync lifecycle cadence

1. Stop invoking sync update from read APIs.
2. Run lifecycle update in one fixed cadence service (`ClockEngine_Service10ms()`).

Files touched:
- `Core/Src/midi/midi_transport_state.c`
- `Core/Src/midi/midi_transport_observer.c`
- `User/Src/app/app_timer_events.c` (single service call)

### Stage 5: Priority hardening and load shedding

1. Revisit NVIC priority hierarchy so timing IRQs cannot be preempted by non-timing work.
2. Add pressure-based suppression for Tier 2 diagnostics/logging.
3. Keep save service aggressively deferred during external clock presence (already partly done).

Files touched:
- `Core/Inc/stm32f4xx_hal_conf.h`
- `User/Src/app/app_timer_events.c`
- `User/Src/app/app_save_service.c`

## High-Value First Changes (Recommended Execution Order)

1. Stage 1 wrappers (low risk, creates isolation seam).
2. Stage 3 TIM2 ISR fanout reduction (largest jitter win).
3. Stage 4 fixed sync lifecycle cadence (determinism improvement).
4. Stage 2 full authority migration (largest structural cleanup).
5. Stage 5 priority/load policy tuning.

## Acceptance Gates Per Stage

- Build passes.
- No regression in START/CONTINUE/STOP behavior.
- Stable quarter-note/downbeat under sustained encoder/UI/diagnostic load.
- ISR runtime max does not increase; target trend is down.
- No new queue drop regressions on realtime paths.

## Explicit Anti-Regression Checklist

- Clock generation does not read UI/menu/preset state.
- Timing ISR paths do not call display/LED/save/menu code.
- Event queue is never timing authority.
- Timebend does not mutate authoritative phase/tick.
- LED/UI consume snapshots only.
