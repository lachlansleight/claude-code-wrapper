# CLAUDE.md

Orientation for Claude Code sessions working on this repo. Read `README.md`
for user-facing docs and `BUILD_BRIEF.md` for the original design spec (still
authoritative on architecture and scope). This file calls out what's not
obvious from those.

## Current state

The bridge described in `BUILD_BRIEF.md` **is built and installed as a Claude
Code plugin**. Do not re-implement it. Source lives in `plugin/src/` (TypeScript,
Node 20+). Compiled output is `plugin/dist/` and is referenced by `plugin/.mcp.json`
and `plugin/hooks/hooks.json` via `${CLAUDE_PLUGIN_ROOT}`.

Firmware for the ESP32 companion device lives in `robot_experiment/`. It has
its own overview doc at `robot_experiment/FIRMWARE_OVERVIEW.md` — start there
for any firmware task.

## Repo layout

```
plugin/              # installable plugin (Node + MCP + hooks)
  src/               # TypeScript sources
  dist/              # compiled output — rebuild with `npm run build` inside plugin/
  hooks/hooks.json   # wires every Claude Code hook to the bridge
  .mcp.json          # registers the bridge as an MCP server
  .claude-plugin/plugin.json
.claude-plugin/marketplace.json   # single-plugin marketplace manifest
robot_experiment/    # ESP32 Arduino sketch — firmware for the companion device
examples/            # curl recipes, browser WS client
example_esp32_client/      # older Firebase-polling ESP32 sketch (legacy reference)
example_esp32_ws_client/   # older raw-WS ESP32 sketch (legacy reference)
BUILD_BRIEF.md       # original design spec
README.md            # user-facing install + usage guide
```

Note: `robot_v2/` may exist as an untracked directory. It is a user-created
scratch copy of `robot_experiment/` — ignore unless the user specifically
points at it.

## Bridge (plugin/)

Single Node process running three I/O layers on an EventEmitter bus:

```
  mcp.ts (stdio ↔ Claude Code)   http.ts (:8787/api)   ws.ts (:8787/ws)
              │                        │                     │
              └────────────── bus.ts (EventEmitter) ──────────┘
                                       │
                                    state.ts
```

### Non-negotiable protocol constraints

Easy to break; hard to debug once broken:

1. **Stdout is reserved for MCP JSON frames only.** No `console.log`, no stray
   prints — stray stdout disconnects the channel. All logging goes to stderr
   (or `BRIDGE_LOG_FILE`). `console.log` is redirected to `console.error` at
   startup.
2. **Authenticate every client.** `BRIDGE_TOKEN` is required on all HTTP
   requests and WS upgrades. WS rejects with close code `4401`. If `BRIDGE_TOKEN`
   is unset the bridge **refuses to start** — do not auto-generate a fallback.
3. **Permission `request_id` is 5 lowercase letters `[a-km-z]`** (skips `l`).
   The terminal dialog doesn't display it — the bridge is the only way remote
   clients learn it.
4. **First verdict wins.** If the terminal user answers before a remote client,
   Claude Code silently drops the remote verdict. Surface `applied: false`
   honestly; don't retry.
5. **`meta` keys must be identifiers** (letters, digits, underscores). Hyphens
   are silently dropped. Use `chat_id`, not `chat-id`.
6. **Always include `chat_id` on inbound messages** and instruct Claude (via
   the `Server` `instructions` string) to echo it back via the `reply` tool.

Required `Server` capabilities:

```js
capabilities: {
  experimental: { 'claude/channel': {}, 'claude/channel/permission': {} },
  tools: {},
}
```

### Stack constraints

- Node 20+, TypeScript, compiled. `plugin/package.json` has `build` + `start`.
- Dependencies: `@modelcontextprotocol/sdk`, `ws`, `zod`.
- **No Express/Fastify/Koa** — builtin `http` only.
- **No Bun APIs.**
- **No database.** Chat log is in-memory `Map`; state is a single struct.
  Optional Firebase sync is write-through only (see README).

### Config

All env vars documented in `README.md`. Most relevant:

- `BRIDGE_TOKEN` — required, hard-fail if absent.
- `BRIDGE_PORT` (default `8787`), `BRIDGE_HOST` (default `0.0.0.0` in
  `plugin/.mcp.json` so the ESP32 can reach it; override to `127.0.0.1` for
  localhost-only).
- `BRIDGE_LOG_FILE`, `BRIDGE_DEBUG_FRAMES` — diagnostic.
- `BRIDGE_DATABASE_URL` + `BRIDGE_AGENT_ID` — optional Firebase RTDB sync.

### Rebuilding after bridge changes

Claude Code pins plugins by commit hash. After editing `plugin/src/`:

```
cd plugin && npm run build
```

Then inside Claude Code:

```
/plugin marketplace remove claude-code-bridge-local
/plugin marketplace add <absolute-path-with-forward-slashes>
/reload-plugins
```

A Git-Bash-style path (`/c/...`) gets interpreted as a git remote and fails.

## Firmware (robot_experiment/)

See `robot_experiment/FIRMWARE_OVERVIEW.md` for the full tour. Quick map:

- Arduino sketch (ESP32), non-blocking cooperative loop.
- Modules are one namespace per file, polled-state primary, callbacks optional.
- `ClaudeEvents` is the central state + dispatch module; everything else
  reads `ClaudeEvents::state()`.
- Display is fully state-driven — never call draw functions imperatively.
- Libraries (install via Arduino Library Manager): WebSockets (Markus Sattler),
  ArduinoJson v7+, Adafruit GFX, Adafruit SSD1306, ESP32Servo.
- `config.h` is gitignored; copy `config.example.h` to create it.

Key firmware invariants worth knowing before editing:

- **`AmbientMotion` edge detection.** PreToolUse edges require `current_tool`
  set AND `current_tool_end_ms == 0` AND (either prevTool was empty OR
  prevEndMs was nonzero). Dropping any condition causes runaway motion —
  hard-learned this session.
- **`Motion::playJog`** slews to a new angle over ~250ms and holds there
  (does not return to centre). Successive jogs interpolate from the last
  commanded angle via `commandedAngle`.
- **Thinking mode** eases the base angle back to centre over 1s on enable
  and ramps oscillation amplitude over the same window, so transitions from
  a jog position don't snap.

## Working on this repo

- When editing bridge code, remember the stdout rule: `console.log` anywhere
  in `plugin/src/` is a latent protocol break.
- When editing firmware, test behavior on-device if possible — type-checking
  / compiling isn't enough for servo or display work. If no hardware is
  available, say so explicitly rather than claiming a behavior change works.
- There is no test suite. Verification is manual per the "Quick smoke test"
  section of `README.md` and the end-to-end recipe in `BUILD_BRIEF.md`.
- Don't bind bridge to `0.0.0.0` in code — that's an env-var choice the user
  opts into. Defaults in code should stay localhost-safe.
