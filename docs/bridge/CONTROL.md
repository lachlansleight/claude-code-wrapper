# Bridge control guide

This document explains how to **control the robot via the bridge**, both:

- **Semantically** (by emitting `agent_event` lifecycle events), and
- **Directly** (by sending raw behaviour commands that drive `VerbSystem` /
  `EmotionSystem` without any agent).

The bridge is presentation-agnostic: it does not decide how the robot should
feel. It only forwards inputs to WebSocket clients.

## Transport: WebSocket broadcast model

The bridge hosts a token-authenticated WebSocket at:

`ws://<host>:<port>/ws?token=<BRIDGE_TOKEN>`

Every connected client (ESP32 firmware, control panels, simulators) receives the
same broadcast stream.

## Controlling via semantic agent events

The bridge emits the canonical `agent_event` stream as:

```json
{
  "type": "agent_event",
  "agent": "claude",
  "ts": 1710000000000,
  "session_id": "sess_123",
  "turn_id": "turn_1",
  "event": { "kind": "turn.started" }
}
```

The full vocabulary is in [`OBJECT_INTERFACE.md`](OBJECT_INTERFACE.md).

### Simulator path (no agent required)

Any WebSocket client may inject synthetic lifecycle events using:

```json
{ "type": "emit_agent_event", "agent": "simulator", "session_id": "s1", "turn_id": "t1",
  "event": { "kind": "turn.started" } }
```

This is how `control/index.html` can drive the robot without a real agent.

## Controlling behaviour directly (verb / emotion)

To test behaviour independent of agent parsing, send **raw control commands**.
These are ordinary JSON messages broadcast to all WS clients; `robot_v3` handles
them in `EventRouter::dispatchRawCommand()`.

### Verb commands

```json
{ "type": "startVerb", "verb": "reading" }
{ "type": "clearVerb" }
{ "type": "setOverlay", "verb": "waking", "duration_ms": 1000 }
```

Valid verbs: `thinking`, `reading`, `writing`, `executing`, `straining`,
`sleeping`, `waking`, `attracting_attention`, `none`.

### Emotion commands

The recommended wrapper is:

```json
{ "type": "emotion.command", "action": "modifyValence", "params": { "delta_v": 0.1 } }
```

Supported actions include:

- `modifyValence` (`delta_v` / `delta`)
- `modifyArousal` (`delta_a` / `delta`)
- `setValence` (`v` / `value`)
- `setArousal` (`a` / `value`)
- `setHeldValenceTarget` (`driver_id`, `target_v`)
- `releaseHeldValenceTarget` (`driver_id`)

### Other useful controls

These are not behaviour, but are helpful for debugging:

```json
{ "type": "config_change", "display_mode": "face" }   // or "text"
{ "type": "setColor", "color": 1, "r": 255, "g": 255, "b": 255 }  // palette entry by index (robot_v3)
{ "type": "set_servo_position", "angle": 0, "duration_ms": 2000 }
```

## HTTP helpers (for browsers / curl)

If you don’t want to open a WebSocket client, the bridge exposes HTTP endpoints
that **broadcast to WS clients**.

All `POST` endpoints require `Authorization: Bearer $BRIDGE_TOKEN`.

- `GET /api/raw/capabilities` — discoverable endpoint catalog
- `POST /api/raw/broadcast` — send any JSON object verbatim
- plus typed helpers under `/api/raw/*` (verb + emotion + config)

See [`CURL_RECIPES.md`](CURL_RECIPES.md) for curl examples.

