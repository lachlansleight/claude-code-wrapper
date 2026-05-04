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
                                          │   ESP32-S3 (robot_v2/)    │
                                          │                           │
                                          │  BridgeClient   ──► AgentEvents.dispatch
                                          │                              │
                                          │                       mutate AgentState
                                          │                              │
                                          │           ┌──────────────────┴──────────┐
                                          │           ▼                              ▼
                                          │     Personality                  (event callbacks)
                                          │   (state machine)
                                          │           │
                                          │           ▼
                                          │   ┌───────┴───────┐
                                          │   ▼               ▼
                                          │ MotionBehaviors  FrameController
                                          │   │               │
                                          │ Motion (servo)   Display (TFT sprite + DMA)
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

## Stage 6 — Firmware decodes and updates `AgentState`

`robot_v2/BridgeClient.cpp` is a thin wrapper over Markus Sattler's
WebSocketsClient — auto-reconnect, 15 s heartbeat, polls the bridge for
the active session list every 5 s while no session is latched. Every
text frame is parsed into an `ArduinoJson::JsonDocument` and handed to
`AgentEvents::dispatch`.

`AgentEvents` (`robot_v2/AgentEvents.cpp`) is the firmware's analogue of
`state.ts`: it owns a single `AgentState` struct holding everything the
renderers might need (`working`, `current_tool`, `tool_detail`,
`pending_permission`, `last_summary`, `read_tools_this_turn`,
`latest_shell_command`, `body_text`, …). The dispatcher is a flat switch
on the WS frame `type` (`agent_event`, `setColor`, `config_change`,
`active_sessions`, `set_servo_position`, …).

`agent_event` frames are routed by `event.kind` into mutations of
`AgentState`. For example:

- `turn.started` → resets per-turn counters, sets `status_line` to "Thinking",
  clears stale fields, clears any stale pending permission.
- `activity.started` → writes `current_tool` + `tool_detail`, picks a
  status title (`Reading` / `Writing` / `Executing`) from the
  `activity.kind`, derives `latest_shell_command` / `latest_read_target`
  / `latest_write_target` from the summary.
- `activity.finished` / `activity.failed` → arms a 1 s `text_tool_linger`
  so the tool label doesn't disappear instantly, increments
  `read_tools_this_turn` / `write_tools_this_turn`.
- `message.assistant` → flips the title to "Done", populates `body_text`,
  records the turn duration.
- `permission.requested` → populates `pending_permission`, `pending_tool`,
  `pending_detail`.

A **session latch** ensures only one agent session drives the robot at
a time — if you have two terminals open, the first one through the door
wins, others are filtered out until the latch releases (session ended,
or vanished from the active-sessions poll).

After mutating state, `AgentEvents` fires the single registered
`EventHandler` (`onAgentEvent`) — that's how `Personality` finds out an
event happened.

## Stage 7 — Personality derives a state

`Personality` (`robot_v2/Personality.cpp`) is a 13-state machine that
reduces the raw event stream into one current "what is the robot doing
right now?" answer. The full state table lives at the top of
Personality.cpp; see `docs/firmware/PERSONALITY.md` for the as-built spec.

Quick sketch:

```
SLEEP  ──session.started──►  WAKING  ──(1s)──►  EXCITED  ──(10s)──►  READY  ──(60s)──►  IDLE  ──(30min)──►  SLEEP
                                                         ▲                                      │
              ┌──turn.started/activity──────────────────►┘                                      │
              │                                                                                 │
              ▼                                                                                 │
         THINKING ◄──(linger)──── READING / WRITING                                             │
              │                                                                                 │
              │                   EXECUTING ──(5s)──► EXECUTING_LONG ──(30s)──► BLOCKED         │
              │                                                                                 │
              ▼                                                                                 │
          turn.ended ──► FINISHED ──(1.5s)──► EXCITED ──────────────────────────────────────────┘

  Permission pending (polled from AgentState):  current ──► BLOCKED ──► (resume on resolve)
  "Claude needs ..."  Notification:             current ──► WANTS_ATTENTION ──(1s)──► current
```

Two states are *protected* with a `min_ms` window — `FINISHED` (1.5 s)
and `WAKING`/`WANTS_ATTENTION` (1 s) — so their entry animations can't
be cut short. Pre-empting requests get queued and fire when the window
expires.

## Stage 8 — MotionBehaviors and FrameController react each tick

Two consumers poll `Personality::current()` every loop iteration:

**`MotionBehaviors`** (`robot_v2/MotionBehaviors.cpp`) is a per-state
table of motor recipes — one of `NONE`, `STATIC`, `RANDOM_DRIFT`,
`OSCILLATE`, `WAGGLE`, `THINKING`. On state entry it issues the
appropriate `Motion::play*` call; on subsequent ticks it re-triggers
periodic motions (waggle every period, oscillate leg swap at half
period, drift to a new random target every period+jitter). All servo
output is clamped to a hard safe range (±45°). One table, one tuning
surface — see `docs/firmware/MOTION_BEHAVIORS.md`.

**`FrameController`** (`robot_v2/FrameController.cpp`) picks a target
`FaceParams` row for the current state from `kBaseTargets`, tweens
between rows over 250 ms on state change, and layers procedural
modulators on top each frame (~30 fps, ~60 fps when text streams are
active):

- **breath** — universal ±1.5 px sine on `eye_dy` / `mouth_dy`, 4 s period
- **body-bob** — vertical face offset synced to the motor period for
  states with rhythmic motion
- **thinking tilt-flip** — periodically inverts `face_rot` + `pupil_dx`
- **idle glance** — slow random pupil targets
- **gaze wander** — small per-state pupil oscillation
- **blink scheduler** — per-state cadence
- **mood-ring colour easing** — `Settings::colorRgb(NamedColor)` for the
  current state, eased with a 200 ms time constant

The composed `FaceParams` is rendered via `Scene` /
`FaceRenderer` (face mode) or `TextScene` (text mode, toggled via
`config_change`), drawn into the `Display` sprite, then DMA-pushed to
the panel. Settings (palette, render mode) come from `Settings.cpp`
backed by NVS; `settingsVersion()` ticks on changes so FrameController
can re-seed mood colour without waiting for a state transition.

`Motion::tick()` also runs every loop — it's responsible for actually
slewing the servo PWM towards whatever angle MotionBehaviors last
requested.

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
| Personality state machine     | `docs/firmware/PERSONALITY.md`               |
| Per-state motor table         | `docs/firmware/MOTION_BEHAVIORS.md`          |
| Face renderer + mood ring + display hardware | `docs/firmware/DISPLAY_AND_FACE.md` |
