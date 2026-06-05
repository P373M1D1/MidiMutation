# External Clock Lock Ownership Spec

## 1. Purpose

Define single-owner timing authority for external MIDI clock synchronization.

This spec prevents legacy behaviors where timer restarts were used as synchronization, and it enforces bounded, phase-continuous correction under lock.

## 2. Scope

This spec governs:

- External MIDI clock ingest and lock behavior.
- Internal clock phase and frequency correction policy.
- Outgoing MIDI clock pulse generation authority.
- Holdover and sync-loss transitions.
- Observer responsibilities for UI, LED, and metronome subsystems.

This spec does not define UI layout or menu behavior.

## 3. Normative Terms

- MUST: required, no exceptions.
- MUST NOT: forbidden.
- SHOULD: recommended unless justified otherwise.
- MAY: optional.

## 4. System Authority Model

### 4.1 Tempo Authority

- Tempo authority defines nominal BPM.
- In internal mode, ClockEngine owns tempo authority.
- In external mode, estimator-recovered tempo is the only tempo authority.
- UI or app state may mirror tempo for display/persistence, but mirrors MUST NOT drive clock timing decisions.

### 4.2 Phase Authority

- Phase authority defines current musical phase position.
- TIM6 phase accumulator is the only phase authority in generated clock output.
- External systems MAY submit correction deltas.
- External systems MUST NOT directly overwrite phase state during steady lock.
- In LOCKED mode, TIM6 CNT MUST NOT be written except via ClockEngine-owned ACQUIRE or HOLDOVER transition entry points.

### 4.3 Frequency Authority

- Frequency authority defines phase advance rate.
- Rate changes MUST pass through bounded correction logic.
- Direct per-pulse timer restart style updates are forbidden in steady lock.
- In LOCKED mode, correction MUST be frequency-only.

### 4.4 F8 Generation Authority

- Outgoing F8 generation is derived from internal phase progression only.
- External RX timestamps MUST NOT directly schedule outgoing F8 timing.

### 4.5 Display, LED, and Metronome Authority

- UI, LED, metronome are observer-only consumers.
- They MAY consume lock state, recovered BPM, and phase snapshots.
- They MUST NOT feed back into timing authority.

### 4.6 TIM6 Register Write Authority

- Only ClockEngine MAY write TIM6 registers (including CNT and ARR).
- Estimator, Transport Sync, MIDI Input, and all observer subsystems MAY only submit correction requests or state requests.
- Any non-ClockEngine TIM6 register write is a contract violation.

## 5. Lock Modes

### 5.1 FREE_RUN

- Internal tempo and phase fully authoritative.
- No external correction applied.

### 5.2 ACQUIRE

- External pulses are timestamped and estimator is updated.
- Bounded coarse alignment MAY be applied.
- Repeated timer restart corrections are forbidden.

### 5.3 LOCKED

- External estimator drives bounded phase/frequency correction through ClockEngine only.
- There MUST be exactly one active correction path in LOCKED mode.
- Timer resets and per-pulse timer restarts are forbidden.
- Correction smoothness and limits in Section 7 are mandatory.

### 5.4 HOLDOVER

- External signal lost.
- Last stable rate estimate continues internal progression.
- Confidence decays; transition policy in Section 8 applies.

### 5.5 Suggested Mapping To Existing ClockState

- FREE_RUN -> CLOCK_STATE_OFF
- ACQUIRE -> CLOCK_STATE_SYNCING
- LOCKED -> CLOCK_STATE_LOCKED
- HOLDOVER -> CLOCK_STATE_HOLDOVER or CLOCK_STATE_LOST (implementation-specific distinction)

## 6. External Pulse Processing Contract

Each valid external pulse MUST execute only:

1. Timestamp capture.
2. Estimator update.
3. Phase/frequency error computation.
4. Publish bounded correction request.

External pulse handling MUST NOT:

- Directly write TIM6 CNT in steady lock.
- Directly rewrite TIM6 ARR every pulse.
- Invoke UI rendering or LED policy work.
- Become a secondary authority for musical phase progression.

Correction request handling rules:

- Correction requests MUST be coalesced; only the latest sample per control cycle is applied.
- Correction requests MUST NOT be accumulated across cycles.

## 7. Correction Model

### 7.1 Forbidden Legacy Behaviors In LOCKED

- Hard timer restart behavior per pulse.
- Phase discontinuity by repeated counter resets.
- Unbounded rate jumps from raw interval replacement.
- Phase register writes during steady LOCKED operation.

### 7.2 Allowed Correction Types

- LOCKED mode: bounded frequency correction delta only.
- ACQUIRE or HOLDOVER transitions: bounded phase correction MAY be applied through a single ClockEngine transition path.
- Slew-limited convergence from estimator output to output generator.

### 7.3 Mandatory Safety Limits

Implementation MUST define these constants and enforce them:

- MAX_PHASE_CORRECTION_US_PER_PULSE
- MAX_FREQ_CORRECTION_PPM_PER_SEC
- MAX_ACCEPTED_ESTIMATOR_STEP_US
- LOCK_ENTRY_MIN_STABLE_PULSES
- LOCK_EXIT_MAX_PHASE_ERROR_US

If any limit is exceeded, controller MUST degrade to ACQUIRE or HOLDOVER based on current signal presence and confidence.

### 7.4 Smoothness Requirement

Steady-state lock MUST satisfy:

