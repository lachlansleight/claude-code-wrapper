# Agent Bridge

A standalone Node service that ingests lifecycle hooks from agentic coding
tools (Claude Code, OpenAI Codex, Cursor, OpenCode) and broadcasts a
unified event stream over WebSocket — both the raw hook payloads and a
derived high-level **personality state** (`idle` / `thinking` / `reading`
/ `writing` / `finished` / `excited` / `ready` / `waking` / `sleep` /
`blocked`).

The original target consumer is the ESP32 firmware in
[`robot_experiment/`](robot_experiment/) (and its `robot_v2/` successor),
but anything that speaks WebSocket — dashboards, mobile clients, Stream
Deck plugins — can subscribe.

## Architecture

```
   ┌──────────────┐  POST /hooks/<agent>     ┌──────────────────────────┐
   │  Claude Code │ ───────────────────────► │                          │
   │  Codex       │                          │     agent-bridge         │
   │  Cursor      │                          │  (this Node service)     │
   │  OpenCode    │                          │                          │
   └──────────────┘                          │   normalize → state      │
                                             │   raw hook + state_event │
                                             └─────────────┬────────────┘
                                                           │ WS
                                                           ▼
                                                ┌────────────────────┐
                                                │  robot, dashboard, │
                                                │  any subscriber    │
                                                └────────────────────┘
```

This used to be a Claude Code MCP/channel plugin — `bridge` ran as a
subprocess of Claude Code and spoke the `claude/channel` protocol. That's
gone. The new service is plain HTTP/WS, started by the user (manually,
systemd, launchd, or whatever you prefer). The Claude Code plugin in this
repo *only* registers hooks now; the actual server runs separately.

## Repo layout

```
plugin/
  src/
    index.ts                 # entry — HTTP + WS + Firebase + personality
    http.ts                  # /hooks/:agent + /api/* surface
    ws.ts                    # WebSocket hub
    bus.ts                   # in-process event bus
    state.ts                 # in-memory chat / permission / session store
    personality.ts           # high-level state machine
    adapters/
      types.ts               # NormalizedHook + ToolAccess + PersonalityState
      claude.ts              # Claude Code hook payloads
      codex.ts               # Codex CLI hook payloads
      cursor.ts              # Cursor 1.7+ hook payloads
      opencode.ts            # OpenCode plugin hook events
      index.ts               # adapter registry
    hook-forward.ts          # Claude-side stdin→HTTP forwarder
    auth.ts, logger.ts, firebase.ts, types.ts
  hooks/hooks.json           # Claude Code hook wiring (lives with the plugin)
  .claude-plugin/plugin.json # Claude Code plugin manifest (hooks only — no MCP)
.claude-plugin/marketplace.json
robot_experiment/, robot_v2/  # ESP32 firmware
examples/                     # curl + browser WS client
```

## Run the bridge

```bash
cd plugin
npm install
npm run build
BRIDGE_TOKEN="$(openssl rand -hex 32)" \
BRIDGE_HOST=0.0.0.0 \
BRIDGE_PORT=8787 \
node dist/index.js
```

For local development, use the hot-reload script — `tsx` runs the
TypeScript directly and restarts the process on any change under
`src/`. Agent CLIs and the robot stay connected via their own
reconnect logic; you only need to restart them if you change *their*
config:

```bash
BRIDGE_TOKEN=dev npm run dev
```

`BRIDGE_TOKEN` is required — the bridge refuses to start without one. All
HTTP and WS traffic must present it.

`BRIDGE_HOST` defaults to `127.0.0.1`. Set it to `0.0.0.0` if a LAN device
(like the ESP32) needs to reach it.

| Variable                   | Default                  | Purpose                                                |
|----------------------------|--------------------------|--------------------------------------------------------|
| `BRIDGE_TOKEN`             | *(required)*             | Shared secret. Server will not start without it.       |
| `BRIDGE_PORT`              | `8787`                   | HTTP + WS port.                                         |
| `BRIDGE_HOST`              | `127.0.0.1`              | Bind address. `0.0.0.0` for LAN access.                |
| `BRIDGE_LOG_FILE`          | *(unset)*                | Mirror stderr logs to this file.                        |
| `BRIDGE_DATABASE_URL`      | *(unset)*                | Firebase RTDB URL. Enables Firebase sync.               |
| `BRIDGE_AGENT_ID`          | *(unset)*                | Agent key under `<db>/agents/`. Required with the URL.  |
| `BRIDGE_URL`               | `http://127.0.0.1:8787`  | Where `hook-forward.js` POSTs hook events.              |
| `BRIDGE_HOOK_TIMEOUT_MS`   | `500`                    | Per-hook POST timeout. Never blocks an agent turn.      |

