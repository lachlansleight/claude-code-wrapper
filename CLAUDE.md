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
[`robot_v3/`](robot_v3/) is the canonical consumer. The robot is called
Sultana; [`sultana-dock/`](sultana-dock/README.md) is a macOS app that
runs the same firmware code to show the robot in the Dock and on the
desktop.

The bridge has **no opinion about presentation**. It emits lifecycle
events (turn started, activity started/finished, permission requested),
not derived states. Behaviour derivation lives in the firmware
(`VerbSystem` + `EmotionSystem` + `EventRouter`).

Source lives in `plugin/src/`. Compiled output is `plugin/dist/`. The
Claude Code plugins only register hooks that POST to the bridge. On a Mac,
The Sultana app runs a bundled copy of the bridge (built into the app by its
"Embed Bridge and Plugin" build phase); otherwise start it separately
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
    transcript.ts             # mid-turn assistant text from Claude transcripts
    hook-forward.ts           # Claude-side stdin→HTTP forwarder
    auth.ts, logger.ts, firebase.ts, types.ts, mojibake.ts, dotenv.ts
  scripts/bundle.mjs          # esbuild: the bridge as one file, for the Sultana app
  hooks/hooks.json
  .claude-plugin/plugin.json
.claude-plugin/marketplace.json
sultana-plugin/               # the Claude Code plugin: hooks.json only (HTTP hooks)
robot_v3/                     # ESP32-S3 firmware
  host/                       # SultanaCore: the firmware built for a desktop, replay CLI, fixtures, make test
sultana-dock/                 # the Sultana macOS app (Xcode project; links SultanaCore)
  bridge/                     # Embed Bridge and Plugin build phase, the pinned Node (node.env, fetch-node.sh), its entitlements
  install.sh, release.sh      # local install; signed + notarised DMG for others (docs/INSTALL.md)
