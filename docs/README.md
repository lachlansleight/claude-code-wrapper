# Documentation

Index for the docs in this repo. Top-level
[`README.md`](../README.md) covers project overview, install, and
quickstart; the rest lives here.

## Start here

- [**AGENT_TO_ROBOT_PIPELINE.md**](AGENT_TO_ROBOT_PIPELINE.md) — end-to-end
  tour: a hook fires inside Claude Code → the servo moves and the face
  changes. Read this first if you want one mental model of the whole
  system.

## Bridge

- [bridge/OBJECT_INTERFACE.md](bridge/OBJECT_INTERFACE.md) — canonical
  `AgentEvent` vocabulary the bridge emits over WebSocket.
- [bridge/HOOK_MAPPING.md](bridge/HOOK_MAPPING.md) — per-agent hook →
  generic event translation tables.
- [bridge/CURL_RECIPES.md](bridge/CURL_RECIPES.md) — curl recipes for
  the HTTP API.

## Getting started (per agent)

- [getting-started/CLAUDE_CODE.md](getting-started/CLAUDE_CODE.md)
- [getting-started/CODEX.md](getting-started/CODEX.md)
- [getting-started/CURSOR.md](getting-started/CURSOR.md)
- [getting-started/OPENCODE.md](getting-started/OPENCODE.md)

## Firmware (`robot_v2/`)

- [firmware/OVERVIEW.md](firmware/OVERVIEW.md) — module map, runtime
  flow, hardware, libraries.
- [firmware/PERSONALITY.md](firmware/PERSONALITY.md) — 13-state machine
  spec (event triggers, transitions, protected windows, tool-linger,
  permission gating).
- [firmware/MOTION_BEHAVIORS.md](firmware/MOTION_BEHAVIORS.md) —
  per-state motor recipe table; modes (`STATIC` / `OSCILLATE` /
  `WAGGLE` / `THINKING` / etc.) and the face-sync `periodMsFor` helper.
- [firmware/DISPLAY_AND_FACE.md](firmware/DISPLAY_AND_FACE.md) — GC9A01
  hardware notes, sprite framebuffer, `FaceParams` model, modulators
  (breath / body-bob / blink / gaze / mood-ring), text mode, effects
  overlays.
- [firmware/EXPRESSION_PLANS.md](firmware/EXPRESSION_PLANS.md) —
  in-progress: expression-name refactor brain-dump.

## Forward-looking ideas

- [ideas/ART_COLLABORATION.md](ideas/ART_COLLABORATION.md) — how to let
  a non-coder artist contribute face designs while keeping the
  parameterised animation system working.
- [ideas/COMPLEX_TRANSITIONS.md](ideas/COMPLEX_TRANSITIONS.md) —
  declarative transition table sketch for `Personality`.
