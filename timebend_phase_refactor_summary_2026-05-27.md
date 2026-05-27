# Timebend Refactor Summary (2026-05-27)

## Why this file exists
This captures the key outcomes from the ENC2 timebend debugging/refactor chat so the work is easy to find later.

## Problem history
- Initial symptom set:
  - ENC2 CCW produced audible pitch/tempo bend.
  - ENC2 CW was weak or inactive.
  - Some scheduler revisions caused MIDI silence while timebend was active.
- Root cause trend:
  - Queue-based future due scheduling created asymmetry and timing fragility.

## Final architecture
The timebend output now uses a continuous phase-domain model instead of a predictive due queue.

Conceptually:
- phi_out(t) is advanced continuously from a target interval derived from truth interval plus warp offset.
- Clock bytes are emitted when phase crosses the next edge.
- A single pending TIM2 compare is used only to wake and continue crossing checks.

Practical effect:
- CW acceleration naturally increases crossing rate.
- CCW naturally spreads crossings.
- No speculative multi-event future queue.

## Key implementation outcomes
- Removed multi-future bent due queue scheduling logic.
- Introduced fixed-point phase accumulation (uint64 Q24 domain) for timing stability.
- Kept bounded warp behavior:
  - bounded phase offset
  - bounded target interval ratio
  - explicit max warp velocity clamp
- Decoupled timing progression from UART backpressure:
  - crossings still advance even when TX ring is full
  - missed emits are counted instead of stalling phase state

## Health hardening added
- Per-pass crossing cap to avoid long ISR loops under extreme backlog.
- Crossing backlog accounting (current and peak).
- Missed emit counter.
- UART realtime clock queue depth (current and peak).
- Phase monotonicity violation counter.

## Files updated
- Core/Src/midi/midi_output.c
- Core/Inc/midi/midi_output.h
- Core/Src/midi/midi_transport_state.c

## New/extended diagnostics surface
The CLKDIAG line now includes additional fields:
- tb_miss
- tb_cross_backlog_now
- tb_cross_backlog_peak
- tb_uart_q_now
- tb_uart_q_peak
- tb_phase_nonmono

## Recommended stress test matrix
- Aggressive CW spin for 30+ seconds.
- Aggressive CCW spin for 30+ seconds.
- Rapid CW/CCW alternation.
- Idle for 10+ minutes.
- Disconnect/reconnect MIDI cable.
- Repeated transport start/stop.
- Encoder jitter around center.
- Max bend while external sync/PLL is relocking.

Watch:
- emitted interval min/max/avg
- effective bend ratio
- crossing backlog
- UART queue depth
- missed emits
- phase monotonicity violations

## Build state at handoff
- CMake build succeeded after each major change set.
- Persistent linker note remains observed in this repo context:
  - section .bss allocation warning with successful link result code 0.

## Notes
If you want this converted into a shorter one-page runbook for live testing, create a second file from this summary with only: test steps, expected ranges, and fail conditions.
