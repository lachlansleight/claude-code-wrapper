# Bridge client, agent events, and event routing

The firmware is the consumer of an agent-agnostic WebSocket protocol
served by the Node bridge in [`plugin/`](../../plugin/). This doc
describes the firmware-side pipeline: how frames arrive, get parsed
into structured state, and drive the verb and emotion systems. The
bridge end of the protocol is documented in
[`docs/bridge/OBJECT_INTERFACE.md`](../bridge/OBJECT_INTERFACE.md) and
[`docs/bridge/HOOK_MAPPING.md`](../bridge/HOOK_MAPPING.md). Read those
for the full event vocabulary and per-agent hook mappings — this doc
focuses on what the firmware does with them.

## Pipeline

```
   ws://host:port/ws?token=…
            │
            ▼
   ┌────────────────────────┐
   │  BridgeClient          │  parse JSON, heartbeat, reconnect
   └─────────┬──────────────┘
             │ JsonDocument
             ▼
   ┌────────────────────────┐  inspect "type" field
   │  EventRouter::         │
   │     onBridgeMessage()  │
   └─┬────────────────────┬─┘
     │                    │
     ▼                    ▼
   AgentEvents          BridgeControl
   (semantic)           (palette, mode, servo)
     │                    │
     │                    ▼
     │            Settings / Motion / FaceMode
     ▼
   AgentState  ──► EventRouter::tick() applies derived
                   verbs + emotion drivers
```

## BridgeClient

`src/bridge/BridgeClient.{h,cpp}`. Thin WebSocket client wrapping
ArduinoWebsockets:

- Connects to `ws://<host>:<port>/ws?token=<token>` from the
  provisioning config.
- Heartbeat: 15 s interval, 3 s ping timeout, 2 consecutive failures
  treated as dead → reconnect.
- Auto-reconnect with backoff.

Public surface:

```cpp
namespace Bridge {
    void begin(const char* host, uint16_t port, const char* token);
    void tick();
    bool isConnected();

    void onMessage(MessageHandler);     // single slot
    void onConnection(ConnectionHandler);

    bool sendRaw(const char* json);
    bool requestSessions();             // {"type":"request_sessions"}
}
```

Inbound text frames are parsed into an `ArduinoJson::JsonDocument` and
handed to the registered `MessageHandler`. There is one slot — the
`EventRouter` owns it. The router fans the document out to multiple
parsers without involving the transport layer.

## AgentEvents — agent_event envelopes

`src/agents/AgentEvents.{h,cpp}` parses the bridge's `agent_event`
envelopes into a singleton `AgentState` that downstream code reads.

The envelope shape is fixed:

```json
{
  "type": "agent_event",
  "agent": "claude",
  "ts": 1736400000000,
  "session_id": "…",
  "turn_id": "…",
  "event": { "kind": "activity.started", "activity": { ... } }
}
```

The full enumeration of `event.kind` values (18 kinds) and the
`ActivityRef` / permission shapes are documented in
[`docs/bridge/OBJECT_INTERFACE.md`](../bridge/OBJECT_INTERFACE.md). The
firmware has callbacks for the ones it cares about; the rest go through
the generic `onEvent` path.

### AgentState (singleton snapshot)

`AgentState` is the firmware's projection of "what is the agent doing
right now". Major fields:

| Field                                     | Purpose                                            |
|-------------------------------------------|----------------------------------------------------|
| `wifi_connected`, `ws_connected`          | Connection state                                   |
| `working`                                 | Turn in progress                                   |
| `agent`, `session_id`, `latched_session`  | Identity (with single-session latching)            |
| `current_tool`, `tool_detail`             | Active activity name + summary                     |
| `current_tool_end_ms`                     | 0 while running, set to `now` on finish/fail       |
| `status_line`                             | High-level status, e.g. "Thinking", "Done"         |
| `subtitle_tool`, `body_text`              | Strings for text-mode display                      |
| `latest_shell_command`                    | For diagnostic display                             |
| `latest_read_target`, `latest_write_target` | Most recent file paths                           |
| `read_tools_this_turn`, `write_tools_this_turn` | Counters for activity dot rings              |
| `pending_permission`                      | Request id, empty if none                          |
| `pending_tool`, `pending_detail`          | Permission target tool + summary                   |
| `text_tool_linger_until_ms`               | Hold the tool subtitle ~1 s past `activity.finished` |
| `render_mode`                             | Face / Text / Debug                                |
| `thinking_title_since_ms`, `turn_started_wall_ms`, `done_turn_elapsed_ms` | Time references for text mode |

### Session latching

The bridge can multiplex multiple agent sessions, but the robot has a
single face. `latchFilter()` keeps the first `session_id` it sees as
sticky and ignores frames from other sessions, *unless* the bridge's
`active_sessions` list says the latched session has gone away (the
router polls this every 5 s via `Bridge::requestSessions()`). When
the latched session ends, the next session seen takes the latch.

### Text-tool linger

When an `activity.finished` arrives, the subtitle would normally drop
back to "Thinking" instantly. To avoid strobing during a fast burst
of tools, `tick()` holds the subtitle for ~1 s after finish via
`text_tool_linger_until_ms`.

### Activity classification (firmware-side)

```cpp
enum ActivityAccess { ACTIVITY_READ, ACTIVITY_WRITE };
ActivityAccess classifyActivity(ActivityKind, tool, summary);
```

This is **separate** from the bridge's `activity-classify.ts`. The
bridge maps tool names → `ActivityKind` (file.read, shell.exec, …);
the firmware then maps `ActivityKind` plus shell-command shape → a
two-way READ/WRITE distinction used for the verb (see
[`VERB_SYSTEM.md`](VERB_SYSTEM.md)) and the activity dot rings. Don't
unify them — they answer different questions.

