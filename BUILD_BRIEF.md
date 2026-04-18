# Claude Code Bridge — Build Brief

## What you're building

A **Claude Code channel plugin** that doubles as a **Node.js server** for external apps. It's a single Node process that:

1. Speaks the Claude Code channel protocol over stdio (as an MCP server) so Claude Code can spawn it and push events through it.
2. Exposes an **HTTP REST API** for injecting messages, querying state, and approving/denying permission requests.
3. Exposes a **WebSocket pub/sub hub** for live event broadcasting to connected clients and for low-latency inbound messages.
4. Relays **permission prompts** from Claude Code to connected clients, and routes verdicts back.

The user has explicitly chosen an **all-in-one architecture**: the MCP plugin IS the Node.js server. One process, one entry point. Don't split it into a "bridge plugin + separate server" design.

## Why this shape

The user is building an integration layer so external apps (dashboards, bots, mobile clients, automation) can observe and interact with Claude Code sessions in real time. They want:

- Outbound: any event Claude Code emits → broadcast to connected clients
- Inbound: any HTTP/WS client → can inject a message into the running Claude Code session as if the user typed it
- Permissions: clients can approve/deny tool-use prompts remotely (this is "the whole point" per the user)

They considered hooks (observability only), the Agent SDK (requires spawning Claude yourself), tmux keystroke injection (hacky), and Remote Control (doesn't support desktop app yet). The Channel SDK is the right answer because it's Anthropic's official extension point for exactly this use case.

## Canonical protocol reference

Before writing any code, read this page end-to-end:

**https://code.claude.com/docs/en/channels-reference**

It's the authoritative spec. The TL;DR protocol shape:

| Direction | Mechanism |
|---|---|
| Plugin → Claude (normal message) | `mcp.notification({ method: 'notifications/claude/channel', params: { content, meta } })` |
| Claude → Plugin (reply) | Claude calls your registered `reply` tool via `CallToolRequestSchema` |
| Claude Code → Plugin (perm request) | Claude Code sends `notifications/claude/channel/permission_request` (handle via `setNotificationHandler`) |
| Plugin → Claude Code (perm verdict) | `mcp.notification({ method: 'notifications/claude/channel/permission', params: { request_id, behavior: 'allow'|'deny' } })` |

**Capabilities required in the `Server` constructor:**
```js
capabilities: {
  experimental: {
    'claude/channel': {},              // required — registers channel listener
    'claude/channel/permission': {},   // required — opts into permission relay
  },
  tools: {},                            // required — enables reply tool
}
```

**Also read (skim, don't memorize):**
- https://code.claude.com/docs/en/channels — the user-facing channels overview, install/enable flags
- https://code.claude.com/docs/en/plugins — plugin packaging format (needed for the wrapper)
- The fakechat reference implementation: https://github.com/anthropics/claude-plugins-official/tree/main/external_plugins/fakechat — this is the closest analog to what you're building. Read its source.

---

## Requirements

### Runtime & dependencies

- **Node.js** (user specified Node, not Bun). The reference fakechat plugin uses Bun but the MCP SDK is Node-compatible. Use Node 20+.
- **TypeScript**, compiled. Ship with a `build` script and a `start` script that runs the compiled output.
- Dependencies (bare minimum):
  - `@modelcontextprotocol/sdk` — the MCP server
  - `ws` — WebSocket server
  - `zod` — for parsing the permission_request notification (the docs example uses this)
- Don't pull in Express, Fastify, Koa, etc. Use the Node builtin `http` module. This thing should be small and boring.

### Constraints from the channel protocol (don't get these wrong)

1. **Stdio transport is the ONLY transport to Claude Code.** Your MCP server connects over stdio. Never write anything to stdout except MCP protocol messages — no `console.log`, no stray prints. Logging goes to stderr (`console.error`) or a file. Stray stdout WILL disconnect the server (there's a fixed Claude Code bug in the changelog about this exact thing).

2. **Sender gating is NOT optional when permission relay is enabled.** The docs are explicit: *"Only declare the capability if your channel authenticates the sender, because anyone who can reply through your channel can approve or deny tool use in your session."* Implement a shared-secret token for both HTTP and WebSocket. Read it from an env var (`BRIDGE_TOKEN`) or a config file. Reject unauthenticated clients with 401/close-4401.

3. **The permission request ID is a 5-letter lowercase ID, `[a-km-z]`** (skips `l` so it doesn't look like `1` or `I`). The local terminal dialog does NOT show this ID — your server is the only way clients learn it. Include it in every outbound permission event.

4. **First verdict wins.** If the terminal user approves before a remote client does, Claude Code silently drops the remote verdict. That's fine — just don't treat "verdict sent but not applied" as an error.

5. **Meta keys must be identifiers** (letters, digits, underscores). Hyphens and other chars get silently dropped. Use `chat_id`, `client_id`, `severity` — not `chat-id`.

6. **Each `meta` entry becomes a `<channel>` tag attribute.** So `meta: { chat_id: "abc" }` becomes `<channel source="bridge" chat_id="abc">...</channel>` in Claude's context. Include a `chat_id` on every inbound message so Claude knows which conversation to reply to — the `instructions` string tells Claude to pass `chat_id` back to the `reply` tool.

### Project layout

```
claude-code-bridge/
├── package.json
├── tsconfig.json
├── README.md                  # user-facing: install, run, env vars, API reference
├── plugin.json                # plugin manifest (for packaging as a plugin)
├── .mcp.json                  # example MCP registration for users to copy
├── src/
│   ├── index.ts               # entry point — wires everything together
│   ├── mcp.ts                 # MCP server: capabilities, reply tool, permission handler
│   ├── http.ts                # HTTP REST API (Node builtin http)
│   ├── ws.ts                  # WebSocket hub (ws library)
│   ├── bus.ts                 # in-process event bus (EventEmitter) that ties it all together
│   ├── auth.ts                # shared-secret token validation
│   ├── state.ts               # session/chat state store (in-memory Map)
│   └── types.ts               # shared TypeScript types
└── examples/
    ├── curl.md                # curl examples for every endpoint
    └── ws-client.html         # minimal browser WS client for testing
```

### Internal architecture (one process, event-driven)

```
                         ┌─────────────────────────────┐
                         │       Event Bus (bus.ts)     │
                         │       EventEmitter           │
                         └───┬───────┬───────┬──────────┘
                             │       │       │
          emits / listens    │       │       │  emits / listens
                ┌────────────┘       │       └────────────┐
                ▼                    ▼                    ▼
         ┌────────────┐      ┌────────────┐       ┌────────────┐
         │  mcp.ts    │      │  http.ts   │       │   ws.ts    │
         │            │      │            │       │            │
         │ stdio to   │      │ REST on    │       │ WS on      │
         │ Claude     │      │ :8787/api  │       │ :8787/ws   │
         │ Code       │      │            │       │            │
         └────────────┘      └────────────┘       └────────────┘
                                   ▲                    ▲
                                   │                    │
                                   └────────┬───────────┘
                                            │
                                       external clients
```

**Event bus events** (bus.ts defines these as typed events):

- `inbound_message` — a client POST/WS wants to inject a message
  - payload: `{ content: string, chat_id?: string, client_id: string, meta?: Record<string, string> }`
  - consumed by: mcp.ts (emits `notifications/claude/channel`)
- `outbound_reply` — Claude called the reply tool
  - payload: `{ chat_id: string, text: string }`
  - consumed by: ws.ts (broadcast), http.ts (SSE endpoint if you add one)
- `permission_request` — Claude Code asked for approval
  - payload: `{ request_id, tool_name, description, input_preview }`
  - consumed by: ws.ts (broadcast), http.ts (stored in pending-requests state for GET)
- `permission_verdict` — a client approved/denied
  - payload: `{ request_id, behavior: 'allow' | 'deny', client_id }`
  - consumed by: mcp.ts (emits verdict notification)
- `claude_message` — a non-reply channel message from Claude (when/if observed)
  - optional; mainly for parity
- `session_meta` — session started, session state changed

### HTTP REST API

All endpoints require `Authorization: Bearer <BRIDGE_TOKEN>` (or `X-Bridge-Token: <token>`, either works). Return 401 on missing/invalid token.

```
POST /api/messages
  body: { content: string, chat_id?: string, meta?: Record<string,string> }
  → injects the content into the Claude Code session
  → returns: { chat_id, status: "forwarded" }
  → chat_id is generated server-side if not provided
  → meta is merged with server-generated meta (client_id, source)

GET /api/messages/:chat_id
  → returns the conversation log for a given chat_id
  → { chat_id, messages: [ { direction, content, ts, ... } ] }

GET /api/state
  → returns current bridge state: connected clients count, active chats, pending permission requests
  → { chats: [...], pending_permissions: [...], ws_clients: N, uptime_seconds: N }

GET /api/permissions
  → returns all currently-pending permission requests
  → [ { request_id, tool_name, description, input_preview, opened_at } ]

POST /api/permissions/:request_id
  body: { behavior: "allow" | "deny" }
  → emits the verdict back to Claude Code
  → returns: { request_id, behavior, applied: bool }
  → applied is best-effort; Claude Code may have already closed the request (terminal user answered first). Surface this honestly, don't pretend it worked.

GET /api/health
  → { ok: true, version, uptime_seconds } — unauthenticated, for liveness checks
```

### WebSocket API

Single endpoint: `ws://localhost:8787/ws?token=<BRIDGE_TOKEN>` (accept token via query param OR `Authorization` header during upgrade — query param is needed because browser `WebSocket` constructor can't set headers).

Reject unauthenticated connections with close code `4401` and reason `"unauthorized"`.

**Server → Client messages** (all JSON, all have a `type` field):

```ts
{ type: "hello", client_id: string, server_version: string }
{ type: "inbound_message", chat_id, content, meta }      // echo of what went to Claude
{ type: "outbound_reply", chat_id, text }                 // Claude replied
{ type: "permission_request", request_id, tool_name, description, input_preview }
{ type: "permission_resolved", request_id, behavior, by: "terminal"|"remote" }  // best-effort; may not always be known
{ type: "session_event", event: "connected"|"disconnected" }
{ type: "error", message: string }
```

**Client → Server messages:**

```ts
{ type: "send_message", content: string, chat_id?: string, meta?: object }
  → same effect as POST /api/messages

{ type: "permission_verdict", request_id: string, behavior: "allow"|"deny" }
  → same effect as POST /api/permissions/:request_id

{ type: "ping" }
  → server replies with { type: "pong" }
```

Any message from an unauthenticated client (shouldn't happen post-handshake, but defense in depth) closes the connection with 4401.

### MCP server (src/mcp.ts) specifics

```ts
const mcp = new Server(
  { name: 'bridge', version: '0.1.0' },
  {
    capabilities: {
      experimental: {
        'claude/channel': {},
        'claude/channel/permission': {},
      },
      tools: {},
    },
    instructions:
      'Messages from the bridge channel arrive as <channel source="bridge" chat_id="..."> tags. ' +
      'Each message is from a remote client (via HTTP or WebSocket). ' +
      'To reply, call the `reply` tool and pass back the exact `chat_id` from the incoming tag. ' +
      'Treat these messages as if the user had typed them directly.',
  },
)
```

**Reply tool schema:**
```ts
{
  name: 'reply',
  description: 'Send a message back to the remote client through the bridge',
  inputSchema: {
    type: 'object',
    properties: {
      chat_id: { type: 'string', description: 'The chat_id from the inbound <channel> tag' },
      text: { type: 'string', description: 'Message text to send back' },
    },
    required: ['chat_id', 'text'],
  },
}
```

When Claude calls `reply`, the handler should:
1. Emit `outbound_reply` on the bus
2. Append to the chat log in state.ts
3. Return `{ content: [{ type: 'text', text: 'sent' }] }` to Claude

**Permission request handler** (this is the critical bit for the user's use case):
```ts
const PermissionRequestSchema = z.object({
  method: z.literal('notifications/claude/channel/permission_request'),
  params: z.object({
    request_id: z.string(),
    tool_name: z.string(),
    description: z.string(),
    input_preview: z.string(),
  }),
})

mcp.setNotificationHandler(PermissionRequestSchema, async ({ params }) => {
  // Store in state as pending
  state.addPendingPermission(params)
  // Emit on bus so HTTP/WS layers can broadcast
  bus.emit('permission_request', params)
})
```

To send the verdict back, emit a notification:
```ts
await mcp.notification({
  method: 'notifications/claude/channel/permission',
  params: { request_id, behavior },
})
```

### CRITICAL: stdout hygiene

Because stdio is the transport, **nothing but MCP JSON frames can touch stdout**. This bites everyone. In `src/index.ts`, monkey-patch or redirect at startup:

```ts
// Route anything errant away from stdout
const origLog = console.log
console.log = (...args) => console.error('[bridge:stdout-redirect]', ...args)
```

Even better: configure the logger (if you add one) to always write to stderr or to a file under `~/.claude-code-bridge/logs/`. All `console.error` is fine — Claude Code's debug log captures it.

### Config

Read from environment variables with sensible defaults:

- `BRIDGE_TOKEN` — **required**. If unset, generate a random one at startup and log it to stderr (so the user sees it on first run). Never auto-generate silently — the user needs to know what token to use.
- `BRIDGE_PORT` — default `8787`
- `BRIDGE_HOST` — default `127.0.0.1`. Do NOT default to `0.0.0.0`; binding to localhost only is the right default for a local IPC bridge. Document how to change it if remote access is wanted (with a warning about tunneling through TLS).
- `BRIDGE_LOG_FILE` — optional; if set, mirror stderr logs to this file.

### Packaging as a plugin

Include a `plugin.json` and an example `.mcp.json` so users can either:

**Option A: register as bare MCP server in .mcp.json**
```json
{
  "mcpServers": {
    "bridge": {
      "command": "node",
      "args": ["/absolute/path/to/claude-code-bridge/dist/index.js"],
      "env": { "BRIDGE_TOKEN": "your-token-here" }
    }
  }
}
```
Then launch Claude Code with:
```
claude --dangerously-load-development-channels server:bridge --channels server:bridge
```

**Option B: full plugin wrapper** — create a `plugin.json` that references the MCP server, so it can be installed via `/plugin install` once the user adds it to their own marketplace. The user probably won't publish to the official marketplace during development; just make Option A work solidly.

### README.md must include

1. **Prerequisites**: Claude Code 2.1.81+, Node 20+, claude.ai login (not API key), orgs must have channels enabled
2. **Install**: `npm install && npm run build`
3. **Configure**: set `BRIDGE_TOKEN`, either inline in `.mcp.json` env block or via shell env
4. **Register with Claude Code**: show both the `.mcp.json` snippet and the launch command
5. **API reference**: all HTTP endpoints with curl examples, all WS message types
6. **Test recipe**: three-terminal test matching the docs' pattern — one for Claude Code, one for `curl -N /ws` or equivalent stream watcher, one for sending messages/verdicts
7. **Security notes**: token handling, localhost-only default, how to expose safely if needed
8. **Troubleshooting**: the "stray stdout disconnects" gotcha, how to read `~/.claude/debug/*.txt`, what to do if permission relay silently fails (version check)

### Testing checklist

Before declaring done, verify end-to-end:

1. Start Claude Code with the bridge. Confirm `/mcp` shows it connected.
2. `curl` a message to `POST /api/messages` → Claude receives it, responds, the reply arrives on the WS stream.
3. Ask Claude to do something that triggers a permission prompt (e.g. "run `ls`"). Confirm:
   - The permission request appears on the WS stream with the 5-letter request_id.
   - `POST /api/permissions/:request_id` with `{"behavior":"allow"}` unblocks Claude and the tool runs.
   - Same flow works via WS `permission_verdict` message.
4. Connect two WS clients at once, confirm both receive broadcasts.
5. Connect with a bad token, confirm rejection (4401).
6. Kill a client mid-stream, confirm the server doesn't crash.
7. Restart Claude Code while bridge is running — confirm clean reconnect (Claude Code respawns the subprocess, so this is really "restart of both").

## Things that are probably wrong to do

- **Do not** try to expose this outside localhost by default. Document it as a feature flag, not a default.
- **Do not** add per-client permission scoping in v1. Single shared token is fine. The user can add RBAC later if they want.
- **Do not** persist state to disk in v1 (no SQLite, no JSON files). In-memory Map for chat logs and pending permissions is fine; it resets when Claude Code restarts the plugin, and that's acceptable.
- **Do not** auto-retry failed verdicts. The protocol's "first answer wins" semantics mean retries are wrong.
- **Do not** add a bundled web UI. The user will build their own consumers. Include one static test HTML page in `examples/` and that's it.
- **Do not** use Express/Fastify. Node builtin `http` is enough for this surface area. Keeps the install lean.
- **Do not** take dependencies on any Bun APIs. The fakechat reference uses `Bun.serve`; port that to Node's `http.createServer`.

## Context the user may want you to know

- The user has been designing a broader system where a Node.js server broadcasts Claude Code state to clients and sends messages back. They went through hooks, SDK, tmux hacks, and Claude Commander before landing on Channels as the cleanest official path. This plugin is the **inbound/outbound backbone** — other pieces of their stack (HTTP hooks for lifecycle events, JSONL transcript tailing for thinking blocks) can layer on top but aren't part of this build.
- They may eventually want to combine this bridge with observability hooks defined in `~/.claude/settings.json`. Keep that future in mind: don't claim ownership of event types you don't produce (lifecycle events like `PreToolUse` aren't in your scope — those come from hooks, not channels).
- The user is comfortable with you making reasonable choices autonomously ("surprise me" energy is OK), but confirm before any big pivot — e.g. switching languages, splitting into multiple processes, adding a database, etc.
- The user said "I'm happy to let you build" — they want a working, polished first version, not a scaffolded stub. Ship something they can `npm run build && npm start` and test in 5 minutes.

## Acceptance criteria (what "done" looks like)

- [ ] `npm run build` produces a clean dist with no TypeScript errors
- [ ] `npm start` boots the server; stderr shows port, token, and "waiting for Claude Code stdio"
- [ ] Registering in `.mcp.json` + launching with `--dangerously-load-development-channels server:bridge --channels server:bridge` works; `/mcp` shows it connected
- [ ] `curl -H "Authorization: Bearer $TOKEN" -d '{"content":"hello"}' http://localhost:8787/api/messages` gets a response from Claude that arrives on a connected WS client
- [ ] Triggering a permission prompt in Claude Code surfaces a `permission_request` event on WS clients with a real `request_id`
- [ ] `POST /api/permissions/<id>` or a WS `permission_verdict` message successfully approves/denies the tool call
- [ ] README walks a new user from clone to working test in under 5 minutes
- [ ] `examples/ws-client.html` loads in a browser, prompts for the token, connects, and shows events live

Ship it.