- No timer restart artifacts.
- No phase jumps beyond MAX_PHASE_CORRECTION_US_PER_PULSE.
- No rate jumps beyond MAX_FREQ_CORRECTION_PPM_PER_SEC.

## 8. Transition Rules

### 8.1 Enter LOCKED

- Require minimum stable interval window.
- Require phase error below threshold.
- Require estimator publication readiness.

### 8.2 Exit LOCKED

- Exit on sustained phase error above threshold, or confidence collapse, or signal timeout.
- Transition target is ACQUIRE if signal present, otherwise HOLDOVER.

### 8.3 HOLDOVER Behavior

- Freeze to last stable rate estimate.
- Keep phase monotonic.
- Decay confidence over configured window.

### 8.4 Re-lock Behavior

- Re-acquire gradually using bounded corrections.
- No abrupt timer state changes at re-lock boundary.

## 9. Ownership Matrix

| Subsystem | Owns | May Influence | Must Never Touch |
|---|---|---|---|
| ClockEngine | TIM6 phase base, output timing authority, TIM6 register access | correction accept/reject policy | external RX as direct output scheduler |
| Estimator | phase/frequency error model | bounded correction proposals | hardware timer registers |
| Transport Sync | mode transitions | correction request gating | direct timer register rewrites |
| MIDI Input | pulse ingest timestamps | estimator input stream | clock authority state machine decisions |
| Output/LED/UI/Metronome | derived presentation timing | none | clock authority, lock controller internals |

## 10. ISR and Scheduling Constraints

- Clock-critical ISR paths MUST be deterministic and minimal.
- Clock ISR paths MUST NOT include UI or non-essential fanout work.
- Any non-critical diagnostics SHOULD be deferred.

## 11. Acceptance Criteria

A candidate implementation passes only if:

1. Under stable external clock, each valid pulse maps to exactly one transport tick.
2. Bar/beat progression is continuous and monotonic.
3. LOCKED mode shows no timer restart artifacts.
4. Holdover preserves phase continuity without abrupt steps.
5. Re-lock converges without discontinuous timer rewrites.
6. UI load, LED activity, and save operations do not alter clock stability.

## 12. Test and Telemetry Requirements

Minimum telemetry for validation:

- lock_state
- phase_error_us
- phase_correction_us
- frequency_correction_ppm
- lock_entry_count
- lock_loss_count
- holdover_entry_count
- relock_latency_ms
- dropped_realtime_events

Minimum test scenarios:

1. Stable external master for 10+ minutes.
2. External jitter injection with bounded amplitude.
3. Cable unplug/replug with holdover and re-lock.
4. Heavy UI/encoder activity under lock.
5. Timebend active while externally locked.

## 13. Migration Guidance From Legacy Retiming

1. Keep monitor-only behavior enabled until bounded correction controller exists.
2. Introduce correction layer behind explicit feature flag.
3. Validate Section 11 acceptance criteria before any default-mode change.
4. Remove legacy restart-style retiming path after replacement passes tests.

## 14. Stabilization Execution Plan (3 Phases)

### Phase 1: Timing Path Stabilization (start immediately)

Goal: reduce timing jitter risk without changing lock-policy semantics.

Scope:

- Remove anchor-based LED pulse width distortion by deriving OFF edge from the actual ON edge.
- Bound per-IRQ timebend crossing work so TIM2/TIM6 paths stay deterministic under load.
- Remove variable-length crossing loops from IRQ-adjacent execution paths.

Status:

- Complete.
- LED edge-based pulse timing change is implemented.
- Timebend crossing emission uses explicit per-call budgets (IRQ and foreground).

Acceptance checks:

- No regression in beat LED visibility and pulse width consistency.
- No unbounded IRQ loop behavior remains in CC4 handling.
- Build passes and timing-path review confirms IRQ boundedness.

### Phase 2: Backlog-Control Hardening

Goal: eliminate backlog as a timing influence under sustained load.

Scope:

- Treat remaining timing risk as scheduling pressure, not PLL drift.
- Add lightweight backlog visibility that survives diagnostic guard conditions.
- Define hard per-subsystem execution budgets and verify observed compliance.
- Keep LED and metronome timing independent of CC4 congestion outcomes.
- Apply visual-layer beat scheduling smoothing and quantization so tiny upstream variance does not appear as LED breathing.

CC4 semantic policy (explicit, frozen for production):

- Selected policy: latest-state drop (strict real-time).
- Behavior: when crossing budget is hit, intermediate pending crossings are discarded and only the latest state is retained.
- Consequence: deterministic bounded execution and bounded backlog influence under overload.
- Tradeoff: intermediate musical continuity is intentionally sacrificed to preserve timing feel.

Acceptance checks:

- Backlog enter/update/exit states are observable from runtime telemetry.
- Output scheduling jitter estimate is observable from runtime telemetry.
- Backlog growth and decay are measurable during stress scenarios.
- No hidden semantic hybrid remains in CC4 crossing handling.

### Phase 3: Verification and Production Gate

Goal: prove stability contract under realistic and stressed operation before default rollout.

Scope:

- Execute stable-lock, jitter-injection, unplug/replug, heavy UI load, and timebend-under-lock tests.
- Capture telemetry required by Section 12 and compare against Section 11 criteria.
- Gate any default-mode behavior change on passing acceptance criteria.

Acceptance checks:

- All Section 11 criteria pass across required scenarios.
- No timer restart artifacts in LOCKED.
- Holdover/re-lock behavior remains phase-continuous and musically usable.