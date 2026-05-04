# Firmware Refactor Plan (Final)

This is the canonical implementation plan for the `robot_v3/` firmware rewrite.
It consolidates and supersedes the implementation direction from:

- `docs/firmware/REFACTOR_PLAN.md`
- `docs/firmware/EMOTION_SYSTEM.md`
- `robot_v3/REFACTOR_PLAN.md`

The purpose is to expand the firmware possibility space by cleanly separating:

- event ingestion and mapping,
- emotion/verb behavior logic,
- face rendering and motor movement execution.

The bridge/server remains agent-agnostic and presentation-agnostic. Firmware owns interpretation.

## Final Architectural Intent

- The bridge emits generic events (`agent_event`) and optional direct affective controls.
- Firmware is the sole place that maps incoming events to behavior state changes.
- `EmotionSystem` is the abstraction boundary between upstream event sources and downstream expression/motion output.
- Face and motion are consumers of effective expression/context, not of protocol internals.

In short: event source does not define robot reaction; firmware does.

## Principles (Locked)

- **Greenfield rewrite in `robot_v3/`** (do not refactor `robot_v2` in place).
- **Agent-agnostic firmware naming** (no module branding to a specific agent).
- **Tick-based runtime model** (`begin()` + `tick()`; no RTOS/scheduler introduction).
- **Compile-time wiring** in `app/` composition root.
- **No upward calls** between layers.
- **Minimal handlers** for incoming events: handlers mutate behavior state and return.
- **Last applied write wins** for behavior mutations (deterministic ordering by arrival/dispatch order).
- **No strict v2 parity requirement**; preserve broad behavior vibe from `EXPRESSION_PLANS.md`.

## Canonical Module Layout

```text
robot_v3/
  core/
  hal/
  bridge/
  agents/
  behaviour/
    EmotionSystem/
    VerbSystem/
    MotionBehaviors/
  face/
    SceneTypes/
    FrameController/
    ...renderers
  app/
    EventRouter/
    robot_v3.ino
```

`app/` is the only layer aware of all modules and all cross-layer wiring.

## Input Surfaces and Ownership

Firmware accepts two classes of input:

1. **Semantic agent lifecycle events**
   - Examples: `session.started`, `turn.started`, `activity.started`, `turn.ended`.
   - Routed through `EventRouter` to minimal behavior mutators.

2. **Raw emotion/behavior control events**
   - Primary fixed command set (see below).
   - Optional extensible command payload (experimental/forward-compatible path).

Both surfaces write to the same behavior systems. There is no special privilege model by source.

## Event Precedence and Ordering (Locked)

- All behavior changes are applied in event dispatch order.
- If two events mutate the same target in close succession, the later mutation defines current state.
- This applies equally to semantic mappings and raw control commands.
- Overlays and timers still obey their own internal lifecycle once started, but triggering is ordered by last write.

Example:

- `turn.started` maps to `setVerb(THINKING)`.
- Immediately after, raw `startVerb(READING)` arrives.
- Effective verb becomes `READING` (latest write).

## Behavior Model

### VerbSystem (discrete, activity-like)

Continuous verbs:

- `THINKING`, `READING`, `WRITING`, `EXECUTING`, `STRAINING`, `SLEEPING`, `NONE`

One-shot overlays:

- `WAKING`, `ATTRACTING_ATTENTION`

Responsibilities:

- set/clear current verb,
- manage linger windows,
- manage overlay playback lifecycle and restoration.

### EmotionSystem (continuous, mood-like)

State:

- `valence` in `[-1, +1]`
- `activation` in `[0, +1]`

Supports:

- impulses (`modifyValence`, `modifyArousal`, combined impulse),
- held targets (drivers like pending permission / prolonged strain),
- decay using frame-rate-independent easing,
- snap-to-named-emotion with hysteresis for render dispatch.

### Composition Rule

1. If overlay verb active: render/move from overlay expression.
2. Else if continuous verb active: render/move from verb expression.
3. Else: render/move from snapped emotion expression.

## Canonical Fixed Raw Command Set (Primary)

v3 must support these fixed commands first:

- `startVerb(verb)`
- `stopVerb()` or `clearVerb()`
- `setOverlay(verb, duration_ms)`
- `modifyValence(delta_v)`
- `modifyArousal(delta_a)`
- `setValence(v)`
- `setArousal(a)`

Optional soon-after additions:

- `setHeldValenceTarget(driver_id, target_v)`
- `releaseHeldValenceTarget(driver_id)`
- `setEmotionPreset(name)` (maps to configured V/A pair)

