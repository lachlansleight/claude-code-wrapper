# Firmware behaviour (`robot_v3/`)

This document describes the **behaviour layer** of the `robot_v3/` firmware:
how incoming inputs (agent lifecycle events and raw control commands) are
mapped into robot “behaviour state”, and how that state is composed into a
single **effective expression** that drives both the face and motion.

## Behaviour systems

`robot_v3` splits behaviour into two independent systems:

- **`VerbSystem`** (`robot_v3/src/behaviour/VerbSystem.*`): discrete “what it’s doing”
  (thinking/reading/writing/executing/…); plus one-shot overlays.
- **`EmotionSystem`** (`robot_v3/src/behaviour/EmotionSystem.*`): continuous mood in
  valence/activation, with decay, snapping, and optional held targets.

`app/EventRouter` is the composition root for behaviour: it is the only place
that interprets incoming messages and mutates these systems.

## Composition rule (locked)

The render/motion layers never look at protocol details; they consume a single
`Face::Expression` computed from the behaviour systems:

1. If an **overlay verb** is active: use the overlay expression.
2. Else if a **continuous verb** is active: use the verb expression.
3. Else: use the **snapped emotion** expression.

This effective expression is produced in `SceneContextFill::fill()` and stored
in `Face::SceneContext.effective_expression`.

## Semantic agent-event mapping

The bridge emits semantic lifecycle events (`agent_event`). The firmware maps a
minimal subset into behaviour writes in `robot_v3/src/app/EventRouter.cpp`:

- `session.started` → overlay waking + positive impulse
- `session.ended` → set sleeping
- `turn.started` → set thinking
- `activity.started` → set verb from activity kind (read/write/exec)
- `activity.finished` / `activity.failed` → arm a short linger
- `turn.ended` → clear verb + positive impulse
- `notification` (attention) → overlay attracting-attention

This mapping is intentionally small. The firmware owns these decisions; the
bridge only provides lifecycle events.

## Raw control command surface

In addition to semantic `agent_event`, `robot_v3` accepts **raw behaviour control
commands** so you can test behaviour independently of any agent.

All raw control commands are ordinary WebSocket frames broadcast by the bridge
(or any WS client). They are handled in `EventRouter::dispatchRawCommand()`.

There are two ways to send commands:

- **Fixed command set** (primary): sent as top-level `type` values like
  `startVerb`, `setOverlay`, `emotion.command`, etc.
- **Extensible wrapper** (secondary): `{ "type": "emotion.command", "action": "...", "params": {...} }`
  which `EventRouter` unwraps into the same fixed commands.

### Fixed commands (supported)

`VerbSystem`:

- **Start verb**

  ```json
  { "type": "startVerb", "verb": "reading" }
  ```

- **Stop / clear verb**

  ```json
  { "type": "clearVerb" }
  ```

- **Overlay**

  ```json
  { "type": "setOverlay", "verb": "waking", "duration_ms": 1000 }
  ```

`EmotionSystem` (recommended as `emotion.command` wrapper):

```json
{ "type": "emotion.command", "action": "modifyValence", "params": { "delta_v": 0.15 } }
{ "type": "emotion.command", "action": "modifyArousal", "params": { "delta_a": 0.10 } }
{ "type": "emotion.command", "action": "setValence",    "params": { "v": 0.40 } }
{ "type": "emotion.command", "action": "setArousal",    "params": { "a": 0.25 } }
{ "type": "emotion.command", "action": "setHeldValenceTarget", "params": { "driver_id": 1, "target_v": -0.6 } }
{ "type": "emotion.command", "action": "releaseHeldValenceTarget", "params": { "driver_id": 1 } }
```

### Deterministic ordering

The firmware applies mutations in **message dispatch order** within
`EventRouter::onBridgeMessage()`:

1. raw command dispatch
2. `AgentEvents::dispatch` (semantic `agent_event`)
3. `BridgeControl::dispatch` (palette/display-mode/servo overrides)

Within that ordering, later writes win (“last write wins”).

## Bridge-driven controls (non-behaviour)

Some WS messages are not “behaviour” but are still useful for debugging:

- `config_change` → toggles face/text mode
- `setColor` → palette change
- `set_servo_position` → servo hold override

These are handled by `agents/BridgeControl` and wired by `EventRouter`.

