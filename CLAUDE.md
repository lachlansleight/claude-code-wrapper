# CLAUDE.md

Orientation for Claude Code sessions working on this repo. Read `README.md`
for the full picture. This file calls out what's not obvious.

## Current shape

This used to be a Claude-Code-only MCP/channel plugin. It is now a
**standalone agent-agnostic bridge**: a Node HTTP/WS service that ingests
lifecycle hooks from any agentic CLI (Claude Code, Codex, Cursor,
OpenCode), normalizes them into a shared event vocabulary, and broadcasts
both the raw hook payloads and a derived high-level personality state
over WebSocket.

Source lives in `plugin/src/`. Compiled output is `plugin/dist/`. The
Claude Code plugin in `plugin/` only registers hooks now — there is no
MCP server, no `.mcp.json`, no `claude/channel` traffic. The bridge is
started separately (`node plugin/dist/index.js`).

`BUILD_BRIEF.md` is **historical**. It describes the original
MCP/channel design that no longer exists. Don't rely on it for
architecture; it stays for context only.

## Repo layout

```
plugin/
  src/
    index.ts                 # entry: HTTP + WS + Firebase + personality
    http.ts                  # /hooks/:agent + /api/* surface
    ws.ts                    # WS hub
    bus.ts                   # in-process EventEmitter
    state.ts                 # in-memory chat / permission / session store
    personality.ts           # state machine (port of robot_v2/Personality.cpp)
    adapters/
      types.ts               # NormalizedHook + ToolAccess + PersonalityState
      claude.ts, codex.ts, cursor.ts, opencode.ts
      index.ts               # adapter registry
    hook-forward.ts          # Claude-side stdin→HTTP forwarder
    auth.ts, logger.ts, firebase.ts, types.ts
  hooks/hooks.json           # Claude Code hook wiring
  .claude-plugin/plugin.json # plugin manifest (hooks only — NO mcpServers)
.claude-plugin/marketplace.json
robot_experiment/, robot_v2/   # ESP32 firmware
examples/                       # curl + browser WS client
```

`robot_v2/` is the in-progress firmware successor to `robot_experiment/`.
Both currently consume raw `hook_event` payloads; the new `state_event`
is additive, no firmware change required.

## Bridge architecture

```
   adapters/<name>.ts ── normalize ──► personality.ts
                          │                   │
                          └────► bus.ts ◄─────┘
                                   │
              ┌────────────────────┼────────────────────┐
              ▼                    ▼                    ▼
            ws.ts              firebase.ts            state.ts
        (broadcast)          (mirror to RTDB)      (in-mem store)
```

Two events of note on the bus:

- `hook_event` — `{ agent, hook_type, payload, ts }` — broadcast as-is.
- `state_event` — `{ state, prev, ts }` — emitted by `personality.ts`
  on every transition, plus on every new WS connection.

### Adapters

`getAdapter(name)` returns `null` for unknown agents. Each adapter
exposes `normalize({ hook_type, payload }) → NormalizedHook | null`.
Returning `null` means "I can't classify this; broadcast the raw event
but don't drive personality."

When adding a new agent, copy `claude.ts` and remap. Tool read/write
classification lives in `adapters/types.ts::toolAccess()` — keep it as
the single source of truth (it's also mirrored in
`robot_v2/ToolFormat.cpp::access()` on the firmware side; if the lists
diverge the robot will mis-classify).

### Personality state machine

Direct port of `robot_v2/Personality.cpp`. State graph + tunables live
in `personality.ts::CONFIG`. A 100ms `setInterval` ticks for timeouts
and queue flushes. The `unref()` keeps it from holding the process
alive on its own.

`pendingPermission` is a polled overlay — set on `permission_request`,
cleared on `permission_resolved`. While set, the state forces to
`blocked` and remembers `preBlockedState` to restore on resolve.

Permission verdict caveat: without an MCP channel back to Claude Code,
`POST /api/permissions/:id` cannot actually approve/deny in the agent.
It clears local state and broadcasts a resolve event for UI parity.
The terminal user still has to answer the agent's own prompt.

## Working on this repo

- Stdout is no longer special. The old `console.log` redirect is gone;
  any logging goes through `logger.ts` (stderr + optional log file).
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
