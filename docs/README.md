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
- [bridge/CONTROL.md](bridge/CONTROL.md) — how to control the robot via
  semantic `agent_event` and raw verb/emotion commands (WS + HTTP).
- [bridge/HOOK_MAPPING.md](bridge/HOOK_MAPPING.md) — per-agent hook →
  generic event translation tables.
- [bridge/CURL_RECIPES.md](bridge/CURL_RECIPES.md) — curl recipes for
  the HTTP API.

## Getting started (per agent)

- [getting-started/CLAUDE_CODE.md](getting-started/CLAUDE_CODE.md)
- [getting-started/CODEX.md](getting-started/CODEX.md)
- [getting-started/CURSOR.md](getting-started/CURSOR.md)
- [getting-started/OPENCODE.md](getting-started/OPENCODE.md)

## Firmware (`robot_v3/`)

- [firmware/OVERVIEW.md](firmware/OVERVIEW.md) — what the firmware does,
  runtime flow, and module map.
- [firmware/BEHAVIOUR.md](firmware/BEHAVIOUR.md) — `VerbSystem` +
  `EmotionSystem`, precedence rules, and the raw command surface.
- [firmware/DISPLAY_AND_FACE.md](firmware/DISPLAY_AND_FACE.md) — GC9A01
  hardware notes, sprite framebuffer, `FaceParams`/`SceneContext`, face
  modulators, text scene, effects overlays.

## Forward-looking ideas

- [ideas/ART_COLLABORATION.md](ideas/ART_COLLABORATION.md) — how to let
  a non-coder artist contribute face designs while keeping the
  parameterised animation system working.
- [ideas/COMPLEX_TRANSITIONS.md](ideas/COMPLEX_TRANSITIONS.md) —
  declarative transition table sketch for `Personality`.
