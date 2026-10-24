# Bridge integration

How the Mac app connects to the bridge, which messages it should react to, and
the gaps in the event data. Checked against commit `1094a48`.

The Mac app is **just another WebSocket client**, exactly like the robot. The
bridge sends every message to every connected client, so the robot and the app
can run at the same time.

> **Note (2026-10-24):** in the finalised plan ([`PLAN.md`](PLAN.md)) the app
> hosts the firmware core, so the latch, verbs and emotions below are the
> firmware's own code. Two things are the host's job: forwarding the core's
> outbound `{"type":"request_sessions"}` (it asks every 5 s while
> unlatched), and bounce detection, which runs in Swift on the raw frames
> before the latch, as described under "When to bounce".

> **Note (phase 2 of [`docs/sultana/PLAN.md`](../sultana/PLAN.md)):** the
> app now runs its own bridge by default, so "start the bridge" below
> applies to Settings ▸ Use ▸ External bridge. See
> [`sultana-dock/README.md`](../../sultana-dock/README.md#the-built-in-bridge).

---

## Connecting

- **Start the bridge:** `node plugin/dist/index.js`. Config comes from env vars
  or `.env`:
  - `BRIDGE_TOKEN` (required);
  - `BRIDGE_PORT` (default `8787`);
  - `BRIDGE_HOST` (default `127.0.0.1`).
- **Address:** `ws://<host>:<port>/ws`. Any other path is rejected.
- **Auth** (`plugin/src/auth.ts`): any one of these:
  - `Authorization: Bearer <token>`;
  - `X-Bridge-Token: <token>`;
  - `?token=<token>` in the URL.

  A bad token gets `401` during the WebSocket handshake.
- **Same Mac:** if the bridge runs on the same Mac, `127.0.0.1` works. If it
  runs on another machine, that machine needs `BRIDGE_HOST=0.0.0.0` (the robot
  already needs this, since it connects over WiFi).
- **On connect, the server sends:**
  ```json
  {"type":"hello","client_id":"ws_…","server_version":"0.3.0"}
  {"type":"active_sessions","session_ids":["…", "…"]}
  ```
- **Messages the client can send** (`plugin/src/ws.ts`):

  | Message | Purpose |
  |---|---|
  | `{"type":"ping"}` | Server replies `{"type":"pong"}`. Use it as a heartbeat. |
  | `{"type":"request_sessions"}` | Server replies with `active_sessions`. |
  | `emit_agent_event`, `config_change`, `setColor`, `set_servo_position`, `send_message`, `permission_verdict` | Not needed for the app |

- **The robot's connection settings** (`robot_v3/src/bridge/BridgeClient.cpp`):
  ping every 15 s, 3 s timeout, disconnect after 2 failures, reconnect after
  2 s. Copy these, or use a normal backoff.
- **Mac side:** `URLSessionWebSocketTask` covers all of this. Keep host and port
  in preferences, and the token in the Keychain.

## Messages the app receives

| `type` | Shape | Use |
|---|---|---|
| `agent_event` | `{type, agent, ts, session_id?, turn_id?, event: {kind, …}}` (`AgentEventEnvelope`, `plugin/src/agent-event.ts`) | The main input. Feed it to the behaviour layer. |
| `active_sessions` | `{type, session_ids: string[]}` | Session latch (see `ENGINE.md` §2.6). Sent on connect and whenever the list changes. |
| Raw commands: `startVerb`, `clearVerb`, `setOverlay`, `emotion.command`, `modifyValence`, `modifyArousal`, `setValence`, `setArousal`, `setHeldValenceTarget`, `releaseHeldValenceTarget` | Sent by the `/api/raw/*` HTTP routes, which the editor's robot buttons use | The robot obeys these (`EventRouter.cpp` `dispatchRawCommand`). For full fidelity the app should too, so the editor can drive both. |
| `setColor`, `config_change`, `set_servo_position` | Robot hardware control | Optional: palette in preferences; display mode is not needed; servo hold could move the window's arms |
| `inbound_message`, `outbound_reply`, `permission_request`, `permission_resolved`, `session_event` | Chat and permission channel | Ignore |

**`agent` values:** `claude`, `codex`, `cursor`, `opencode`, `simulator`.

**`event.kind` values:** `session.started`, `session.ended`, `turn.started`,
`turn.ended`, `message.user`, `message.assistant`, `thinking`,
`activity.started`, `activity.finished`, `activity.failed`,
`permission.requested`, `permission.resolved`, `todo.updated`,
`subagent.started`, `subagent.finished`, `agent.question`,
`context.compacting`, `notification`, `unknown`.

Full reference: `docs/bridge/OBJECT_INTERFACE.md`. Per-agent mapping:
`docs/bridge/HOOK_MAPPING.md`.

---

## When to bounce

### Turn complete → bounce

- **Signal:** an `agent_event` with `event.kind === "turn.ended"`, optionally
  only when `event.cause === "completed"`. Possible causes: `completed`,
  `aborted`, `error`, `unknown`.
- **Claude:** the `Stop` hook produces, in order:
  1. `{kind:"message.assistant", text, final:true}`, only if transcript text
     was scraped;
  2. `{kind:"turn.ended", cause:"completed", last_assistant_text}`.

  See `plugin/src/adapters/claude.ts`, `case 'Stop'`.
- The Codex, Cursor and OpenCode adapters also produce `turn.ended`.
- **Check every session, not just the latched one.** The robot's latch
  (`ENGINE.md` §2.6) drops events from sessions it isn't showing. If session A
  is latched and B starts a turn, B takes over the latch, and A's later
  `turn.ended` is dropped. So bounce detection should look at all
  `agent_event` frames before the latch filter, even though the face only
  follows the latched session (as the robot does).
- **On the robot:** `turn.ended` plays a 3 s Celebrating overlay and a
  +0.7 / +0.9 emotion impulse. The Dock face will do the same once the
  behaviour layer is ported.

### Needs attention → optional second bounce

- **Claude:** `notification` whose `text` starts with `"Claude needs"`. This is
  the permission-prompt notification; the robot plays AttractingAttention for
  it.
- The idle notification `"Claude is waiting for your input"` is filtered out
  by the bridge (`claude.ts`, `case 'Notification'`).
- **Codex / OpenCode:** `permission.requested`.

### Not completion signals

- `subagent.finished`: a subagent ended, not the turn.
- `session.ended`: can arrive without a `turn.ended` before it.

---

## Known gaps in the event data

These affect the robot today. For now, copy them as-is.

### Claude hooks that become `unknown`

`plugin/hooks/hooks.json` registers many hooks, but `claude.ts` only handles:
`SessionStart`, `SessionEnd`, `UserPromptSubmit`, `PreToolUse`,
`PostToolUse`, `Stop`, `SubagentStop`, `PreCompact`, `Notification`.
Everything else becomes `{kind:"unknown"}`. The ones that matter for the face:

| Hook | Consequence today |
|---|---|
| `PermissionRequest` | No `permission.requested` for Claude, so `pending_permission` and the −0.6 valence driver never activate. This is intentional, per the comment in `claude.ts`: Claude has no hook for the verdict, so the state could never be cleared. |
| `PostToolUseFailure` | A failed tool never produces `activity.failed`, so the linger never starts. The face stays in Executing, then Straining, until the next event. |
| `StopFailure` | A turn that ends with an API error never produces `turn.ended`, so there's no Celebrating and **no bounce**. |

### Disconnects don't reset the face

The firmware clears the latch and per-turn state on disconnect, but not the
verb or emotion (`ENGINE.md` §2.7). On a Mac, sleep/wake and bridge restarts
are routine, so this will show up more often than on the robot. It's an easy
first fix later.

### Stale sessions

The bridge forgets a session 10 minutes after its last event
(`SESSION_STALE_MS`, `plugin/src/state.ts`). `active_sessions` lists sessions
in the order the bridge first saw them, so `ids[0]`, which the robot latches
when idle, may be an old session.

---

## Verifying against the robot

Because both clients receive the same broadcast, you can compare them side by
side:

1. Run the bridge, with the robot and the Mac app both connected.
2. Replay a recorded turn:
   - **from the face editor's Log Replay panel**, which posts each entry to
     `POST /hooksRaw/<agent>` with `{events:[…]}`; or
   - **with the hook simulator:**
     `BRIDGE_TOKEN=… BRIDGE_URL=http://127.0.0.1:8787 node scripts/simulate-hooks.js`.
     It sends a Claude-shaped sequence: prompt, 5 reads, 2 writes, a
     notification, Stop.
3. Watch both faces. They should go through the same verbs and emotions at the
   same times. Blink and glance timing is random, so that will differ.

For checking a single feature, the `/api/raw/*` routes trigger one thing at a
time (verbs, overlays, emotion nudges) on both clients. `GET /api/raw/capabilities`
lists them, and `docs/bridge/CURL_RECIPES.md` has examples.
