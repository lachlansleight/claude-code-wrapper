# robot_v3 — refactor / rewrite plan

A clean-slate rewrite of `robot_v2/` into a stack of self-contained,
reusable modules. The goal isn't bug-for-bug parity at every internal
contract — it's that **the device behaves the same** while the codebase
becomes a kit of parts you can recombine for the next robot.

This plan builds on `docs/ideas/FIRMWARE_MODULARISATION.md` (read it
first — it covers the audit of v2's coupling and the proposed layer
diagram). That doc was scoped as an *in-place* refactor with one-commit-
at-a-time migration safety. Because v3 is a fresh tree, we can take
harder cuts the in-place doc deferred. The deltas are called out below.

## Reuse axes we're optimising for

You named three future variants:

1. **Same robot shape, different protocol** — e.g. listens to a non-
   Claude WS feed (Home Assistant, MIDI, custom telemetry).
2. **Same protocol, different face** — different visual identity, same
   personality + servo behaviour.
3. **Same protocol, different motors** — multi-servo arm, stepper base,
   no motors at all.

Each variant should swap **one layer**, not edit-through every file.
That's the test: if changing the face requires touching `BridgeClient`,
the layering is wrong.

## Target layer stack

```
              ┌──────────────────────────┐
              │       app/robot_v3       │   composition root — the only file
              │                          │   that knows about every other layer
              └──────────────────────────┘
                         │ wires
   ┌──────────┬──────────┼──────────┬──────────┐
   ▼          ▼          ▼          ▼          ▼
 face/    motion/   behaviour/   protocol/   net/
                                    │          │
                                    ▼          ▼
                              transport/    hal/
                                    │          │
                                    └────►  core/
```

Rules:

- Calls only go **down** the stack. No upward calls, no sibling calls.
- Cross-layer wiring lives **only in `app/`**. Every module exposes
  `begin / tick / on*` and is otherwise inert.
- Data flows up through **plain structs** passed by `const&` into tick
  functions. Modules never reach into each other's globals.
- Every module is a folder with `<Name>.h` + `<Name>.cpp` + an optional
  `examples/` sketch demonstrating it standalone (forces the
  no-hidden-deps property to actually hold).

## Module catalogue

| Module                        | Layer       | Responsibility | Depends on |
|-------------------------------|-------------|----------------|------------|
| `core/DebugLog`               | core        | LOG_* macros over Serial | Arduino |
| `core/AsciiCopy`              | core        | UTF-8 → ASCII transliteration | Arduino |
| `core/EventBus` *(new)*       | core        | Tiny pub/sub for typed callbacks (`Slot<T>`) — replaces the ad-hoc `on*` slot per module | Arduino |
| `hal/Display`                 | hal         | TFT_eSPI init, sprite framebuffer, DMA push | TFT_eSPI |
| `hal/Motion`                  | hal         | Servo abstraction: `attach`, `commandAngle`, `tick` slew | ESP32Servo |
| `hal/Settings`                | hal         | Preferences-backed palette + flags + version counter | Preferences |
| `net/WiFiManager`             | net         | Connect with timeout + auto-reconnect, no UI | WiFi |
| `net/Provisioning`            | net         | NVS multi-network store + captive-AP HTTP form. **No display include.** Emits `onPortalState(ssid, ip)` | Preferences, WebServer |
| `net/ProvisioningUI` *(opt)* | net         | Glue that draws portal screen via `hal/Display`. Drop for headless builds | net/Provisioning, hal/Display |
| `transport/WsClient` *(generic)* | transport | **Generic** auth'd WebSocket client. Knows token-in-query auth + reconnect; **does not know it's the Claude bridge.** Emits `onConnection(bool)` + `onMessage(JsonDocument&)` | WebSocketsClient, ArduinoJson |
| `protocol/ClaudeBridge`       | protocol    | Parses Claude bridge envelopes (`agent_event`, `active_sessions`, `setColor`, `set_servo_position`, `config_change`) into typed events. Send helpers (`sendPermissionVerdict`, `requestSessions`). **No side effects** — emits typed events. | transport/WsClient, ArduinoJson |
| `behaviour/Personality`       | behaviour   | State machine driven by typed events. Pure logic — emits `onStateChange`. **Does not call face/motion.** | (event types only — see below) |
| `behaviour/MotionScript`      | behaviour   | Per-state motion recipe. Maps a `BehaviourState` enum → motion commands on `hal/Motion`. | hal/Motion |
| `face/SceneTypes`             | face        | `FaceParams`, `SceneRenderState`, `SceneContext` structs only. No deps. | — |
| `face/FaceRenderer`           | face        | Eyes/mouth from `FaceParams` into a `TFT_eSprite&` | TFT_eSPI, face/SceneTypes |
| `face/EffectsRenderer`        | face        | Scrolling read/write text streams | TFT_eSPI, face/SceneTypes |
| `face/ActivityDots`           | face        | Per-turn read/write tally pips | TFT_eSPI, face/SceneTypes |
| `face/MoodRingRenderer`       | face        | Coloured halo (RGB comes in via `SceneContext`, not Settings) | TFT_eSPI, face/SceneTypes |
| `face/Scene`                  | face        | Composes the above each frame | face/{renderers, SceneTypes} |
| `face/TextScene`              | face        | Alternate text-status mode | face/SceneTypes, core/AsciiCopy |
| `face/FrameController`        | face        | Tween FaceParams between targets, layer modulators, dispatch to Scene/TextScene, push frame. `tick(const SceneContext&)`. | hal/Display, face/{Scene, TextScene, SceneTypes} |
| `app/robot_v3`                | app         | `setup()` + `loop()` + serial shim. Wires every callback. Builds `SceneContext` each frame from Personality + ClaudeBridge state. | every module above |

