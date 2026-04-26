# Agent Bridge

A standalone Node service that ingests lifecycle hooks from agentic coding
tools (Claude Code, OpenAI Codex, Cursor, OpenCode), classifies them into a
shared event vocabulary, and broadcasts the result over WebSocket as a
single typed `agent_event` stream.

The bridge has **no opinion about presentation**. It does not derive
"thinking" / "idle" / "blocked" states. Those decisions live in the
consumer — the ESP32 firmware in [`robot_v2/`](robot_v2/), a dashboard,
or anything else that subscribes.

See [`plugin/src/OBJECT_INTERFACE.md`](plugin/src/OBJECT_INTERFACE.md) for
the full event vocabulary.

## Architecture

```
   ┌──────────────┐  POST /hooks/<agent>     ┌──────────────────────────┐
   │  Claude Code │ ───────────────────────► │                          │
   │  Codex       │                          │     agent-bridge         │
   │  Cursor      │                          │  (this Node service)     │
   │  OpenCode    │                          │                          │
   └──────────────┘                          │   parse → AgentEvent     │
                                             │   broadcast envelopes    │
                                             └─────────────┬────────────┘
                                                           │ WS
                                                           ▼
                                                ┌────────────────────┐
                                                │  robot, dashboard, │
                                                │  any subscriber    │
                                                └────────────────────┘
```

The Claude Code plugin in this repo *only* registers hooks; the actual
server runs separately (`node plugin/dist/index.js`).

## Repo layout

```
plugin/
  src/
    index.ts                 # entry — HTTP + WS + Firebase
    http.ts                  # /hooks/:agent + /api/* surface
    ws.ts                    # WebSocket hub
    bus.ts                   # in-process event bus
    state.ts                 # in-memory chat / permission / session store
    agent-event.ts           # canonical AgentEvent type vocabulary
    activity-classify.ts     # tool name → ActivityKind table
    activity-summary.ts      # ActivityRef.summary builder
    adapters/                # per-agent parsers (claude/codex/cursor/opencode)
    hook-forward.ts          # Claude-side stdin→HTTP forwarder
    auth.ts, logger.ts, firebase.ts, types.ts
    OBJECT_INTERFACE.md      # spec for the AgentEvent vocabulary
  hooks/hooks.json           # Claude Code hook wiring
  .claude-plugin/plugin.json # Claude Code plugin manifest
.claude-plugin/marketplace.json
robot_experiment/, robot_v2/  # ESP32 firmware
examples/                     # curl recipes + browser WS client
helpers/                      # forwarder scripts for Codex / Cursor / OpenCode
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

For local development, copy `plugin/src/.env.example` to
`plugin/src/.env` (gitignored) and fill in your values:

```bash
cp plugin/src/.env.example plugin/src/.env
cd plugin && npm run dev
```

Both `npm run dev` and `npm start` auto-load that file. Real shell
exports always override values from the file.

`BRIDGE_TOKEN` is required — the bridge refuses to start without one.
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
| `BRIDGE_URL`               | `http://127.0.0.1:8787`  | Where forwarder scripts POST hook events.               |
| `BRIDGE_HOOK_TIMEOUT_MS`   | `500`                    | Per-hook POST timeout. Never blocks an agent turn.      |

## Wire up your agent

Each agent posts hook events to `POST /hooks/<agent>` with body
`{ "hook_type": "...", "payload": <agent-native-payload> }`. See the
per-agent guides:

- [Claude Code](GETTING_STARTED_CLAUDE.md)
- [Codex CLI](GETTING_STARTED_CODEX.md)
- [Cursor 1.7+](GETTING_STARTED_CURSOR.md)
- [OpenCode](GETTING_STARTED_OPENCODE.md)

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
| GET    | `/api/state`                | Bridge state (chats, pending permissions, uptime).        |
| GET    | `/api/sessions`             | Active session ids (last 10 minutes of activity).         |
| GET    | `/api/permissions`          | Currently-pending permission requests.                    |
| POST   | `/api/permissions/:id`      | Resolve a pending permission locally + broadcast.         |
| GET    | `/api/firebaseData`         | Proxy the agent's Firebase RTDB record.                   |

### Note on permission verdicts

Without an MCP channel back to the agent CLI, `POST /api/permissions/:id`
**cannot actually approve or deny a tool call** in the agent. It only
clears the local pending entry and broadcasts a `permission_resolved`
event. The agent's own terminal prompt still has to be answered.

## WebSocket API

Endpoint: `ws://127.0.0.1:8787/ws?token=$BRIDGE_TOKEN`. Bad token → close
code `4401`.

**Server → client** (all JSON, all have `type`):

| `type`                | Notes                                                              |
|-----------------------|--------------------------------------------------------------------|
| `hello`               | First frame: `{ client_id, server_version }`                       |
| `active_sessions`     | Current session id list                                            |
| `agent_event`         | Classified `AgentEventEnvelope` — see [OBJECT_INTERFACE.md](plugin/src/OBJECT_INTERFACE.md) |
| `inbound_message`     | Echo of `POST /api/messages`                                       |
| `permission_request`  | Pending-permission state mirror (also surfaces as `agent_event`)   |
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

Each `agent_event` envelope looks like:

```ts
{
  type: 'agent_event',
  agent: 'claude' | 'cursor' | 'codex' | 'opencode',
  ts: number,
  session_id?: string,
  turn_id?: string,
  event: AgentEvent,             // discriminated union — see spec
  raw: { hook_type, payload },   // unmodified vendor payload
}
```

## Smoke test

1. Start the bridge: `BRIDGE_TOKEN=test BRIDGE_HOST=127.0.0.1 node dist/index.js`.
2. Open `examples/ws-client.html`, paste `test`, click Connect.
3. Hand-fire a hook:
   ```bash
   curl -X POST "http://127.0.0.1:8787/hooks/claude" \
     -H "Authorization: Bearer test" \
     -H "Content-Type: application/json" \
     -d '{"hook_type":"UserPromptSubmit","payload":{"session_id":"sess1","prompt":"hello"}}'
   ```
   The browser should receive an `agent_event` with `event.kind:
   "turn.started"` followed by `event.kind: "message.user"`.
4. Repeat with `PreToolUse` + `tool_name: "Read"` →
   `event.kind: "activity.started"` with `activity.kind: "file.read"`.
5. `Stop` → `event.kind: "turn.ended"`.

## Firebase sync (optional)

If `BRIDGE_DATABASE_URL` and `BRIDGE_AGENT_ID` are set, the bridge mirrors
key state to `<db>/agents/<id>/`:

- `lastAwake` — bridge startup timestamp
- `working` — true while a turn is in progress
- `lastMessage` — `{ summary, blocks }` from the final assistant reply
- `preToolUse` / `postToolUse` — `{ tool, kind, summary }` from the most
  recent activity
- `permissionRequest` — current pending request, `null` on resolve

`GET /api/firebaseData` proxies the agent's record so low-powered LAN
clients don't need to pin Firebase's TLS cert.

## ESP32 firmware

[`robot_v2/`](robot_v2/) is the in-progress firmware client (successor to
[`robot_experiment/`](robot_experiment/)). Personality state derivation
lives there now — the bridge only emits raw lifecycle events.

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

## Further reading

- [`plugin/src/OBJECT_INTERFACE.md`](plugin/src/OBJECT_INTERFACE.md) —
  canonical event vocabulary spec.
- [`CLAUDE.md`](CLAUDE.md) — orientation notes for Claude Code sessions
  working on this repo.