face-editor/                  # Next.js face editor (TS port of the face engine)
cad/                          # parametric build123d model of the body (cad/README.md)
scripts/record-ws.mjs         # record a bridge session as JSONL (fixtures, replay)
helpers/                      # forwarder scripts for Codex / Cursor / OpenCode
examples/                     # browser WS client + sample hooks-settings.json
docs/                         # all documentation (start at docs/README.md)
```

`robot_experiment/` no longer exists — all firmware work lives in
`robot_v3/`.

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

The bridge cannot push verdicts into the agent CLI; `POST /api/permissions/:id`
only clears local state
and broadcasts a resolve. The terminal user still has to answer the
agent's own prompt. Claude Code has no hook for the verdict, so the Claude
adapter keeps one pending request per session and infers the resolution
(see `docs/bridge/HOOK_MAPPING.md`). The firmware also clears
`pending_permission` on turn and session boundaries and on disconnect.

## Working on this repo

- After editing the bridge: `cd plugin && npm run build`, then restart it.
  The Sultana app picks up bridge changes when it's rebuilt.
- Test bridges belong on a spare port (8797, 8798) with a clean
  environment (`env -i PATH=/usr/bin:/bin BRIDGE_TOKEN=test node …`): a
  shell may export `BRIDGE_*` settings (log file, Firebase), and a bridge
  usually already runs on 8787.
- The Claude Code plugin is `sultana-plugin/` (HTTP hooks to the bridge on
  127.0.0.1:8787; no Node, no token). The marketplace (`sultana`, in
  `.claude-plugin/marketplace.json`) also lists the legacy
  `claude-code-bridge` forwarder plugin; never enable both. Plugins are
  pinned by commit, so to pick up plugin changes inside Claude Code:
  ```
  /plugin marketplace remove sultana
  /plugin marketplace add C:/path/to/claude-code-wrapper
  /reload-plugins
  ```
  Use a Windows-style absolute path with forward slashes — Git-Bash
  paths (`/c/...`) get treated as git remotes and fail. Test a plugin
  change without installing: `claude --plugin-dir sultana-plugin`.
  The Sultana app carries its own copy of the plugin and installs it from
  Settings ▸ Claude Code; after changing `sultana-plugin/`, rebuild the app
  and use Update Plugin there. Test that code against a throwaway
  `CLAUDE_CONFIG_DIR`, not your real `~/.claude`.
- The plan for shipping Sultana as a self-contained plugin and app is
  [`docs/sultana/PLAN.md`](docs/sultana/PLAN.md).
- When editing firmware, **test on-device**. Type-checking / Arduino
  compile aren't enough for servo or display work. If no hardware is
  available, say so explicitly.
- Firmware behaviour has a regression test: `cd robot_v3/host && make test`
  replays recorded bridge sessions through the unmodified firmware and
  diffs golden traces. Run it after behaviour changes; if a change is
  intended, `make golden` and commit the new traces with it. A new firmware
  `.cpp` must be added to `FIRMWARE_SRCS` in `robot_v3/host/Makefile` and to
  the SultanaCore target in `sultana-dock/`.
- There is no bridge test suite. Verify the bridge via the smoke test in
  `README.md`.

## Firmware quick-map

Full tour: [`docs/firmware/OVERVIEW.md`](docs/firmware/OVERVIEW.md) and
[`docs/firmware2/`](docs/firmware2/README.md). The exact event rules are
in [`docs/mac-app/ENGINE.md`](docs/mac-app/ENGINE.md) §2. Key invariants:

- **The face pulls; nothing pushes.** `EventRouter` turns agent events
  into `VerbSystem` and `EmotionSystem` writes; each loop
  `SceneContextFill::fill` snapshots that state into a `SceneContext`
  that the face reads.
- **One session at a time.** `AgentEvents`' latch follows the session of
  the most recent `turn.started` / `session.started` and drops the rest.
- **Pending permission is polled.** `EventRouter::tick` holds a −0.6
  valence driver while `AgentState.pending_permission` is set. Valence
  moves with τ = 90 s, so the effect is slow.
- **Verbs.** Overlays (Waking and AttractingAttention 1.5 s, Celebrating
  3 s) cross-fade out over their last 500 ms. Nothing is protected or
  queued: `setVerb` cuts an overlay. Tools linger 1 s past
  `activity.finished` / `activity.failed`, then Thinking; Executing turns
  into Straining after 5 s. Setting the current verb again restarts it.
- **Turn ends by cause.** `completed` (or none) → Celebrating + happy
  impulse; `error` → idle with a valence dip; `aborted` → idle.
- **Disconnects** clear the latch, pending permission and the verb
  (unless asleep).
- **The bridge host is discovered at boot, not configured.** The stored
  host is only the last known good address; `BridgeDiscovery::find`
  probes it, then sweeps the local /24 on the bridge port, verifying
  candidates via `GET /api/health`. If nothing is found the robot runs
  offline in idle until reboot — `Bridge::begin` is deliberately never
  called, since the WS client blocks the loop for seconds per attempt
  against an unreachable host.
- **Body bob tracks arm position.** `FrameController` maps
  `Motion::currentOffsetDeg()` to vertical bob using effective `arm_min_deg` /
  `arm_max_deg` from `Face::effectiveFaceParams()` (28-field row, ms timing).
  Loop: `tickEffectiveParams` → `MotionBehaviors` → `Motion::tick` → `Face::tick`.
- **TFT_eSPI bakes pins at compile time.** `robot_v3/User_Setup.h`
  governs display wiring; `config.h` does not.
- **The sprite framebuffer must be in internal SRAM, not PSRAM** —
  PSRAM is not DMA-safe for SPI master writes on ESP32-S3.
- **`activity-classify.ts` (bridge) ≠ `AgentEvents::classifyActivity`
  (firmware).** The bridge classifies tool names → ActivityKind; the
  firmware classifies ActivityKind + shell-command shape → READ vs WRITE.
  They serve different layers; don't unify.
