# Mac Dock app: technical discovery

**Status:** discovery only. Nothing has been built yet.
**Written:** 2026-10-24, against commit `1094a48` of `claude-code-wrapper`. All
file paths are relative to the repo root. Line numbers refer to that commit and
will drift.

This folder contains:

| File | What's in it |
|---|---|
| `README.md` (this file) | Goals, recommendation, architecture, work plan, open decisions |
| [`ENGINE.md`](ENGINE.md) | The TS face engine as it stands, the firmware behaviour layer to port, and the changes needed for parity |
| [`BRIDGE.md`](BRIDGE.md) | WebSocket protocol, which events to react to, multi-session handling, adapter gaps, how to verify |
| [`MACOS.md`](MACOS.md) | Dock tile and bounce APIs, Tahoe icon behaviour, the robot window, App Nap, distribution, sources |

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
   as-is. They can be fixed for both together later.
4. Rendering may be restyled later. The first goal is fidelity.

## Verdict

This is feasible, and the repo is already in good shape for it.

- **What already exists in TypeScript:** the face renderer, the animation
  (blink, gaze, breathing, verb timelines, emotion blend, mood ring) and the arm
  motion simulation. They live in `face-editor/src/app/_lib/face-engine/`, with
  no React dependency. Browser APIs are used in only two places.
- **What's missing:** the behaviour layer that turns agent events into verbs and
  emotions. It exists only in firmware C++. A faithful TS port is about 500–700
  lines.
- **Also needed:** a "driven" mode for the TS frame controller, and a handful of
  fixes so the screen is composed the way the robot composes it. See
  [`ENGINE.md`](ENGINE.md).

## Recommended architecture

**A Swift/AppKit app that runs the TS engine inside JavaScriptCore.**

```
bridge  (node plugin/dist/index.js)
   │  ws://host:8787/ws?token=…   ← the same broadcast the robot receives
   ▼
Swift: URLSessionWebSocketTask ──messages──► JSContext (bundled TS, one script file)
                                               ├─ behaviour layer  (port of EventRouter / VerbSystem /
                                               │                   EmotionSystem / AgentEvents latch)
                                               ├─ driven FrameController + arm sim
                                               └─ faceRenderer → pixel-buffer TFTSprite (240×240 RGBA)
                               ◄──per frame──  pixels, arm angle, signals (turn ended / needs attention)
Swift: CGImage ─┬─► NSDockTile.contentView     (≤ 30 fps, slower when idle)
                └─► Robot window (NSPanel)     body art + circular screen + 2 arm CALayers (+θ / −θ)
       NSApp.requestUserAttention(...)         on turn.ended (any session)
```

**Why this approach:**
- There's one TS copy of the behaviour and animation logic, shared with the face
  editor. The firmware C++ stays the reference. A full Swift port would be a
  third copy of logic that's still changing.
- The Dock tile (`contentView`) and the window features (non-activating panel,
  floating level, all Spaces) are native and easy to build in AppKit.
- JavaScriptCore runs in-process and synchronously, and needs no DOM after a
  small refactor. You can debug it with Safari's Web Inspector by setting
  `JSContext.isInspectable` (macOS 13.3+).

**Rendering plan:**
- Start with a pixel-buffer `TFTSprite` replacement. Write it to rasterise like
  TFT_eSPI: hard-edged pixels, with only `fillSmoothCircle` smoothed. That makes
  it *more* faithful than the editor's anti-aliased canvas version.
- Scale it up with nearest-neighbour, so it looks like the real 240×240 round
  GC9A01 screen at any size.
- Later, if you want a restyled look: have TS output per-frame face parameters
  and draw them as vectors in Core Graphics.

### Alternatives considered

