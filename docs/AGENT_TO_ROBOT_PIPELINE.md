# Agent → Robot Pipeline

End-to-end tour of how a single keystroke inside Claude Code (or Codex /
Cursor / OpenCode) eventually moves a servo and changes a face on the
240×240 GC9A01 panel. Read this first if you want a single mental model
of the whole system; the per-layer docs go deeper on each stage.

```
   ┌──────────────┐   stdin JSON       ┌─────────────────┐   POST /hooks/<a>   ┌──────────────────┐
   │  Agent CLI   │ ─────────────────► │  helper script  │ ──────────────────► │    bridge        │
   │ (Claude/...) │                    │ (forwarder.mjs) │                     │  (Node HTTP+WS)  │
   └──────────────┘                    └─────────────────┘                     └────────┬─────────┘
                                                                                        │
                                                                              parse → AgentEvent
                                                                                        │
                                                                                  bus.emit
                                                                                        │
                                                       ┌────────────────────────────────┼────────────────────┐
                                                       ▼                                ▼                    ▼
                                                     ws.ts                        firebase.ts            state.ts
                                                  (broadcast)                  (RTDB mirror, opt)     (in-mem store)
                                                       │
                                                  WS frame
                                                       │
                                          ┌────────────▼──────────────┐
                                          │   ESP32-S3 (robot_v3/)    │
                                          │                           │
                                          │  BridgeClient   ──► EventRouter::onBridgeMessage
                                          │                              │
                                          │       dispatch ordering:
                                          │         1) raw behaviour commands
                                          │         2) AgentEvents (semantic agent_event)
                                          │         3) BridgeControl (palette/mode/servo)
                                          │
                                          │  ┌─────────────── behaviour ───────────────┐
                                          │  ▼                                         ▼
                                          │ VerbSystem                             EmotionSystem
                                          │  │                                         │
                                          │  └──────────────► SceneContextFill ◄────────┘
                                          │                    (effective Expression)
                                          │
                                          │  ┌─────────────── outputs ────────────────┐
                                          │  ▼                                        ▼
                                          │ MotionBehaviors(Expression)          Face::FrameController(ctx)
                                          │  │                                        │
                                          │ Motion (servo)                      Display (TFT sprite + DMA)
                                          └───────────────────────────┘
```

---

## Stage 1 — Agent CLI fires a hook

Each agent CLI exposes a lifecycle hook system that runs an external
command for events like "tool about to execute", "user submitted a
prompt", "session ended". The bridge does **not** integrate with any
agent's native API; it only consumes hooks. That's deliberate — anything
that can fire a hook can drive the robot.

| Agent       | Hook mechanism                          | Wiring                                   |
|-------------|-----------------------------------------|------------------------------------------|
| Claude Code | Plugin in `plugin/`, registered via `plugin/hooks/hooks.json` | installed via Claude Code's plugin marketplace |
| Codex CLI   | `~/.codex/hooks.json` — `command` per event | manual setup, see `docs/getting-started/CODEX.md` |
| Cursor 1.7+ | `~/.cursor/hooks.json` — `command` per event | manual setup, see `docs/getting-started/CURSOR.md` |
| OpenCode    | JS plugin in `~/.config/opencode/plugins/` | `helpers/opencode-bridge-plugin.js` |

Each hook execution runs a small Node forwarder script that reads the
hook's payload from stdin and POSTs it to the bridge.

## Stage 2 — Forwarder POSTs to the bridge

Forwarders all live in `helpers/`:

- `claude-hook-forward.mjs` — also tails the transcript JSONL to
  enrich `Stop` events with the assistant's final reply text (Claude
  Code doesn't put this in the hook payload directly).
- `codex-hook-forward.mjs`, `cursor-hook-forward.mjs` — minimal:
  read stdin, POST.
- `opencode-bridge-plugin.js` — runs in-process inside OpenCode and
  uses global `fetch`.

All four POST `{ hook_type, payload }` to
`POST /hooks/<agent>` with `Authorization: Bearer $BRIDGE_TOKEN`.
They are **fire-and-forget** with a 500 ms abort
(`BRIDGE_HOOK_TIMEOUT_MS`) so a down or slow bridge never holds up the
agent's turn.

## Stage 3 — Bridge parses the hook into an AgentEvent

`plugin/src/http.ts` receives the POST. The agent name in the URL
selects a parser from `plugin/src/adapters/`:

```
http.ts  →  getParser(agent).parse({ hook_type, payload })  →  ParsedEvent[]
```

Each parser is a thin translation layer that turns the agent-native hook
into zero, one, or several entries from a single shared vocabulary —
the **AgentEvent** discriminated union defined in
`plugin/src/agent-event.ts`. Kinds include:

```
session.started / session.ended
turn.started   / turn.ended
message.user   / message.assistant   / thinking
activity.started / activity.finished / activity.failed
permission.requested / permission.resolved
todo.updated
subagent.started / subagent.finished
agent.question
context.compacting
notification
unknown
```

Tool calls collapse onto a small `ActivityKind` enum
(`file.read`, `file.write`, `shell.exec`, `search.code`, `web.fetch`,
`mcp.call`, …) via `plugin/src/activity-classify.ts`. A
`PreToolUse{ tool_name: "Read" }` from Claude and a
`tool.execute.before{ tool: "read" }` from OpenCode both become the same
`{ kind: 'activity.started', activity: { kind: 'file.read', tool: ..., summary: ... } }`.

The full per-agent translation table is `docs/bridge/HOOK_MAPPING.md`.
The vocabulary spec is `docs/bridge/OBJECT_INTERFACE.md`.

## Stage 4 — Envelope onto the in-process bus

http.ts wraps each ParsedEvent into an `AgentEventEnvelope`:

