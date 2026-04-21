# Claude Code Bridge

A Claude Code [channel plugin](https://code.claude.com/docs/en/channels-reference)
that exposes an HTTP REST API and a WebSocket hub. External clients can:

- inject messages into a running Claude Code session,
- subscribe to live events (Claude's replies, permission requests, lifecycle hooks),
- approve or deny tool-use permission prompts remotely.

It is a single Node.js process: the MCP server (over stdio to Claude Code) and
the HTTP/WS server live together. One entry point. One port for clients.

## Repo layout

```
plugin/           # the installable plugin — Node project + manifests live here
  src/            # TypeScript sources
  dist/           # compiled output (run `npm run build` inside plugin/)
  .mcp.json       # registers the bridge as an MCP server
  hooks/          # hooks.json wiring every lifecycle hook to the bridge
  .claude-plugin/plugin.json
.claude-plugin/
  marketplace.json  # single-plugin marketplace manifest pointing at ./plugin
robot_experiment/   # ESP32 firmware: OLED dashboard + WS client
examples/           # curl recipes, browser WS client
example_esp32_client/     # older ESP32 Firebase-polling sketch
example_esp32_ws_client/  # older ESP32 raw WS sketch
```

## Prerequisites

- Claude Code 2.1.81+ (channels enabled for your org)
- Node.js 20+
- Logged in to claude.ai (channels do not work with raw API keys)

## Install

### 1. Build the plugin

```bash
cd plugin
npm install
npm run build
cd ..
```

This produces `plugin/dist/` which the plugin runtime and hooks reference via
`${CLAUDE_PLUGIN_ROOT}`.

### 2. Register the marketplace and install

The repo self-hosts as a single-plugin marketplace. From inside Claude Code
(CLI or the Code tab of Claude Desktop):

```
/plugin marketplace add C:/Users/you/path/to/claude-code-wrapper
/plugin install claude-code-bridge@claude-code-bridge-local
/reload-plugins
```

Use an absolute path with forward slashes. Git-Bash-style paths like
`/c/Users/...` get interpreted as git remotes and fail.

Confirm with `/mcp` — `bridge` should show as connected.

### 3. Refreshing after updates

Claude Code snapshots the plugin at install time (keyed by commit hash). After
pulling new changes or editing the plugin, re-snapshot with:

```
/plugin marketplace remove claude-code-bridge-local
/plugin marketplace add C:/Users/you/path/to/claude-code-wrapper
/reload-plugins
```

## Configure

The bridge **will not start without** `BRIDGE_TOKEN` — it is the shared secret
clients use to inject messages and approve tool calls.

### Recommended: `~/.claude/settings.json`

Claude Code merges the `env` block into MCP subprocesses and hook commands.
This scopes the token to Claude rather than polluting the system env.

```json
{
  "env": {
    "BRIDGE_TOKEN": "e0112a5b1f05",
    "BRIDGE_URL": "http://127.0.0.1:8787",
    "BRIDGE_LOG_FILE": "C:/Users/you/path/to/bridge.log",
    "BRIDGE_DEBUG_FRAMES": "1",
    "BRIDGE_DATABASE_URL": "https://your-project-default-rtdb.firebaseio.com/",
    "BRIDGE_AGENT_ID": "test-agent"
  }
}
```

(`BRIDGE_URL` is read by `hook-forward.js` when posting hook events back to the
bridge. The rest are consumed by the bridge process itself.)

### Alternative: system env vars

On Windows:

```
setx BRIDGE_TOKEN "your-token-here"
```

Close and reopen your terminal (and fully restart Claude Desktop via the tray
icon) so the new value is inherited.

### All env vars

| Variable              | Default       | Notes                                                                      |
|-----------------------|---------------|----------------------------------------------------------------------------|
| `BRIDGE_TOKEN`        | *(required)*  | Shared secret. Bridge refuses to start if unset.                           |
| `BRIDGE_PORT`         | `8787`        | HTTP + WS port (set in `plugin/.mcp.json`).                                |
| `BRIDGE_HOST`         | `0.0.0.0`     | Bind address (set in `plugin/.mcp.json`). `127.0.0.1` for localhost-only. |
| `BRIDGE_LOG_FILE`     | *(unset)*     | If set, mirror stderr logs to this file.                                   |
| `BRIDGE_DEBUG_FRAMES` | *(unset)*     | `1` → mirror every outbound MCP frame into `BRIDGE_LOG_FILE`.              |
| `BRIDGE_DATABASE_URL` | *(unset)*     | Firebase RTDB URL. Enables Firebase sync + `/api/firebaseData` proxy.      |
| `BRIDGE_AGENT_ID`     | *(unset)*     | Agent key under `<db>/agents/`. Required alongside `BRIDGE_DATABASE_URL`.  |
| `BRIDGE_URL`          | `http://127.0.0.1:8787` | Where `hook-forward.js` POSTs hook events.                       |
| `BRIDGE_HOOK_TIMEOUT_MS` | `500`      | Per-hook POST timeout. Never blocks your turn.                             |

## Quick smoke test

Two surfaces:

1. **Browser** — open `examples/ws-client.html`, paste your token, click
   **Connect**.
2. **Terminal** — send a message:

   ```bash
   curl -X POST "http://127.0.0.1:8787/api/messages" \
     -H "Authorization: Bearer $BRIDGE_TOKEN" \
     -H "Content-Type: application/json" \
     -d '{"content": "Hi from curl"}'
   ```

Claude responds in Claude Code and the reply arrives on the WS client. Ask
Claude to run `ls` and the browser will pop up an Allow/Deny card that
resolves without touching the terminal dialog.

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
| POST   | `/api/hook-event`             | Relay a Claude Code hook event.                    |
| GET    | `/api/firebaseData`           | Proxy current agent state from Firebase.           |

`POST /api/permissions/:id` returns `applied: false` if Claude Code already
closed the request (terminal user answered first). The verdict was sent but
not used — this is normal, not an error.

## WebSocket API

Single endpoint: `ws://127.0.0.1:8787/ws?token=$BRIDGE_TOKEN`.

Browsers can't set headers on `WebSocket`, so the token is read from the
`token` query param OR the `Authorization` header. Bad token → close code
`4401`.

**Server → client** (all JSON, all have a `type`):

- `hello` — `{ type, client_id, server_version }`
- `inbound_message` — echo of a message that went to Claude
- `outbound_reply` — Claude called the `reply` tool
- `permission_request` — Claude Code asked for tool approval
- `permission_resolved` — a verdict was relayed (best-effort)
- `hook_event` — Claude Code lifecycle hook (see below)
- `session_event` — connection lifecycle
- `pong` — reply to client `ping`
- `error` — malformed input

**Client → server:**

- `{ type: "send_message", content, chat_id?, meta? }` — same as `POST /api/messages`
- `{ type: "permission_verdict", request_id, behavior: "allow"|"deny" }`
- `{ type: "ping" }`

## Lifecycle hooks

The channel only surfaces `reply` tool calls and permission prompts. The
plugin additionally wires every Claude Code hook
(`UserPromptSubmit`, `PreToolUse`, `PostToolUse`, `Notification`, `Stop`,
`SubagentStop`, `SessionStart`, `SessionEnd`, `PreCompact`) to a small helper
at `plugin/dist/hook-forward.js`.

The helper reads the hook payload from stdin, POSTs it to
`$BRIDGE_URL/api/hook-event` with `$BRIDGE_TOKEN`, and always exits 0 — so a
down bridge never blocks your turn. Failures log to stderr (visible in Claude
Code's debug output).

Events arrive on WS clients as `{ type: "hook_event", hook_type, payload, ts }`
and are broadcast only — they are not stored in bridge state.

The hooks are registered automatically when the plugin is installed; there is
nothing to wire up by hand.

## Firebase sync (optional)

If `BRIDGE_DATABASE_URL` and `BRIDGE_AGENT_ID` are set, the bridge writes agent
state to Firebase Realtime Database via its REST API (no SDK). Keys under
`<db>/agents/<id>/`:

- `lastAwake` — timestamp on bridge startup
- `working` — `true` on any non-idle hook, `false` on `Stop` / `SessionEnd`
- `lastMessage` — `{ summary, blocks }` from the last `Stop` hook's assistant text
- `preToolUse` / `postToolUse` — raw hook payloads
- `permissionRequest` — payload on request, `null` on resolve

The bridge also exposes `GET /api/firebaseData`, which proxies
`<db>/agents/<id>.json` over HTTPS so low-powered clients on the LAN (e.g. the
sketch in [`example_esp32_client/`](example_esp32_client/)) don't need to pin
a root CA themselves. Requires `BRIDGE_TOKEN` auth like other endpoints.

## ESP32 firmware

[`robot_experiment/`](robot_experiment/) is an Arduino sketch for an ESP32
with a 128×32 SSD1306 OLED. It connects to the bridge over WebSocket and
renders a live dashboard: WiFi status, bridge connection, spinner while Claude
is working, last message summary, current tool + argument, and inline
permission prompts.

See `robot_experiment/config.example.h` for setup, and
[`robot_experiment/TOOL_DISPLAY.md`](robot_experiment/TOOL_DISPLAY.md) for the
tool label / detail mapping.

## Security notes

- **Bind address.** `plugin/.mcp.json` defaults `BRIDGE_HOST=0.0.0.0` to
  support LAN clients (including the ESP32). Anyone who can reach the port AND
  knows the token can approve tool calls. For localhost-only, override
  `BRIDGE_HOST=127.0.0.1` in `~/.claude/settings.json` → `env`.
- **Do not expose directly to the internet.** Tunnel through SSH / WireGuard /
  Tailscale, or front it with a TLS-terminating reverse proxy that also
  enforces auth.
- **Token rotation.** Restart Claude Code (which spawns the bridge as a
  subprocess) after changing the token.

## Troubleshooting

- **`/mcp` shows bridge failed / disconnected.** The most common cause is
  something writing to stdout (the MCP transport). All logs here go to stderr.
  Check `~/.claude/debug/*.txt`.
- **Hooks don't fire / OLED doesn't update.** The plugin cache is pinned to a
  commit. After pulling or editing, remove + re-add the marketplace (see
  *Refreshing after updates* above).
- **Install fails with `plugins.0.source: Invalid input`.** The marketplace
  manifest's `source` field needs a relative path like `./plugin`. Plain `.`
  is rejected.
- **Marketplace add tries to clone via git.** You passed a Git-Bash-style path
  (`/c/...`). Use a Windows absolute path with forward slashes
  (`C:/Users/.../claude-code-wrapper`).
- **Permission relay does nothing.** Confirm Claude Code is 2.1.81+. Confirm
  `/mcp` shows bridge connected.
- **Verdict returns `applied: false`.** Expected — the terminal user (or a
  faster remote client) answered first. First verdict wins.

## Further reading

- [`BUILD_BRIEF.md`](BUILD_BRIEF.md) — original design brief, authoritative on
  architecture and scope.
- [`CLAUDE.md`](CLAUDE.md) — condensed guide for Claude Code when working on
  this repo.