## Wire up your agent

Each agent posts hook events to `POST /hooks/<agent>` with body
`{ "hook_type": "...", "payload": <agent-native-payload> }`.

### Claude Code

The plugin in `plugin/` already wires every hook
(`UserPromptSubmit`, `PreToolUse`, `PostToolUse`, `Notification`, `Stop`,
`SubagentStop`, `SessionStart`, `SessionEnd`, `PreCompact`) through
`plugin/dist/hook-forward.js`, which posts to `$BRIDGE_URL/api/hook-event`
(an alias for `/hooks/claude`). Install it via the local marketplace:

```
/plugin marketplace add C:/path/to/claude-code-wrapper
/plugin install claude-code-bridge@claude-code-bridge-local
/reload-plugins
```

Set `BRIDGE_TOKEN` in `~/.claude/settings.json` so the hook commands
inherit it:

```json
{
  "env": {
    "BRIDGE_TOKEN": "your-token-here",
    "BRIDGE_URL": "http://127.0.0.1:8787"
  }
}
```

### Codex CLI

Codex uses Claude-compatible hook names. Add a `~/.codex/hooks.json`
that runs a small forwarder for each event, posting to `/hooks/codex`.
Sketch:

```bash
curl -s -X POST "http://127.0.0.1:8787/hooks/codex" \
  -H "Authorization: Bearer $BRIDGE_TOKEN" \
  -H "Content-Type: application/json" \
  -d "{\"hook_type\":\"PreToolUse\",\"payload\":$(cat -)}"
```

### Cursor 1.7+

Cursor hooks (`beforeShellExecution`, `beforeMCPExecution`, `beforeReadFile`,
`afterFileEdit`, `stop`, …) read JSON on stdin. Wire each event in
`~/.cursor/hooks.json` to a script that posts to `/hooks/cursor` with
`hook_type` set to the event name.

### OpenCode

OpenCode plugin events (`tool.execute.before`, `tool.execute.after`,
`session.start`, etc.) — wire a small TS plugin that calls the bridge:

```ts
fetch("http://127.0.0.1:8787/hooks/opencode", {
  method: "POST",
  headers: { "Authorization": `Bearer ${process.env.BRIDGE_TOKEN}`, "Content-Type": "application/json" },
  body: JSON.stringify({ hook_type: "tool.execute.before", payload: ctx }),
})
```

## HTTP API

All endpoints except `/api/health` require
`Authorization: Bearer $BRIDGE_TOKEN` (or `X-Bridge-Token: $BRIDGE_TOKEN`).

| Method | Path                        | Purpose                                                   |
|--------|-----------------------------|-----------------------------------------------------------|
| GET    | `/api/health`               | Liveness; lists registered agent adapters.                |
| POST   | `/hooks/:agent`             | Ingest a lifecycle hook from a named agent.               |
| POST   | `/api/hook-event`           | Legacy alias for `/hooks/claude`.                         |
| POST   | `/api/messages`             | Inject a message (broadcast on WS only — no MCP delivery).|
| GET    | `/api/messages/:chat_id`    | Read message log for a chat.                              |
| GET    | `/api/state`                | Bridge state including `personality.state`.               |
| GET    | `/api/sessions`             | Active session ids (last 10 minutes of activity).         |
| GET    | `/api/permissions`          | Currently-pending permission requests.                    |
| POST   | `/api/permissions/:id`      | Resolve a pending permission locally + broadcast.         |
| GET    | `/api/firebaseData`         | Proxy the agent's Firebase RTDB record.                   |

### Note on permission verdicts

Without an MCP channel back to Claude Code, `POST /api/permissions/:id`
**cannot actually approve or deny a tool call** in the agent. It only
clears the local pending entry and broadcasts a `permission_resolved`
event. The agent's own terminal prompt still has to be answered. Cursor,
Codex, and OpenCode also lack a remote-verdict primitive today.

## WebSocket API

Endpoint: `ws://127.0.0.1:8787/ws?token=$BRIDGE_TOKEN`. Bad token → close
code `4401`.

**Server → client** (all JSON, all have `type`):

| `type`                | Notes                                                              |
|-----------------------|--------------------------------------------------------------------|
| `hello`               | First frame: `{ client_id, server_version }`                       |
| `active_sessions`     | Current session id list                                            |
| `state_event`         | `{ state, prev, ts }` — personality transition (sent on connect too) |
| `hook_event`          | `{ agent, hook_type, payload, ts }` — raw normalized envelope      |
| `inbound_message`     | Echo of `POST /api/messages`                                       |
| `permission_request`  | Adapter detected a permission ask                                  |
| `permission_resolved` | Best-effort verdict broadcast                                      |
| `pong`                | Reply to client `ping`                                             |
| `error`               | Malformed input                                                    |

