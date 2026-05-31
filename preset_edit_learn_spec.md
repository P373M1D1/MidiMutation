# Preset Edit LEARN Spec

## Goal
Contain Preset Edit MIDI learn behavior to one owner module with deterministic start/stop rules and minimal cross-layer coupling.

## Ownership
- Learn state owner: User/Src/app/app_ui.c
- Learn state API: User/Inc/app/app_ui.h
- Learn state must not be mutated outside app_ui.c.

## Learn Session Model
- LEARN indicator in Preset Edit footer is always visible as a static label.
- LEARNING popup is transient and visible only while a learn session is active.
- Session starts when ENC2 is pressed on a learn-capable Preset Edit field.
- Session stops on:
  - successful capture for Program or CC Number field
  - 3s idle timeout for CC Value field
  - ENC2 pressed again while learning
  - leaving Preset Edit mode

## Field Rules
- Program field:
  - Accept first incoming Program Change after session start.
  - Write preset program value for selected slot.
  - End session immediately.
- CC Number field:
  - Accept first incoming Control Change after session start.
  - Write CC number for selected slot.
  - End session immediately.
- CC Value field:
  - Accept incoming Control Change values continuously.
  - Write value for selected slot on each new matching MIDI monitor revision.
  - End session after 3000 ms with no new CC update.

## MIDI Consumption Rule
- app_ui.c is the only module that consumes MIDI monitor data for Preset Edit learning.
- Service function: AppUi_PresetEditLearningService().
- Service uses MidiMonitor revision tracking so only newly received entries are processed.
- Stale monitor history must not trigger new captures after learn starts.

## Event Integration
- Runtime event layer only calls AppUi_PresetEditLearningService() and does not implement learn logic.
- Timeout and MIDI capture decisions both live in app_ui.c.

## Optional Behavior
- In Preset Edit mode, pressing the currently active preset footswitch re-transmits that preset.
- ENC2 in Preset Edit is reserved for LEARN toggle and does not send preset.

## Timing Constraints
- No ISR transport/clock code changes for LEARN behavior.
- Learn processing runs in foreground event/service context only.
- Timing correctness of MIDI clock and transport remains higher priority than UI behavior.
