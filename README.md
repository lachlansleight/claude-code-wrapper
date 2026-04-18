# Claude Code Bridge

A Claude Code [channel plugin](https://code.claude.com/docs/en/channels-reference)
that exposes an HTTP REST API and a WebSocket hub. External clients can:

- inject messages into a running Claude Code session,
- subscribe to live events (Claude's replies, permission requests),
- approve or deny tool-use permission prompts remotely.

It is a single Node.js process: the MCP server (over stdio to Claude Code) and
the HTTP/WS server live together. One entry point. One port for clients.

## Prerequisites

- Claude Code 2.1.81+ (channels enabled for your org)
- Node.js 20+
- Logged in to claude.ai (channels do not work with raw API keys)

## Install

```bash
npm install
npm run build
```

## Configure

The bridge **will not start without** `BRIDGE_TOKEN`. This is intentional — it
is a shared secret that authorises clients to inject messages and approve tool
calls in your Claude Code session. Generate a strong value:

```bash
export BRIDGE_TOKEN="$(openssl rand -hex 32)"
```

Optional env vars:

| Variable          | Default       | Notes                                                                  |
|-------------------|---------------|------------------------------------------------------------------------|
| `BRIDGE_TOKEN`    | *(required)*  | Shared secret. Bridge refuses to start if unset.                       |
| `BRIDGE_PORT`     | `8787`        | HTTP + WS port.                                                        |
| `BRIDGE_HOST`     | `127.0.0.1`   | Bind address. Localhost only by default — see "Exposing remotely".     |
| `BRIDGE_LOG_FILE` | *(unset)*     | If set, mirror stderr logs to this file.                               |

## Register with Claude Code

Copy `.mcp.json.example` to `.mcp.json` (in your project, or `~/.claude/`),
fill in the absolute path to `dist/index.js`, and set `BRIDGE_TOKEN`:

```json
{
  "mcpServers": {
    "bridge": {
      "command": "node",
      "args": ["/absolute/path/to/claude-code-wrapper/dist/index.js"],
      "env": { "BRIDGE_TOKEN": "your-token-here" }
    }
  }
}
```

Launch Claude Code with the channel enabled:

```bash
claude --dangerously-load-development-channels server:bridge --channels server:bridge
```

Inside Claude Code, run `/mcp` and confirm `bridge` is connected.

## Test recipe (5 minutes)

Three terminals:

1. **Terminal A** — Claude Code, launched as above.
2. **Terminal B** — open `examples/ws-client.html` in a browser, paste your
   token, click **Connect**. (Or run `wscat -c "ws://127.0.0.1:8787/ws?token=$BRIDGE_TOKEN"`.)
3. **Terminal C** — send a message:

   ```bash
   curl -X POST "http://127.0.0.1:8787/api/messages" \
     -H "Authorization: Bearer $BRIDGE_TOKEN" \
     -H "Content-Type: application/json" \
     -d '{"content": "Hi from curl"}'
   ```

You should see Claude respond in Terminal A and the reply arrive on the WS
client in Terminal B. Now ask Claude to do something that triggers a tool
permission (`run ls`). The browser client will pop up an Allow/Deny card —
clicking either resolves it without touching the terminal dialog.

## HTTP API

All endpoints except `/api/health` require
`Authorization: Bearer $BRIDGE_TOKEN` (or `X-Bridge-Token: $BRIDGE_TOKEN`).
See [`examples/curl.md`](examples/curl.md) for full request/response examples.

| Method | Path                          | Purpose                                            |
|--------|-------------------------------|----------------------------------------------------|
| GET    | `/api/health`                 | Liveness check (unauthenticated).                  |
| POST   | `/api/messages`               | Inject a message into the Claude Code session.     |
| GET    | `/api/messages/:chat_id`      | Read the conversation log for a given chat.        |
| GET    | `/api/state`                  | Bridge state (chats, pending perms, uptime).       |
| GET    | `/api/permissions`            | List currently-pending permission requests.        |
| POST   | `/api/permissions/:id`        | Approve or deny a permission request.              |

`POST /api/permissions/:id` returns `applied: false` if Claude Code already
closed the request (terminal user answered first). The verdict was sent but
not used — this is normal, not an error.

## WebSocket API

Single endpoint: `ws://127.0.0.1:8787/ws?token=$BRIDGE_TOKEN`.

Browsers can't set headers on `WebSocket`, so the token is read from the
`token` query param OR the `Authorization` header. Bad token → close code
`4401`.

**Server → client** (all JSON, all have a `type`):

- `hello` — sent on connect: `{ type, client_id, server_version }`
- `inbound_message` — echo of a message that went to Claude
- `outbound_reply` — Claude called the `reply` tool
- `permission_request` — Claude Code asked for tool approval
- `permission_resolved` — a verdict was relayed (best-effort)
- `session_event` — connection lifecycle
- `pong` — reply to client `ping`
- `error` — malformed input

**Client → server:**

- `{ type: "send_message", content, chat_id?, meta? }` — same as `POST /api/messages`
- `{ type: "permission_verdict", request_id, behavior: "allow"|"deny" }`
- `{ type: "ping" }`

## Security notes

- **Localhost by default.** `BRIDGE_HOST` defaults to `127.0.0.1`. Anyone who
  can connect to the bridge AND knows the token can approve tool calls in
  your Claude Code session — this is, by design, not a sandbox.
- **Exposing remotely.** If you really need remote access, do *not* set
  `BRIDGE_HOST=0.0.0.0` directly to the internet. Tunnel it through SSH /
  WireGuard / Tailscale, or front it with a TLS-terminating reverse proxy
  that also enforces auth.
- **Token rotation.** Restart the bridge (and Claude Code, since it spawns the
  bridge as a subprocess) after changing the token.

## Troubleshooting

- **`/mcp` shows the bridge as failed / disconnected.** The most common cause
  is something on the bridge writing to stdout (which is the MCP transport).
  All logs in this codebase go to stderr; if you've added a dependency that
  prints to stdout, redirect or remove it. Check Claude Code's debug log at
  `~/.claude/debug/*.txt`.
- **Permission relay does nothing.** Confirm Claude Code is 2.1.81+ (the
  permission capability was wired up in that release). Confirm `/mcp` shows
  the bridge as connected. Confirm you launched with both
  `--dangerously-load-development-channels server:bridge` and
  `--channels server:bridge`.
- **Verdict returns `applied: false`.** Expected — the terminal user (or a
  faster remote client) answered first. First verdict wins.

## Project layout

```
src/
  index.ts    # entry — config, stdout redirect, wires MCP+HTTP+WS
  mcp.ts      # MCP server: channel capabilities, reply tool, permission relay
  http.ts     # HTTP REST API (Node builtin http)
  ws.ts       # WebSocket hub (ws library)
  bus.ts      # typed in-process EventEmitter
  state.ts    # in-memory chat log + pending-permission store
  auth.ts     # token validation
  logger.ts   # stderr logger (and optional file mirror)
  types.ts    # shared types
examples/
  curl.md         # curl recipes for every endpoint
  ws-client.html  # browser-based WS test client
.mcp.json.example # copy and edit to register the bridge with Claude Code
BUILD_BRIEF.md    # original design brief (authoritative spec)
```