## Extensible Raw Command Surface (Secondary)

Support an optional generic action payload, validated at runtime:

```json
{
  "kind": "emotion.command",
  "action": "startVerb",
  "params": { "verb": "READING" }
}
```

Rules:

- Fixed command set remains the stable contract.
- Generic actions are opt-in and can be gated behind a feature flag/config.
- Unknown actions are ignored with diagnostic logging (no crash/no undefined writes).

## Minimal Semantic Mapping Table (Initial)

Baseline mapping from semantic events to behavior writes:

- `session.started` -> `setOverlay(WAKING, 1000)` and optional positive impulse
- `session.ended` -> `setVerb(SLEEPING)`
- `turn.started` -> `setVerb(THINKING)`
- `activity.started` -> `setVerb(mapActivityKindToVerb(kind))`
- `activity.finished` -> `armLinger(1000)` then fall back to `THINKING` if no replacement
- `turn.ended` -> `clearVerb()` and apply positive impulse toward joyful
- `notification` (attention pattern) -> `setOverlay(ATTRACTING_ATTENTION, 1000)`

These handlers stay intentionally tiny: decode, call mutator, return.

## Expression Space Strategy

- Use a unified `Expression` enum for all render/motion table dispatch.
- Include verb expressions and named emotion expressions.
- Include overlay expressions as explicit enum rows.
- Keep compile-time completeness checks across:
  - expression-to-face target tables,
  - expression-to-motion behavior tables,
  - expression-to-palette mappings.

## Implementation Phases and Gates

### Phase 0: Spec Alignment (this document)

Deliverables:

- This file accepted as canonical.
- Older plan docs marked as historical/superseded.

Gate:

- No unresolved architecture contradictions remain.

### Phase 1: Core + HAL extraction

Deliverables:

- `core/*`, `hal/*` moved into `robot_v3` layout.
- provisioning split (`Provisioning` vs `ProvisioningUI`).

Gate:

- Device boots, provisioning works, display and servo stable on hardware.

### Phase 2: Transport + parser split

Deliverables:

- `bridge/BridgeClient` callback-first and semantics-free.
- `agents/AgentEvents` + `agents/BridgeControl` parsing split.

Gate:

- Event/control frames parse and route correctly under reconnect stress.

### Phase 3: SceneContext decoupling

Deliverables:

- face stack no longer reaches into protocol/settings directly.
- `FrameController::tick` driven by `SceneContext`.

Gate:

- face renders correctly from synthetic context inputs alone.

### Phase 4: VerbSystem integration

Deliverables:

- continuous verbs, overlays, linger behavior implemented.
- `EventRouter` semantic mapping writes into `VerbSystem`.

Gate:

- event traces show expected verb transitions and overlay behavior.

### Phase 5: EmotionSystem integration

Deliverables:

- V/A state, decay, snap/hysteresis, held targets.
- semantic and raw command writes both operational.

Gate:

- telemetry confirms stable decay/snap behavior and target stacking.

### Phase 6: Unified expression dispatch

Deliverables:

- expression enum complete and mapped across face/motion/palette.
- motion behavior table driven by effective expression.

Gate:

- no missing expression rows; expected movement/face combinations on device.

### Phase 7: Router hardening + docs lock

Deliverables:

- deterministic ordering documented in `EventRouter`.
- raw command schema documented.
- firmware docs updated to remove split-brain guidance.

Gate:

- team can implement/extend without consulting legacy plan docs.

## Out of Scope for Initial Landing

- Full host-side unit test harness.
- Advanced blended emotional rendering (snap first, blend later if needed).
- Rich policy/priority framework beyond deterministic last-write ordering.

## Risks and Mitigations

- **Spec drift across docs**
  - Mitigation: treat this as sole execution plan and mark older plans historical.
- **Expression table mismatch bugs**
  - Mitigation: compile-time size checks and startup diagnostics.
- **Unexpected behavior jumps from rapid mixed inputs**
  - Mitigation: deterministic ordering logs and reproducible event replay scripts.
- **Schema/color breakage from enum changes**
  - Mitigation: settings schema versioning and controlled reset on mismatch.

## Definition of Done

The rewrite is complete when:

- firmware runs from `robot_v3` architecture above,
- semantic and raw event surfaces both drive behavior reliably,
- face and motion are decoupled from parser/protocol internals,
- event-to-behavior mapping remains firmware-owned and easily editable,
- docs are internally consistent with this plan.