## Key responsibilities & the cuts that matter

The four moves below are what make this rewrite better than v2 — and
better than the in-place doc — for cross-project reuse.

### 1. `transport/WsClient` is protocol-agnostic

In v2, `BridgeClient` parses Claude-specific frames inline. In v3 the
transport layer only does WS + auth + reconnect + JSON parsing. The
`protocol/` layer interprets shapes.

**Win:** swap `protocol/ClaudeBridge` for `protocol/HomeAssistant` (or
whatever) and the entire transport keeps working. Reuse axis #1.

### 2. `protocol/ClaudeBridge` has zero side effects

In v2, `AgentEvents::dispatch` writes to `Settings`, calls
`Face::invalidate()`, calls `Motion::holdPosition()`. Three layers
collapsed into one.

In v3, `protocol/ClaudeBridge` emits **typed events only**:

```cpp
struct PaletteChange { NamedColor color; uint8_t r, g, b; };
struct ServoOverride { float angle_deg; uint32_t hold_ms; };
struct DisplayModeChange { RenderMode mode; };
struct AgentEvent { /* turn/activity/permission/message */ };
```

The `.ino` wires palette changes into `Settings::setColorRgb` +
`Face::invalidate`, servo overrides into `Motion::commandAngle`, etc.
This is the v2 doc's "split BridgeControl out of AgentEvents" move,
finished.

**Win:** the protocol layer compiles standalone. You can write a unit
test that feeds canned JSON in and asserts the events that come out,
no hardware needed.

### 3. `behaviour/Personality` doesn't know about Claude

The Personality state machine in v2 directly switches on `agent_event`
fields. In v3 it consumes a smaller, **protocol-neutral** event vocab:

```cpp
struct BehaviourInput {
  enum Kind { TURN_START, TURN_END, ACTIVITY_START, ACTIVITY_END,
              PERMISSION_REQUESTED, PERMISSION_RESOLVED,
              ATTENTION_REQUESTED, IDLE_TIMEOUT } kind;
  ActivityAccess access;       // for ACTIVITY_*
  uint32_t duration_ms;        // for ACTIVITY_END
  // ...
};
```

The `.ino` translates ClaudeBridge events → BehaviourInput. A different
protocol does its own translation. Personality doesn't change.

**Win:** the same personality (and motion script, and face) can be
driven by a different bridge. Reuse axis #1 without touching the fun
parts.

### 4. `face/` is fed via a `SceneContext` struct, not Settings/AgentEvents reach-throughs

The renderers in v2 read `Settings::color565(...)` and
`AgentEvents::state().latest_shell_command` directly. In v3 every
renderer takes its inputs from `SceneContext`:

```cpp
struct SceneContext {
  uint8_t  state_id;          // opaque — renderer dispatches on it
  uint32_t entered_at_ms;
  uint8_t  mood_r, mood_g, mood_b;          // resolved palette
  uint8_t  fg_r, fg_g, fg_b, bg_r, bg_g, bg_b;
  uint16_t read_count, write_count;
  const char* read_target;
  const char* write_target;
  const char* shell_command;
  const char* status_line;
  const char* subtitle;
  const char* body_text;
  const char* pending_permission;
  // ... only what renderers need
};
```

The `.ino` builds a `SceneContext` each frame from the current
Personality state + ClaudeBridge state + Settings palette. Renderers
include nothing from `behaviour/`, `protocol/`, or `hal/Settings`.

**Win:** a face-only demo board fills `SceneContext` from a button or
a sweep generator and reuses every renderer. Reuse axis #2.

## Open design questions (please weigh in before I start)

These are the calls I'd like alignment on — each is cheap to decide
now, expensive to change after we've built around it.

