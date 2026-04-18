# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Status

This repo currently contains only `BUILD_BRIEF.md` — the design spec for a project named **Claude Code Bridge** that has not yet been implemented. The brief is authoritative; read it before writing code. This file is a condensed reference so you don't have to re-read all 380 lines for every task.

## What's being built

A single Node.js process that is simultaneously:
- An **MCP server** speaking the Claude Code **channel protocol** over stdio (spawned by Claude Code)
- An **HTTP REST API** on `127.0.0.1:8787` for injecting messages, querying state, and approving/denying permissions
- A **WebSocket hub** at `/ws` broadcasting live events to clients

The core value is **remote permission relay**: external clients can approve/deny Claude Code tool-use prompts. The all-in-one architecture is a deliberate user choice — do NOT split it into a bridge-plugin + separate-server design.

## Canonical spec

Before coding, read **https://code.claude.com/docs/en/channels-reference** end-to-end. The fakechat reference implementation (https://github.com/anthropics/claude-plugins-official/tree/main/external_plugins/fakechat) is the closest analog — study its source, but port `Bun.serve` to Node's builtin `http`.

## Architecture

One process, EventEmitter-based bus wiring three I/O layers to a shared state store:

```
  mcp.ts (stdio ↔ Claude Code)   http.ts (:8787/api)   ws.ts (:8787/ws)
              │                        │                     │
              └────────────── bus.ts (EventEmitter) ──────────┘
                                       │
                                    state.ts
```

Bus events: `inbound_message`, `outbound_reply`, `permission_request`, `permission_verdict`, `claude_message`, `session_meta`. Layer responsibilities and event consumers are spelled out in the brief — follow them.

Planned layout: `src/{index,mcp,http,ws,bus,auth,state,types}.ts`, plus `plugin.json`, `.mcp.json`, `examples/{curl.md,ws-client.html}`.

## Non-negotiable protocol constraints

These are easy to get wrong and break the channel:

1. **Stdout is reserved for MCP JSON frames only.** No `console.log`, no stray prints — stray stdout disconnects the server. Redirect `console.log` → `console.error` at startup. All logging goes to stderr (or an optional file via `BRIDGE_LOG_FILE`).
2. **Authenticate every client.** The `claude/channel/permission` capability means anyone who can send through the channel can approve tool use. Require a shared `BRIDGE_TOKEN` on all HTTP requests and WS upgrades. Reject WS with close code `4401`.
3. **Permission `request_id` is 5 lowercase letters `[a-km-z]`** (skips `l`). The terminal dialog doesn't display it — the bridge is the only way clients learn the ID. Include it in every outbound permission event.
4. **First verdict wins.** If the terminal user answers before a remote client, Claude Code silently drops the remote verdict. Surface `applied: false` honestly; don't retry.
5. **`meta` keys must be identifiers** (letters, digits, underscores). Hyphens are silently dropped. Use `chat_id`, not `chat-id`. Each meta entry becomes a `<channel>` tag attribute in Claude's context.
6. **Always include `chat_id` on inbound messages** and instruct Claude (via the `Server` `instructions` string) to echo it back when calling the `reply` tool.

Required `Server` capabilities:
```js
capabilities: {
  experimental: { 'claude/channel': {}, 'claude/channel/permission': {} },
  tools: {},
}
```

## Stack and dependencies

- Node 20+, TypeScript, compiled (`build` + `start` scripts).
- Dependencies: `@modelcontextprotocol/sdk`, `ws`, `zod`. **No Express/Fastify/Koa** — use builtin `http`. **No Bun APIs.** **No database** (in-memory `Map` only in v1).

## Config (env vars)

- `BRIDGE_TOKEN` — required. If unset, generate a random one at startup and log it loudly to stderr. Never silent.
- `BRIDGE_PORT` — default `8787`.
- `BRIDGE_HOST` — default `127.0.0.1`. Do NOT default to `0.0.0.0`.
- `BRIDGE_LOG_FILE` — optional stderr mirror.

## Scope boundaries (don't do these in v1)

- No bind to `0.0.0.0` by default, no per-client RBAC, no on-disk persistence, no verdict retries, no bundled web UI, no Express/Fastify, no Bun APIs.
- Lifecycle events (`PreToolUse`, etc.) come from hooks, not channels — not this project's scope.

## End-to-end test recipe

Once implemented, verify (from the brief's acceptance criteria):
1. `/mcp` in Claude Code shows the bridge connected after launching with `claude --dangerously-load-development-channels server:bridge --channels server:bridge`.
2. `curl -H "Authorization: Bearer $BRIDGE_TOKEN" -d '{"content":"hello"}' http://localhost:8787/api/messages` → Claude responds → reply arrives on a connected WS client.
3. Trigger a tool (e.g. ask Claude to run `ls`) → `permission_request` event on WS with a 5-letter `request_id` → `POST /api/permissions/<id>` with `{"behavior":"allow"}` unblocks it. Same flow via WS `permission_verdict`.
4. Two WS clients both receive broadcasts; bad token is rejected with 4401; killing a client doesn't crash the server.