| Option | Pros | Cons |
|---|---|---|
| **WKWebView** instead of JavaScriptCore | A real `<canvas>`, so no `TFTSprite` replacement is needed | Every frame must be serialised (`toDataURL` → `postMessage`) and handed across processes asynchronously. Fine for a prototype, clunky for a permanent setup. |
| **Electron** | Everything runs as-is. You could even run the bridge inside the app. It has `app.dock.setIcon` / `bounce` / `setBadge`, `setAlwaysOnTop`, `setAspectRatio` and transparent frameless windows. | `setIcon` uses the `applicationIconImage` path, not `contentView`. Heavy runtime for a Dock toy. Good for a weekend proof of concept. |
| **Full Swift port** | The most native result, with the best footprint | A third copy (C++, TS, Swift) of logic that's still changing; recent commits fixed verb blending in the firmware. Only worth it if the Mac's behaviour should diverge. |

---

## Work plan

### Phase 1: TS engine prep (in this repo; can be done on Windows)

Details are in [`ENGINE.md`](ENGINE.md).

1. Port the behaviour layer (EventRouter, VerbSystem, EmotionSystem,
   AgentEvents latch and state, SceneContextFill) to TS, quirks included.
2. Add a driven mode to `frameController.ts` that mirrors
   `FrameEffective.cpp` + `FrameController.cpp`.
3. Close the gaps in how the screen is composed:
   - Draw order: face first, effects on top.
   - Code-stream effects driven by the current verb.
   - Smoothed mood ring.
   - Activity dots.
4. Arms: round to whole degrees and clamp to ±45°.
5. Make the engine runnable without a browser: pass the clock in, drive it with
   an external `tick(nowMs)`, no `requestAnimationFrame`, no canvas.
6. Write a pixel-buffer `TFTSprite`.
7. Bundle everything into one script (IIFE) with esbuild, with the face config
   snapshot embedded.

As a bonus, the editor's simulator could then react to live bridge events or
log replays locally, without a robot.

### Phase 2: Swift app (needs a Mac with Xcode)

Details are in [`MACOS.md`](MACOS.md) and [`BRIDGE.md`](BRIDGE.md).

1. WebSocket client and preferences: host, port, and token (stored in the
   Keychain). Reconnect with backoff.
2. Host the engine in JavaScriptCore. Frame loop: 30 fps normally, 60 fps while
   the code streams are showing, matching the robot. Hand the pixels to a
   `CGImage`.
3. Dock tile `contentView`, with frame-rate throttling.
4. Bounce on `turn.ended` from any session. Optionally also bounce for
   attention.
5. Robot window (`NSPanel`): resizable with a fixed shape, optional
   always-on-top, all Spaces.
6. Launch at login (`SMAppService`) and a Dock menu.
7. Developer ID signing and notarization, if you share it.

### Phase 3: design (you)

- Body and arm art, plus:
  - each arm's pivot point;
  - where the screen sits in the body, and its size;
  - which way +θ moves the arms;
  - the rest pose at offset 0.
- What goes in the Dock tile: just the face, or a mini robot with arms. The
  screen is round and Dock tiles are square, so this is also a framing choice.
- Window look: a normal window with a transparent title bar, or a borderless
  "desktop pet" window shaped like the robot.

### Verifying it matches the robot

The bridge sends every frame to every connected client. Connect the robot and
the app to the same bridge and play a log through it: they get identical input
and can be compared side by side. See [`BRIDGE.md`](BRIDGE.md#verifying-against-the-robot).

---

## Open decisions

- **Bridge lifecycle.** Is the app only a client (the bridge runs separately, as
  today), or does the app launch and own the bridge? If the Mac app is your
  always-on thing, owning the bridge is reasonable.
- **Bounce style.** `.informationalRequest` bounces once;
  `.criticalRequest` bounces until the user switches to the app. Should a
  "needs attention" notification also bounce?
- **Dock frame-rate budget.** Apple publishes no limit. Measure Dock and app CPU
  with Instruments at 10, 15 and 30 fps.
- **One face or one per session.** The fidelity goal means one face that
  follows the robot's single-session rule. Per-session faces would need a
  refactor, because `verbTimeline.ts` keeps its state at module level (one face
  per JS context).