1. **Singletons vs instances.** v2 is namespaces with file-static
   state. Genuinely simpler on a one-MCU project. Going to instances
   (`FaceRenderer face;`) buys testability and "two of these on one
   board" but costs verbosity everywhere. **My recommendation:** stay
   with namespaces, but design every module's API as if it were a class
   (no implicit cross-module reads). If we ever need instancing, the
   refactor is mechanical.

2. **EventBus or direct callbacks?** A tiny `core/EventBus` with
   `Slot<T>::subscribe(cb)` removes the per-module `onWhatever()`
   boilerplate and lets the `.ino` look like a wiring diagram. But it
   adds an indirection layer that's harder to grep. **My
   recommendation:** plain function-pointer `on*()` slots per module,
   matching v2's style. Keep grep-ability.

3. **Compile-time vs runtime composition.** Other firmware projects
   could include only the modules they need by editing `.ino` includes
   (compile-time), or we could add `#define USE_FACE 1` flags. **My
   recommendation:** compile-time only. Each variant is its own `app/`
   sketch.

4. **Do we keep the polled-state model?** v2 is "tick everything in a
   loop, modules read each other's polled state." v3 could be more
   event-driven. **My recommendation:** keep polled — it's the v2
   property that lets you add a new consumer (LED ring, second display)
   by writing one more `tick()`. Don't break that.

5. **MotionScript decoupling.** v2's `MotionBehaviors` is indexed by
   `Personality::State`. The v2 doc deferred this. **My
   recommendation:** also defer — the readability win of an enum-aligned
   table is real, the reuse win is hypothetical. If you want a
   different motion script for the same personality, swap the
   `MotionScript` module wholesale.

6. **Should `app/robot_v3` actually be `app/claude_observer/` etc.?**
   If we expect multiple variant sketches, naming the first one for
   what it *is* (a Claude observer with face + servo) leaves room for
   `app/midi_visualiser/` next to it without renaming. **My
   recommendation:** rename to `app/claude_face_arm/` (or similar) once
   we know the second variant.

## Build order

The clean-slate version of the v2 doc's "in one sitting" sequence.
Each step ends with a build that runs on hardware — don't move on
until the device boots and behaves.

1. **`core/` + `hal/` + `net/`.** Display, Motion, Settings,
   WiFiManager, Provisioning, ProvisioningUI. Hello-world `.ino` that
   provisions, connects, and renders a static face. This is the
   reusable plumbing — get it right first.

2. **`transport/WsClient`.** Generic WS client with token auth +
   reconnect. `.ino` logs every received frame to Serial. No protocol
   parsing yet.

3. **`protocol/ClaudeBridge`.** Parse the five frame families into
   typed events. `.ino` logs each typed event. Add send helpers
   (`sendPermissionVerdict`, `requestSessions`).

4. **`face/SceneTypes` + `face/FrameController` + renderers.** Drive
   the renderers from a hand-built `SceneContext` (from Serial commands,
   say). Confirms `face/` is genuinely standalone.

5. **`behaviour/Personality`.** State machine over `BehaviourInput`.
   `.ino` translates ClaudeBridge events into BehaviourInput. Wire
   Personality state into the `SceneContext`.

6. **`behaviour/MotionScript`.** Per-state motion table over
   `hal/Motion`.

7. **Polish.** Serial command shim, BOOT-button portal entry, render-
   mode toggle, palette overrides, etc.

8. **Strip dead code.** Anything in v2 that didn't earn its way into
   v3 stays dead. Don't port for completeness.

## What we're explicitly *not* carrying over

A clean rewrite is a chance to drop things. Confirm or push back on
each:

- `ToolFormat.{h,cpp}` — already marked "legacy, unused" in v2's
  header. Don't port.
- `compile_errors.txt` — build artefact.
- The `g_forceHardcodedProvisioning` path in `setup()` — if you want a
  hard-coded build, just don't store anything in NVS. Drop the flag.
- Half-finished personality states that aren't wired (the v2 header
  notes "v1 wires up idle + thinking only — the state table knows
  about the rest"). v3 should ship with every state in the table
  actually driven, or those states should be removed.
- `clearTextDisplayForSleep()` as a member of the protocol layer —
  becomes a wired callback in `app/`.

## What this plan does *not* cover

- **Pin / wiring changes.** TFT_eSPI bakes pins at compile time via
  `User_Setup.h`; `config.h` cannot override them. v3 inherits the
  same constraint.
- **PSRAM/DMA constraint.** Sprite framebuffer must stay in internal
  SRAM. `hal/Display` keeps that invariant.
- **Test strategy.** No on-host test suite is proposed; we still
  verify on hardware. The `examples/` per module is the closest thing
  to a smoke test (it forces the module to compile in isolation).

---

Once you've signed off on the open questions in §"Open design
questions", I'll start at step 1 of the build order.