### Public API

```cpp
namespace AgentEvents {
    AgentState& state();
    void begin();
    void tick();                              // drive linger countdown
    void dispatch(const JsonDocument&);       // entry point

    void setWifiConnected(bool);
    void setWsConnected(bool);
    void setRenderMode(RenderMode);

    // Callback registration (single slot each)
    void onPermissionRequest(PermissionRequestHandler);
    void onPermissionResolved(PermissionResolvedHandler);
    void onEvent(EventHandler);               // every classified event
    void onRawEvent(RawEventHandler);         // every JSON frame
}
```

## BridgeControl — non-semantic frames

`src/agents/BridgeControl.{h,cpp}` handles control frames that aren't
agent activity:

| Frame `type`           | Payload                              | Handler                        |
|------------------------|--------------------------------------|--------------------------------|
| `setColor`             | `{ key, r, g, b }` (0–255)           | Palette mutation               |
| `config_change`        | `{ display_mode?, motors_disabled? }`| Display mode + motor enable    |
| `set_servo_position`   | `{ position, duration_ms }`          | Servo test override            |

`EventRouter` registers handlers that:

- Apply palette changes via `Settings::setColorRgb()`.
- Apply display mode via `Settings::setFaceModeEnabled()` and
  `AgentEvents::setRenderMode()`.
- Apply motor enable via `Settings::setMotorsDisabled()` and
  `Motion::setEnabled()`.
- Apply servo override via `Motion::holdPosition()`.

These changes also bump `Settings::settingsVersion()`. `FrameController`
invalidates its tween when the version changes so a palette swap takes
effect in one frame instead of bleeding through the smoothing window.

## EventRouter — composition root

`src/app/EventRouter.{h,cpp}` is where every callback is wired and
where verbs and emotion drivers are derived. Two responsibilities:

1. **Register everything in `begin()`** — the verb system, the emotion
   system, the agent-event callbacks, the bridge-control callbacks,
   and the bridge connection callback.
2. **Run `tick()` every loop iteration** in deterministic order:

```
1. AgentEvents::tick()        // text-tool linger countdown
2. Periodically request bridge sessions list (every 5 s)
3. VerbSystem::tick()         // overlays / linger / strain promotion
4. Apply derived emotion drivers based on AgentState + verb state
5. EmotionSystem::tick()      // goal / raw / snap update
```

### Event → verb/emotion mapping

| `event.kind`               | Verb action                                               | Emotion action                          |
|----------------------------|-----------------------------------------------------------|------------------------------------------|
| `session.started`          | Fire `Waking` overlay (1 s, post→`None`)                  | Impulse `+0.6 V, +0.6 A`                |
| `session.ended`            | `setVerb(Sleeping)`                                       | —                                       |
| `turn.started`             | If `Sleeping`, fire `Waking` first; then `Thinking`       | —                                       |
| `turn.ended`               | `clearVerb()`                                             | Impulse `+0.7 V, +0.9 A`                |
| `activity.started` (read)  | `setVerb(Reading)`                                        | —                                       |
| `activity.started` (write) | `setVerb(Writing)`                                        | —                                       |
| `activity.started` (shell) | `setVerb(Executing)`                                      | —                                       |
| `activity.finished/failed` | `armLinger(1000)`                                         | —                                       |
| `notification` ("needs…")  | Fire `AttractingAttention` overlay (1 s)                  | —                                       |
| `permission.requested`     | —                                                         | Hold `PendingPermission` driver at -0.6 |
| `permission.resolved`      | —                                                         | Release `PendingPermission` driver      |

### Derived held drivers

In step 4 every tick, the router decides whether each held driver
should be active and updates the emotion system:

- **`PendingPermission`** active iff `AgentState.pending_permission`
  is non-empty. Held at `v = -0.6`.
- **`Straining`** active iff `VerbSystem::current() == Straining` and
  `timeInCurrentMs() ≥ 30000`. Held at `v = -0.4`.

When the condition clears, the driver is released. See
[`EMOTION_SYSTEM.md`](EMOTION_SYSTEM.md) for how drivers pull on the
goal value.

### Public API

```cpp
namespace EventRouter {
    void begin();
    void tick();
    void onBridgeMessage(const JsonDocument&);
    void onBridgeConnection(bool connected);
}
```

## SceneContextFill — world snapshot

`src/app/SceneContextFill.{h,cpp}` runs every loop iteration after the
router and produces a flat, immutable `Face::SceneContext` for the
face renderer. Think of it as the boundary between "live system state"
and "what the renderer sees".

Responsibilities:

1. **Compose the effective expression** from `VerbSystem::effective()`
   and `EmotionSystem::snapped()`:
   - If a verb overlay is active → overlay expression.
   - Else if a base verb is active → that verb's expression.
   - Else → emotion's expression (from the snapped `NamedEmotion`).
2. **Snapshot the emotion blend** outputs:
   `EmotionBlend::blendedFaceParams()` and
   `EmotionBlend::blendedEmotionArmMotion()` at the current raw point.
3. **Resolve the accent palette colour** from the active expression's
   `NamedColor` and write the RGB888 + RGB565 forms.
4. **Sanitize all display strings** through `AsciiCopy::copy()` (or
   `copyPreserveNewlines()` for body text).
5. **Copy diagnostic state** — verb chain, snapped emotion name,
   pending snap state, held drivers — into the context for debug-mode
   rendering.

The output `SceneContext` is the only thing `FrameController` reads.
Everything that wants to influence the face must do so by changing
state that `SceneContextFill` reads.
