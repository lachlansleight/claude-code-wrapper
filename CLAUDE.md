# CLAUDE.md

Orientation for Claude Code sessions working on this repo. Read
[`README.md`](README.md) for the project quickstart and
[`docs/README.md`](docs/README.md) for the full doc index. This file
calls out what's not obvious from those.

## Current shape

A **standalone agent-agnostic bridge**: a Node HTTP/WS service that
ingests lifecycle hooks from any agentic CLI (Claude Code, Codex, Cursor,
OpenCode), parses them into a shared `AgentEvent` vocabulary, and
broadcasts envelopes over WebSocket. The ESP32-S3 firmware in
[`robot_v2/`](robot_v2/) is the canonical consumer.

The bridge has **no opinion about presentation**. It emits lifecycle
events (turn started, activity started/finished, permission requested),
not derived states. State derivation lives in the firmware
(`Personality`).

Source lives in `plugin/src/`. Compiled output is `plugin/dist/`. The
Claude Code plugin in `plugin/` only registers hooks now — there is no
MCP server, no `.mcp.json`. The bridge is started separately
(`node plugin/dist/index.js`).

## Repo layout

```
plugin/                       # bridge service (Node)
  src/
    index.ts, http.ts, ws.ts, bus.ts, state.ts
    agent-event.ts            # canonical event vocabulary (in code)
    activity-classify.ts      # tool name → ActivityKind table
    activity-summary.ts       # ActivityRef.summary builder
    adapters/{claude,codex,cursor,opencode,index,types}.ts
    hook-forward.ts           # Claude-side stdin→HTTP forwarder
    auth.ts, logger.ts, firebase.ts, types.ts, mojibake.ts, dotenv.ts
  hooks/hooks.json
  .claude-plugin/plugin.json
.claude-plugin/marketplace.json
robot_v2/                     # ESP32-S3 firmware
helpers/                      # forwarder scripts for Codex / Cursor / OpenCode
examples/                     # browser WS client + sample hooks-settings.json
docs/                         # all documentation (start at docs/README.md)
```

`robot_experiment/` no longer exists — all firmware work lives in
`robot_v2/`.

## Bridge architecture

```
   adapters/<name>.ts ── parse ──► ParsedEvent[]
                                       │
                                       ▼
                                    bus.ts
                                       │
              ┌────────────────────────┼────────────────────┐
              ▼                        ▼                    ▼
            ws.ts                 firebase.ts            state.ts
        (broadcast)          (mirror to RTDB)      (in-mem store)
```

The single bus channel of note is `agent_event` — `AgentEventEnvelope`
broadcast as-is over WebSocket. See
[`docs/bridge/OBJECT_INTERFACE.md`](docs/bridge/OBJECT_INTERFACE.md)
for the union of `event.kind` values, and
[`docs/bridge/HOOK_MAPPING.md`](docs/bridge/HOOK_MAPPING.md) for the
per-agent translation tables.

### Parsers

`getParser(name)` returns `undefined` for unknown agents. Each parser
exposes `parse({ hook_type, payload }) → ParsedEvent[]`. One native hook
can fan out into multiple events (e.g. `UserPromptSubmit` →
`[turn.started, message.user]`; `PostToolUse` for `TodoWrite` →
`[activity.finished, todo.updated]`).

When adding a new agent: copy `claude.ts` and remap. Add the agent's
tool names to `activity-classify.ts`. Add per-tool summary helpers in
`activity-summary.ts` if the existing keys (`file_path`, `command`,
`pattern`, `query`, `url`) don't match.

`activity-classify.ts` is the single source of truth for tool →
ActivityKind. `AgentEvents::classifyActivity` in the firmware mirrors
the same idea but on the consumer side, classifying for read/write
heuristics — they are intentionally separate.

### Permissions

`state.ts` holds a `Map<request_id, PendingPermission>` keyed by id. The
HTTP layer adds entries on `permission.requested`, removes them on
`permission.resolved`.

Without an MCP channel back to the agent CLI, `POST /api/permissions/:id`
cannot actually approve/deny in the agent — it only clears local state
and broadcasts a resolve. The terminal user still has to answer the
agent's own prompt. The firmware recovers from stuck `BLOCKED` state by
clearing `pending_permission` on `turn.started`.

## Working on this repo

- After editing the bridge: `cd plugin && npm run build`. The Claude
  Code plugin marketplace pin is keyed by commit hash, so to pick up
  changes inside Claude Code:
  ```
  /plugin marketplace remove claude-code-bridge-local
  /plugin marketplace add C:/path/to/claude-code-wrapper
  /reload-plugins
  ```
  Use a Windows-style absolute path with forward slashes — Git-Bash
  paths (`/c/...`) get treated as git remotes and fail.
- When editing firmware, **test on-device**. Type-checking / Arduino
  compile aren't enough for servo or display work. If no hardware is
  available, say so explicitly.
- There is no test suite. Verify the bridge via the smoke test in
  `README.md`; verify firmware on hardware.

## Firmware quick-map

Full tour: [`docs/firmware/OVERVIEW.md`](docs/firmware/OVERVIEW.md).
Key invariants worth knowing:

- **`Personality::tick` polls `pending_permission`.** It cannot use the
  callback because the single `EventHandler` slot is owned by
  Personality itself.
- **Tool-linger.** `READING` / `WRITING` / `EXECUTING` survive 1 s past
  `activity.finished` so bursts of same-type calls don't flap. Refreshed
  by any matching `activity.started`; pre-empted by a different-type one.
- **Protected `min_ms` windows.** `FINISHED` (1.5 s), `WAKING` (1 s),
  `WANTS_ATTENTION` (1 s) play in full; pre-empting requests are queued
  and fire when the window expires.
- **`MotionBehaviors::periodMsFor(state)` is the face-sync source.**
  `FrameController` reads it to body-bob the face in time with the arm.
  Change a state's `periodMs` in `kMotion[]` and the face auto-resyncs.
- **TFT_eSPI bakes pins at compile time.** `robot_v2/User_Setup.h`
  governs display wiring; `config.h` does not.
- **The sprite framebuffer must be in internal SRAM, not PSRAM** —
  PSRAM is not DMA-safe for SPI master writes on ESP32-S3.
- **`activity-classify.ts` (bridge) ≠ `AgentEvents::classifyActivity`
  (firmware).** The bridge classifies tool names → ActivityKind; the
  firmware classifies ActivityKind + shell-command shape → READ vs WRITE.
  They serve different layers; don't unify.