**Client → server:**

```ts
{ type: "send_message", content, chat_id?, meta? }
{ type: "permission_verdict", request_id, behavior: "allow"|"deny" }
{ type: "request_sessions" }
{ type: "ping" }
```

## Personality state machine

`personality.ts` is a TS port of `robot_v2/Personality.cpp`. It owns the
state graph (`idle ↔ thinking ↔ reading|writing ↔ finished ↔ excited ↔
ready ↔ idle ↔ sleep`, plus a one-shot `waking` beat and a polled
`blocked` overlay while a permission is pending). Every NormalizedHook
flows through `handleHook()`; the resulting transition fires a
`state_event`.

Tunables (line up with the firmware):
- Tool linger after `post_tool`: 1000ms before falling back to `thinking`.
- `finished` minimum 1500ms; transitions to `excited` afterwards.
- `excited` lasts 10s, decays to `ready`; `ready` lasts 60s, decays to `idle`.
- `idle` decays to `sleep` after 30 min.
- `waking` is a fixed 1s beat after a `session_start` from `sleep`.

## Smoke test

Two terminals + browser:

1. Start the bridge: `BRIDGE_TOKEN=test BRIDGE_HOST=127.0.0.1 node dist/index.js`.
2. Open `examples/ws-client.html`, paste `test`, click Connect.
3. Hand-fire a hook:
   ```bash
   curl -X POST "http://127.0.0.1:8787/hooks/claude" \
     -H "Authorization: Bearer test" \
     -H "Content-Type: application/json" \
     -d '{"hook_type":"UserPromptSubmit","payload":{"session_id":"sess1"}}'
   ```
   The browser should receive both a `hook_event` and a `state_event`
   with `state: "thinking"`.
4. Repeat with `PreToolUse` + `tool_name: "Read"` → `state_event` with
   `state: "reading"`.
5. With `tool_name: "Write"` → `writing`. Then `Stop` → `finished` →
   `excited` → `ready` → `idle`.

## Firebase sync (optional)

If `BRIDGE_DATABASE_URL` and `BRIDGE_AGENT_ID` are set, hook events
mirror to `<db>/agents/<id>/`:

- `lastAwake` — bridge startup timestamp
- `working` — `true` between user prompt and `Stop`/`SessionEnd`
- `lastMessage` — `{ summary, blocks }` from a Stop hook's
  `assistant_text` (Claude only)
- `preToolUse` / `postToolUse` — raw hook payloads
- `permissionRequest` — payload on request, `null` on resolve

`GET /api/firebaseData` proxies the agent's record so low-powered LAN
clients don't need to pin Firebase's TLS cert.

## ESP32 firmware

[`robot_experiment/`](robot_experiment/) (and the in-progress
[`robot_v2/`](robot_v2/) successor) is the canonical client. It connects
to the bridge over WebSocket and renders a live dashboard. The firmware
currently consumes `hook_event` payloads directly; the new `state_event`
is additive, so existing behaviour is unchanged. A future cleanup can
switch the firmware to subscribe to `state_event` and drop its local
mapping.

## Security

- **Bind address.** Default `127.0.0.1`. Anyone who can reach the port
  AND knows the token can drive your robot — only expose to LAN with
  intent.
- **Don't expose to the internet directly.** Tunnel via SSH / WireGuard /
  Tailscale, or front it with a TLS-terminating reverse proxy.
- **Token rotation.** Restart the bridge.

## Troubleshooting

- **No hook events arriving.** Confirm the bridge is running
  (`curl http://127.0.0.1:8787/api/health`). Then check `BRIDGE_LOG_FILE`
  for the agent's POSTs. The Claude side logs hook failures to stderr,
  visible in `~/.claude/debug/*.txt`.
- **Plugin install fails with `plugins.0.source: Invalid input`.** The
  marketplace `source` must be a relative path like `./plugin`. Plain
  `.` is rejected.
- **Marketplace add tries to clone via git.** You passed a Git-Bash-style
  path (`/c/...`). Use a Windows absolute path with forward slashes
  (`C:/Users/.../claude-code-wrapper`).
- **State stuck at `sleep`.** Send a `SessionStart` or any hook event;
  the machine starts in `sleep` and routes through `waking` on the
  first activity.

## Further reading

- [`BUILD_BRIEF.md`](BUILD_BRIEF.md) — historical design brief from when
  this was a Claude-Code-only MCP plugin. Architecture has shifted; see
  this README first.
- [`CLAUDE.md`](CLAUDE.md) — orientation notes for Claude Code sessions
  working on this repo.
