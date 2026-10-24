# Sultana Dock (Mac app)

**Status:** Phase 1 (the host build of the firmware core,
[`robot_v3/host/`](../../robot_v3/host/README.md)) and Phase 2 (the Dock app,
[`sultana-dock/`](../../sultana-dock/README.md)) are done; Phase 3 (robot
window) is next. The finalised plan is
[`PLAN.md`](PLAN.md); start there.
**History:** the technical discovery was written 2026-10-24 against commit
`1094a48`. It recommended porting the behaviour layer to TypeScript and
running it in JavaScriptCore. That was reviewed the same day and replaced by
compiling the firmware's own C++ into the app (see `PLAN.md` §1 for why and
§10 for the proof). The discovery files are kept as reference; the parts
they got right are still right.

This folder contains:

| File | What's in it | Status |
|---|---|---|
| [`PLAN.md`](PLAN.md) | Decision, architecture, repo layout, host core and C API, Mac app design, phases, open decisions, verification | **current** |
| `README.md` (this file) | Goals and the design brief | current |
| [`ENGINE.md`](ENGINE.md) | §2 is the reference description of the firmware behaviour layer, and §9 lists stale docs. §1, §3, §4, §7, §8 describe the TS-port route and are superseded | partly superseded |
| [`BRIDGE.md`](BRIDGE.md) | WebSocket protocol, which events to react to, multi-session handling, adapter gaps, how to verify | current |
| [`MACOS.md`](MACOS.md) | Dock tile and bounce APIs, Tahoe icon behaviour, the robot window, App Nap, distribution. The JavaScriptCore section is superseded | mostly current |

---

## Goals

1. **A macOS app whose Dock icon shows the robot's face**, behaving *exactly*
   like the `robot_v3` firmware in response to bridge events.
   - The icon bounces when a turn completes.
2. **An optional robot window:** a larger rendering of the whole robot,
   including its servo arms.
   - The real device has a single servo, and a gear mirrors its motion to both
     arms on the tiny body.
   - The window is resizable and can optionally float above all other windows.
3. **Copy the robot as it is, gaps and quirks included.** The robot's known
   gaps (missing adapter cases, no reset on disconnect, and so on) are copied
   as-is for the first build, then fixed for both together (`PLAN.md` Phase 4).
4. Rendering may be restyled later. The first goal is fidelity.

## How it's built, in one paragraph

Sultana Dock is a Swift/AppKit app. It links SultanaCore, a static library built from the firmware's own
`app/`, `behaviour/`, `agents/`, `face/` and `core/` sources plus
`Motion.cpp`, `MotionBehaviors.cpp` and `Settings.cpp`, compiled unmodified
against ~150 lines of stub headers (`Arduino.h`, `TFT_eSPI.h`,
`ESP32Servo.h`, `Preferences.h`, `esp_random.h`) and host implementations of
`Display` and `BridgeClient`. The app feeds it the same WebSocket frames the
robot receives, ticks it from a display link, and draws the 240×240 sprite it
produces into the Dock tile and a robot window. The same library is a
command-line harness that replays recorded event logs, which doubles as the
firmware's first test suite. Details: [`PLAN.md`](PLAN.md).

## Design brief (Phase 5 in the plan)

- Body and arm art, plus:
  - each arm's pivot point;
  - where the screen sits in the body, and its size;
  - which way +θ moves the arms;
  - the rest pose at offset 0.
- What goes in the Dock tile: just the face, or a mini robot with arms. The
  screen is round and Dock tiles are square, so this is also a framing choice.
- Window look: a normal window with a transparent title bar (the plan's
  starting point), or a borderless "desktop pet" window shaped like the robot
  (later toggle).

## Verifying it matches the robot

The bridge sends every frame to every connected client. Connect the robot and
the app to the same bridge and play a log through it: they get identical input
and can be compared side by side. See
[`BRIDGE.md`](BRIDGE.md#verifying-against-the-robot). The plan adds a
headless check (the CLI harness against `ENGINE.md` §2) and a frame-exact
check (app replay vs CLI, same log and seed); see `PLAN.md` §9.

## Open decisions

Tracked in [`PLAN.md`](PLAN.md#8-open-decisions).
