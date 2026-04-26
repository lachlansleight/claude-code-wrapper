# CLAUDE.md

Orientation for Claude Code sessions working on this repo. Read `README.md`
for the full picture and `plugin/src/OBJECT_INTERFACE.md` for the event
vocabulary spec. This file calls out what's not obvious from those.

## Current shape

A **standalone agent-agnostic bridge**: a Node HTTP/WS service that
ingests lifecycle hooks from any agentic CLI (Claude Code, Codex, Cursor,
OpenCode), parses them into a shared `AgentEvent` vocabulary, and
broadcasts envelopes over WebSocket.

The bridge has **no opinion about presentation**. It emits lifecycle
events (turn started, activity started/finished, permission requested),
not derived states (`thinking`, `idle`, `blocked`). State derivation
lives in the consumer — the firmware in `robot_v2/`.

Source lives in `plugin/src/`. Compiled output is `plugin/dist/`. The
Claude Code plugin in `plugin/` only registers hooks now — there is no
MCP server, no `.mcp.json`. The bridge is started separately
(`node plugin/dist/index.js`).

## Repo layout

```
plugin/
  src/
    index.ts                 # entry: HTTP + WS + Firebase
    http.ts                  # /hooks/:agent + /api/* surface
    ws.ts                    # WS hub
    bus.ts                   # in-process EventEmitter
    state.ts                 # in-memory chat / permission / session store
    agent-event.ts           # AgentEvent type vocabulary (the spec, in code)
    activity-classify.ts     # tool name → ActivityKind table
    activity-summary.ts      # ActivityRef.summary builder
    adapters/
      types.ts               # Parser interface + ParsedEvent
      claude.ts, codex.ts, cursor.ts, opencode.ts
      index.ts               # parser registry
    hook-forward.ts          # Claude-side stdin→HTTP forwarder
    auth.ts, logger.ts, firebase.ts, types.ts
    OBJECT_INTERFACE.md      # canonical event-vocabulary spec
  hooks/hooks.json           # Claude Code hook wiring
  .claude-plugin/plugin.json # plugin manifest (hooks only — NO mcpServers)
.claude-plugin/marketplace.json
robot_experiment/, robot_v2/   # ESP32 firmware
helpers/                       # forwarders for Codex / Cursor / OpenCode
examples/                      # curl + browser WS client
```

`robot_v2/` is the in-progress firmware successor to `robot_experiment/`.

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
broadcast as-is over WebSocket. See `OBJECT_INTERFACE.md` for the union
of `event.kind` values.

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
ActivityKind. Mirror it in `robot_v2/` if/when the firmware needs the
same classification.

### Permissions

`state.ts` holds a `Map<request_id, PendingPermission>` keyed by id. The
HTTP layer adds entries on `permission.requested`, removes them on
`permission.resolved`.

Without an MCP channel back to the agent CLI, `POST /api/permissions/:id`
cannot actually approve/deny in the agent — it only clears local state
and broadcasts a resolve. The terminal user still has to answer the
agent's own prompt.

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
- When editing firmware, test on-device. Type-checking / Arduino
  compile aren't enough for servo or display work. If no hardware is
  available, say so explicitly.
- There is no test suite. Verify via the smoke test in `README.md`.

## Firmware quick-map

See `robot_experiment/FIRMWARE_OVERVIEW.md` (and `robot_v2/`) for the
full tour. Key invariants worth knowing:

- **`AmbientMotion` edge detection.** PreToolUse edges require
  `current_tool` set AND `current_tool_end_ms == 0` AND (either prevTool
  was empty OR prevEndMs was nonzero). Dropping any condition causes
  runaway motion.
- **`Motion::playJog`** slews to a new angle over ~250ms and holds
  there. Successive jogs interpolate from the last commanded angle.
- **Thinking mode** eases the base angle back to centre over 1s on
  enable and ramps oscillation amplitude over the same window.

The firmware is mid-migration from raw `hook_event` consumption to
`agent_event`. Personality derivation now belongs in firmware, not the
bridge.