```ts
{
  type: 'agent_event',
  agent: 'claude' | 'cursor' | 'codex' | 'opencode',
  ts: <ms since epoch>,
  session_id?: string,
  turn_id?: string,
  event: AgentEvent,
}
```

…and emits it on the in-process bus (`plugin/src/bus.ts`). Three
subscribers are attached at startup:

- **`ws.ts`** — broadcasts the envelope verbatim to every connected
  WebSocket client.
- **`firebase.ts`** — optional RTDB mirror. Off unless `BRIDGE_DATABASE_URL`
  + `BRIDGE_AGENT_ID` are set.
- **`state.ts`** — keeps an in-memory store of pending permissions,
  sessions, and chat logs that backs the `/api/*` endpoints.

Permission events are special-cased: http.ts mutates `state.ts` (add /
remove pending entries) and emits a separate `permission_request` /
`permission_resolved` bus message in addition to the `agent_event`.

## Stage 5 — WebSocket frame to the firmware (and any other client)

ws.ts hosts a token-authed WebSocket at `ws://<host>:<port>/ws?token=...`.
On every `agent_event` bus message it sends the envelope JSON to every
client. The firmware is one client; `examples/ws-client.html` is a
debugging dashboard; you can write your own.

Clients can also send messages **back** to the bridge:

- `set_servo_position` / `setColor` / `config_change` / `emit_agent_event`
  — broadcast to all clients (the firmware listens for these to expose
  manual controls and a simulator path).
- `permission_verdict` — bridge clears local pending state and broadcasts.
  See "Permission verdicts" below for why this can't actually unblock
  Claude Code today.
- `send_message` — appended to a chat log; broadcast as `inbound_message`.

## Stage 6 — Firmware routes messages and updates behaviour

`robot_v3/src/bridge/BridgeClient.cpp` is a thin wrapper over Markus Sattler's
WebSocketsClient — auto-reconnect, 15 s heartbeat, polls the bridge for
the active session list every 5 s while no session is latched. Every
text frame is parsed into an `ArduinoJson::JsonDocument` and handed to
`EventRouter::onBridgeMessage()`.

`EventRouter` applies three dispatch passes in deterministic order:

1. **Raw behaviour commands** (e.g. `emotion.command`, `startVerb`) are applied
   first so you can test behaviour independently of any agent.
2. **Semantic `agent_event`** frames are parsed into `AgentEvents::AgentState`
   (a side-effect-free store) and routed into tiny behaviour writes (Verb/Emotion).
3. **Bridge controls** (`config_change`, `setColor`, `set_servo_position`) are
   routed via `BridgeControl` into Settings / motion hold overrides.

Behaviour is split into `VerbSystem` + `EmotionSystem`, and composed into a
single effective `Face::Expression` by `SceneContextFill` each frame.

A **session latch** ensures only one agent session drives the robot at
a time — if you have two terminals open, the first one through the door
wins, others are filtered out until the latch releases (session ended,
or vanished from the active-sessions poll).

## Stage 7 — Firmware composes behaviour into an expression

`robot_v3` does not use the old monolithic `Personality` module. Instead:

- `VerbSystem` holds a discrete “what it’s doing” verb (plus timed overlays).
- `EmotionSystem` holds continuous valence/activation and snaps to a named emotion.
- `SceneContextFill` combines these into a single effective `Face::Expression`.

That single effective expression drives **both** motion and face rendering.

## Stage 8 — MotionBehaviors + FrameController react each tick

Two output systems consume the same effective `Face::Expression`:

- **`MotionBehaviors`** (`robot_v3/src/hal/MotionBehaviors.cpp`) is an
  expression-indexed motor table (modes like `STATIC`, `OSCILLATE`, `WAGGLE`,
  `THINKING`). It issues `Motion::play*` calls on entry and schedules periodic
  retriggers. It also exposes `periodMsFor(Expression)` so the face can sync
  body-bob to arm rhythm.

- **`Face::FrameController`** (`robot_v3/src/face/FrameController.cpp`) picks a
  target `FaceParams` row for the current expression, tweens between rows, and
  layers procedural modulators (blink, gaze, breath, body-bob, thinking
  tilt-flip). It dispatches to `Scene` (face mode) or `TextScene` (text mode),
  then pushes via `Display::pushFrame()`.

`Motion::tick()` still runs every loop to slew the servo PWM toward the latest
requested target.

---

## Permission verdicts: the asterisk

The bridge exposes `POST /api/permissions/:id` and a WS
`permission_verdict` message. **Neither of these can actually approve
or deny a tool call inside the agent CLI**, because the bridge has no
back-channel to the agent — it only consumes hooks, it can't reply to
them. `applied: true` in the HTTP response means "we cleared our local
pending entry"; the user still has to answer the prompt in the agent's
own terminal. Codex and OpenCode have hook-based permission flows that
*could* be wired up to deliver verdicts but aren't today.

The firmware's recovery path: `turn.started` clears any pending
permission state, since a new turn implies the old prompt is no longer
blocking.

## Where each layer's docs live

| Stage                         | Doc                                          |
|-------------------------------|----------------------------------------------|
| Hook payloads → AgentEvent    | `docs/bridge/HOOK_MAPPING.md`                |
| AgentEvent vocabulary         | `docs/bridge/OBJECT_INTERFACE.md`            |
| HTTP / WS surface             | `README.md` ("HTTP API" / "WebSocket API")   |
| Per-agent setup               | `docs/getting-started/`                      |
| Firmware module map           | `docs/firmware/OVERVIEW.md`                  |
| Behaviour model (verbs + emotion) | `docs/firmware/BEHAVIOUR.md`            |
| Face renderer + mood ring + display hardware | `docs/firmware/DISPLAY_AND_FACE.md` |
