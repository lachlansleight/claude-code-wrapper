# Firmware refactor plan

The next firmware revision lives in `robot_v3/` (greenfield — built
fresh, not edited in place from `robot_v2/`). It pulls together two
restructuring threads that have been planned separately:

1. **Modularisation** — split today's flat `robot_v2/` into a layered
   set of folders with strictly one-way dependencies, so individual
   modules can be reused in non-robot or non-agent-bridge projects.
2. **Personality split** — replace the monolithic `Personality` state
   machine with `EmotionSystem` + `VerbSystem`, per
   [EMOTION_SYSTEM.md](EMOTION_SYSTEM.md). The face composes a verb
   (if active) on top of a snapped V/A emotion.

Both can land at the same time because they touch overlapping code.
This doc is the master plan for that work; it supersedes the
"FIRMWARE_MODULARISATION" idea doc that lived in `docs/ideas/`.

## Naming: agent-agnostic throughout

The bridge is **agent-agnostic** — the bridge server abstracts over
every supported coding agent (and is open to new ones). The firmware
listens to a generic `agent_event` stream over WebSocket; it has no
idea which agent is behind it.

In `robot_v3/` and in the firmware-side docs, **no module names,
comments, or strings should reference any specific agent**. The
exceptions:

- Bridge-side docs (`docs/bridge/`, `docs/getting-started/`) — those
  describe per-agent setup and obviously name each agent.
- Literal payload strings the firmware matches on (e.g. the exact
  notification text for the attracting-attention overlay). Those are
  real per-agent implementation details, not framing — keep them and
  comment why.

Everywhere else in the firmware: "the agent", "the coding agent
bridge", "the bridge".

## Locked-in decisions

Settled and not up for re-litigation in the implementation:

- **Singletons (file-static state) are fine.** Every module is a
  namespace with file-static state. No instance/class refactor.
- **Grep-ability is a first-class concern.** Cross-module wiring uses
  function pointers, not string-keyed registries. Every callback
  boundary logs (`LOG_EVT(...)`) so a "where did this fire" search
  lands on the right .cpp.
- **Compile-time wiring only.** No runtime DI, no dependency
  containers, no service locator. Tunables are `constexpr` in
  headers; cross-module hookups are explicit `Module::onSomething(&handler)`
  calls in the .ino.
- **Keep the `tick()` model.** Every module exposes `begin()` +
  `tick()` if it has running state; the .ino's `loop()` calls them
  in order. No event loop, no scheduler, no FreeRTOS tasks.
- **MotionBehaviors stays indexed by a single discrete enum.** With
  the personality split, that enum becomes the *effective expression*
  (the verb if one is active, else the snapped emotion). One row per
  effective-expression value. Don't try to make motion continuous in
  V/A — snap first, evolve later if it actually bothers you.
- **Snap-to-named-emotion (not blend) for v1.** See EMOTION_SYSTEM.md.
- **Target firmware directory: `robot_v3/`.** Greenfield. Final name
  TBD.
- **`Expression` enum lives in `face/SceneTypes`.** It's data-only
  (no logic, no deps). `behaviour/MotionBehaviors` imports it
  down-layer for its table index; `face/FrameController` imports it
  for dispatch. Keeps `face/` self-sufficient — a face-only project
  doesn't need any of the `behaviour/` modules to instantiate the enum.
- **Event router is its own file: `app/EventRouter.{h,cpp}`.** The
  trigger table mapping `agent_event` callbacks to `EmotionSystem` /
  `VerbSystem` mutations is too meaty (~10 events × 2 subsystems) to
  live inline in the .ino setup. Pulling it into a dedicated
  translation unit keeps the table declarative and grep-discoverable
  in one place. The .ino just calls `EventRouter::begin()` to wire
  it up.
- **`Settings` carries a schema version.** With `NamedColor` restructured
  around `Expression`, NVS-stored values from older firmware are
  invalid. Add a `kSettingsSchemaVersion` constant and have
  `Settings::begin()` wipe + reseed defaults on a mismatch. Five
  lines, saves a debug session every time the enum shifts.
- **`hal/Display` has no `hal/Settings` dependency.** Portal /
  connecting / failed-to-connect overlays use hardcoded colours, not
  `Settings::colorRgb(NamedColor::Foreground/Background)`. Keeps the
  layer rule clean (no sideways calls within `hal/`) at the cost of
  two `constexpr` colour values inside `hal/Display`.
- **No per-folder `library.properties`** (or other Arduino Library
  Manager metadata). Not publishing to the Library Manager — the
  modules ship as plain folders to be dropped into `lib/` or
  symlinked into `src/`. Add the metadata only if and when you do
  decide to publish.
- **No tests for v1.** `behaviour/EmotionSystem` and `VerbSystem` are
  pure logic and would be ideal candidates for host-compiled tests,
  but skip for now to keep the refactor scope finite. If/when you add
  them later you'll have to retrofit no-Arduino discipline (no
  `millis()` calls in hot paths, inject a clock); flagged here so
  it's not a surprise later.

## Today's dependency reality (`robot_v2/`)

```
                       ┌────────────────────────────────────────────────┐
                       │  robot_v2.ino  (composition root)              │
                       └────────────────────────────────────────────────┘
                                         │
   ┌─────────────────────────────────────┼──────────────────────────────┐
   ▼                                     ▼                              ▼
 Provisioning  ──► Display          BridgeClient ──► AgentEvents     WiFiManager
                                                       │   │   │
                                                       │   │   └─► Settings
                                                       │   └─────► Motion           ◄── leak
                                                       └─────────► FrameController  ◄── leak
                                                                       │
                  Personality ◄────────────────────────────────────────┤
                       │                                                │
                       │                                                ▼
                MotionBehaviors ──► Motion                          Display, Settings,
                                                                    Personality,
                                                                    MotionBehaviors,
                                                                    AgentEvents,
                                                                    Scene → renderers,
                                                                    TextScene
```

Three real entanglements driving the refactor:

1. **`BridgeClient` directly calls `AgentEvents::dispatch`.** The WS
   transport can't be reused without dragging in agent-event payload
   semantics.
2. **`AgentEvents::dispatch` has upward calls.** It pokes
   `Face::invalidate()`, `Motion::holdPosition()`, and
   `Settings::setFaceModeEnabled()` from inside its handlers. Three
   layers collapsed into one module.
3. **`Personality::transitionTo(SLEEP)` calls
   `AgentEvents::clearTextDisplayForSleep()`.** A presentation
   concern leaking into the state machine.

All three become "callbacks pointing the wrong way" — the fix in each
case is "invert the callback so the lower layer publishes; the .ino
wires."

## Target structure

```
robot_v3/
  core/
    DebugLog/                # printf wrappers — header-only, no deps
    AsciiCopy/               # UTF-8 → ASCII
  hal/                       # hardware drivers — depend only on core + library
    Display/                 # GC9A01 (TFT_eSPI)
    Motion/                  # ESP32Servo + safe-range + jog/waggle/osc/hold
    Settings/                # Preferences (NVS) palette + flags
    WiFiManager/
    Provisioning/            # NVS + captive-AP HTTP server (no Display dep)
    ProvisioningUI/          # Display glue for Provisioning portal screen
  bridge/
    BridgeClient/            # generic agent-bridge WS client. Emits raw
                             # JsonDocument + connection state via callbacks.
                             # NO knowledge of payload semantics.
  agents/
    AgentEvents/             # parses `agent_event` envelopes → AgentState +
                             # typed event callbacks. Pure data; no display
                             # or motor side effects.
    BridgeControl/           # parses non-agent_event control frames
                             # (setColor, config_change, set_servo_position).
                             # Side effects via injected callbacks.
  behaviour/
    EmotionSystem/           # Valence/Activation model, decay, persistent
                             # V targets, snap-to-discrete-emotion w/ hysteresis.
    VerbSystem/              # current verb (incl. linger) + one-shot overlay
                             # verbs (waking, attracting attention).
    MotionBehaviors/         # per-effective-expression motor recipe table.
  face/
    SceneTypes/              # FaceParams, SceneContext, SceneRenderState
    FaceRenderer/            # eyes/mouth from FaceParams into TFT_eSprite&
    EffectsRenderer/         # scrolling read/write streams
    ActivityDots/            # per-turn read/write tally pips
    MoodRingRenderer/        # colour halo
    Scene/                   # composes the renderers
    TextScene/               # alternate text-status mode
    FrameController/         # tween FaceParams, layer modulators, dispatch
                             # to Scene/TextScene, push frame
  app/
    EventRouter/             # trigger table: maps agent_event callbacks
                             # to EmotionSystem + VerbSystem mutations.
    robot_v3/                # composition root: includes from every layer,
                             # wires callbacks. Only place that knows about
                             # all modules.
```

Each leaf folder is a self-contained library (a `<Name>.h` +
`<Name>.cpp`, optional `examples/`). Drop into another project's
`lib/` and it'll compile if its declared deps are present.

## Dependency direction

```
                    ┌──────────────────┐
                    │      app         │
                    └──────────────────┘
                            │
     ┌──────────────────────┼──────────────────────┐
     ▼                      ▼                      ▼
  ┌──────┐            ┌───────────┐         ┌───────────┐
  │ face │ ─────────► │ behaviour │ ──────► │  agents   │
  └──────┘            └───────────┘         └───────────┘
     │                      │                      │
     │                      ▼                      ▼
     │                ┌───────────┐          ┌──────────┐
     │                │ hal/Motion│          │  bridge/ │
     │                └───────────┘          └──────────┘
     │                                             │
     ▼                                             │
  ┌─────────────────┐                              │
  │ hal/Display     │                              │
  │ hal/Settings    │                              │
  └─────────────────┘                              │
     │                                             │
     └─────────────────────┬───────────────────────┘
                           ▼
                       ┌──────┐
                       │ core │
                       └──────┘
```

Rules:

- **No upward calls.** `agents` may not call `behaviour`, `behaviour`
  may not call `face`, `bridge` may not call `agents`.
- **No sideways calls between siblings within a layer.** `EmotionSystem`
  does not call `VerbSystem`; `face` renderers do not call each other
  except via `Scene` (their composer).
- **Composition lives in `app/`.** The .ino is the only file allowed
  to do `Bridge::onMessage(AgentEvents::dispatch)` and similar
  cross-layer wiring.
- **Data flows up through structs.** When `face` needs to know
  "what's the current verb / emotion / agent state", the .ino fills a
  `SceneContext` struct each frame; renderers read from it.

## The refactors that make this true

### 1. Invert `BridgeClient` → callbacks

```cpp
namespace Bridge {
  using MessageHandler    = void(*)(JsonDocument& doc);
  using ConnectionHandler = void(*)(bool connected);

  void onMessage(MessageHandler);
  void onConnection(ConnectionHandler);
}
```

`BridgeClient` parses WS frames into a `JsonDocument` and forwards.
Knows nothing about payload shape. The .ino wires:

```cpp
Bridge::onMessage([](JsonDocument& doc) {
  AgentEvents::dispatch(doc);
  BridgeControl::dispatch(doc);
});
Bridge::onConnection([](bool up){
  AgentEvents::notifyConnection(up);
});
```

### 2. Split `AgentEvents` into "agent semantics" + "operator controls"

Today `AgentEvents::dispatch` handles four unrelated families:

- `agent_event` and `active_sessions` — agent semantics (stays in
  `AgentEvents`).
- `setColor`, `config_change` — operator controls (move to
  `BridgeControl`).
- `set_servo_position` — manual servo override (move to `BridgeControl`).

`BridgeControl` exposes injected side-effect callbacks:

```cpp
namespace BridgeControl {
  using PaletteChangeHandler  = void(*)(Settings::NamedColor, uint8_t r, uint8_t g, uint8_t b);
  using DisplayModeHandler    = void(*)(DisplayMode mode);
  using ServoOverrideHandler  = void(*)(int8_t angle, uint32_t duration_ms);

  void onPaletteChange(PaletteChangeHandler);
  void onDisplayModeChange(DisplayModeHandler);
  void onServoOverride(ServoOverrideHandler);

  void dispatch(JsonDocument& doc);
}
```

The .ino wires those into `Settings::setColorRgb` + `Face::invalidate`,
`Settings::setFaceModeEnabled`, and `Motion::holdPosition` respectively.

After this `AgentEvents` has zero outbound calls into `face/`,
`hal/Motion`, or `hal/Settings`.

### 3. Replace `Personality` with `EmotionSystem` + `VerbSystem`

See [EMOTION_SYSTEM.md](EMOTION_SYSTEM.md) for the model. Module
shapes:

```cpp
namespace EmotionSystem {
  void begin();
  void tick();

  void impulse(float dV, float dA);
  void setHeldTarget(uint8_t driver_id, float target_v);
  void releaseHeldTarget(uint8_t driver_id);

  Emotion raw();
  Emotion snapped();        // nearest enum + raw V/A
}

namespace VerbSystem {
  void begin();
  void tick();

  void setVerb(Verb v);
  void clearVerb();
  void armLinger(uint32_t ms);
  void fireOverlay(Verb v, uint32_t duration_ms);

  Verb current();
  Verb effective();         // overlay-or-current
}
```

The trigger table that maps `agent_event` callbacks to mutations on
both subsystems lives in the .ino (or a small `EventRouter.cpp` if it
gets crowded). Keeping it in one place — declarative — is the goal.

The "verb if active else emotion" composition rule is:

```cpp
// in the .ino's SceneContext fill, every frame:
ctx.effective_expression = (VerbSystem::effective() != Verb::NONE)
    ? expression_for_verb(VerbSystem::effective())
    : expression_for_emotion(EmotionSystem::snapped().named);
```

`MotionBehaviors`'s table is indexed by `Expression` (a single enum
that unions every verb plus every named emotion). One row per
expression. Adding an emotion or a verb means adding a row.

### 4. Move presentation calls out of `behaviour/`

`Personality::transitionTo(SLEEP)`'s call to
`AgentEvents::clearTextDisplayForSleep()` doesn't survive the split —
in `robot_v3/`, the SLEEPING verb's renderer is responsible for not
displaying stale tool/agent text. If a clear is still wanted on
verb-enter SLEEPING, the .ino subscribes to `VerbSystem::onVerbChange`
and calls the clear there.

### 5. `face/` becomes agent-shape-agnostic via `SceneContext`

Today renderers read named fields off `AgentEvents::state()` directly.
Replace with a struct passed each tick:

```cpp
struct SceneContext {
  // Behaviour
  Expression effective_expression;
  uint32_t   expression_entered_at_ms;
  float      mood_v, mood_a;             // raw V/A for fine modulators

  // Activity tally
  uint16_t read_tools_this_turn;
  uint16_t write_tools_this_turn;

  // Streaming text content
  const char* read_target;
  const char* write_target;
  const char* shell_command;
  const char* body_text;

  // Status text (for TextScene)
  const char* status_line;
  const char* subtitle_tool;
  const char* pending_permission;
};
```

The .ino fills it from `AgentEvents::state()` + `EmotionSystem::raw()`
+ `VerbSystem::effective()` before calling `FrameController::tick(ctx)`.
Renderers never include `AgentEvents.h`.

This makes `face/` reusable in any project that can fill that struct.

### 6. Mood ring colour passed as RGB, not as a `NamedColor` enum

Today `MoodRingRenderer` reads `Settings::colorRgb(NamedColor)` —
linking `face/` to `hal/Settings/`. The Expression→NamedColor→RGB
mapping moves to the .ino, which writes `mood_r/g/b` into the
`SceneContext` (those fields already exist on `SceneRenderState`).
After this `face/` has no `Settings` dep.

### 7. Split `Provisioning` / `ProvisioningUI`

`Provisioning` includes `Display.h` only to draw the portal screen.
Split:

- `hal/Provisioning/` — NVS, multi-network store, captive-AP server,
  blocking `runPortal(cfg)`. Exposes `onPortalState(SSID, IP)`
  callback. No display dep.
- `hal/ProvisioningUI/` — wires `onPortalState` to
  `Display::drawPortalScreen`. ~30 lines.

A headless project drops `ProvisioningUI` and gets serial-only
feedback during the portal phase.

## Module catalogue (after the refactor)

| Module                       | Layer       | Depends on                                   | Notes |
|------------------------------|-------------|----------------------------------------------|-------|
| `core/DebugLog`              | core        | Arduino                                      | macros only |
| `core/AsciiCopy`             | core        | Arduino                                      | |
| `hal/Display`                | hal         | TFT_eSPI, core                               | exposes `TFT_eSprite&` |
| `hal/Motion`                 | hal         | ESP32Servo, core                             | jog/waggle/osc/hold + safe-range |
| `hal/Settings`               | hal         | Preferences, core                            | NamedColor enum lives here |
| `hal/WiFiManager`            | hal         | WiFi, core                                   | |
| `hal/Provisioning`           | hal         | Preferences, WebServer, WiFi, core           | display-free |
| `hal/ProvisioningUI`         | hal         | Provisioning, Display                        | optional glue |
| `bridge/BridgeClient`        | bridge      | WebSocketsClient, ArduinoJson, core          | onMessage / onConnection callbacks |
| `agents/AgentEvents`         | agents      | ArduinoJson, core                            | parses `agent_event`, owns AgentState |
| `agents/BridgeControl`       | agents      | ArduinoJson, core                            | parses operator-control msgs |
| `behaviour/EmotionSystem`    | behaviour   | core                                         | V/A model + decay + snap |
| `behaviour/VerbSystem`       | behaviour   | core                                         | current verb + lingers + overlays |
| `behaviour/MotionBehaviors`  | behaviour   | hal/Motion, behaviour/{EmotionSystem,VerbSystem} | per-Expression table |
| `face/SceneTypes`            | face        | (none)                                       | structs + `Expression` enum; no logic |
| `face/FaceRenderer`          | face        | TFT_eSPI, face/SceneTypes                    | |
| `face/EffectsRenderer`       | face        | TFT_eSPI, face/SceneTypes                    | |
| `face/ActivityDots`          | face        | TFT_eSPI, face/SceneTypes                    | |
| `face/MoodRingRenderer`      | face        | TFT_eSPI, face/SceneTypes                    | |
| `face/Scene`                 | face        | face/{FaceRenderer, EffectsRenderer, ActivityDots, MoodRingRenderer, SceneTypes} | |
| `face/TextScene`             | face        | TFT_eSPI, face/SceneTypes, core/AsciiCopy    | |
| `face/FrameController`       | face        | hal/Display, face/{Scene, TextScene, SceneTypes} | tick(SceneContext&) |
| `app/EventRouter`            | app         | agents/AgentEvents, behaviour/{EmotionSystem,VerbSystem} | trigger table: agent_event → EmotionSystem/VerbSystem mutations |
| `app/robot_v3`               | app         | every above module                           | composition root |

Every "Depends on" is satisfied by modules above in the table. No
back-edges.

## Reuse scenarios this enables

- **Headless agent observer (no face, no servo):** `core` +
  `hal/{Settings,WiFiManager,Provisioning}` + `bridge/BridgeClient` +
  `agents/AgentEvents`. Subscribe to AgentEvents callbacks; log to
  Serial / push to MQTT / blink an LED.
- **Custom-display robot:** swap `hal/Display` for a different driver,
  drop `face/Scene` for your own, keep everything else.
- **Face-only demo (no agent bridge):** `core` + `hal/{Display,Settings}`
  + `face/*`. Fill `SceneContext` from a button press or a sweep
  generator.
- **Servo-only arm (no face):** `core` + `hal/{Motion,WiFiManager}` +
  `bridge/BridgeClient` + `agents/{AgentEvents,BridgeControl}` +
  `behaviour/*`. Skip Display + face entirely.
- **Operator-control-only listener:** `core` + `bridge/BridgeClient` +
  `agents/BridgeControl` + your own side effects.

## Refactor order

Greenfield in `robot_v3/`, copying files from `robot_v2/` into their
new homes one layer at a time. The order minimises broken intermediate
states:

1. **Bottom up — `core/` + `hal/`.** Copy `DebugLog`, `AsciiCopy`,
   `Display`, `Motion`, `Settings`, `WiFiManager`, `Provisioning`
   into folders. Split off `ProvisioningUI`. Build a smoke-test
   `app/robot_v3.ino` that just initialises hardware and pushes a
   solid colour. Verify on device.
2. **`bridge/BridgeClient`.** Move it; introduce `onMessage` /
   `onConnection`. Add a smoke-test message handler that just logs
   incoming JSON. Verify the bridge's WS flow end-to-end on device.
3. **`agents/AgentEvents` + `agents/BridgeControl`.** Move semantic
   parsing; split control messages off. Wire all callbacks in the
   .ino. Verify `setColor` updates the palette and
   `set_servo_position` moves the arm.
4. **`behaviour/EmotionSystem` + `behaviour/VerbSystem`.** Implement
   per [EMOTION_SYSTEM.md](EMOTION_SYSTEM.md). Wire the trigger
   table. Verify state transitions in serial logs.
5. **`behaviour/MotionBehaviors`.** Re-index the existing table on
   the new `Expression` enum. Verify motion per state.
6. **`face/SceneTypes` + `face/*` renderers.** Move; convert
   renderers to read from a `SceneContext` parameter instead of
   `AgentEvents::state()`.
7. **`face/FrameController`.** Move; switch its dispatch to read
   `Expression` from `SceneContext` (not `Personality::current()`).
   Move the mood-ring colour resolution into the .ino's
   `SceneContext`-fill so `face/` no longer depends on `Settings`.
8. **Final composition pass on `app/robot_v3.ino`.** Add a
   `verifyWiring()` that asserts every required callback was
   registered before `loop()` runs.

Each step ends with a verifiable "this still works" check on hardware.

## Tradeoffs (acknowledged)

- **More files, more includes per .ino.** The composition root grows
  to ~10–15 callback registrations. Acceptable cost for moving
  cross-module knowledge from "scattered through every .cpp" to "one
  file you can read top-to-bottom."
- **Callback indirection during debugging.** `LOG_EVT` at every
  callback boundary keeps grep working.
- **Slight RAM overhead for callback function pointers.** ~50 bytes
  on S3. Negligible.
- **Risk of forgotten wiring in the .ino.** Mitigated by
  `verifyWiring()` running before `loop()`.

## Still want to nail down before starting

A few smaller calls I'd like a steer on:

- **Where `BridgeControl` lives.** I've put it in `agents/` because
  the parsing is JSON-shaped and it sits next to `AgentEvents`. It's
  arguable it belongs in `bridge/` (it's about the bridge transport's
  control channel) or in `app/` (it has no semantic content of its
  own). `agents/` is my recommendation but happy to move.
- **`Settings::NamedColor` after the personality split.** Today the
  enum has rows for the old states (`Thinking`, `Reading`, `Writing`,
  `Executing`, `ExecutingLong`, `Blocked`, `Finished`, `Excited`,
  `WantsAt`, plus `Background` / `Foreground`). With verbs and
  emotions split, do we want the palette structured by Expression
  (one colour per row of the MotionBehaviors-style table)? I'd
  recommend yes — same indexing as everything else.
- **Polled vs event-driven `pending_permission`.** Today
  `Personality` polls `AgentEvents::state().pending_permission`
  because the bridge can't reliably emit `permission.resolved` for
  all agents. In the new world `EmotionSystem` will need the same
  dual: a `permission.requested` event sets the held V target, but a
  polled fallback is needed to clear it on a `turn.started` if no
  `permission.resolved` ever arrived. Plan to keep both paths.
- **Effective-expression enum granularity.** Should `STRAINING` get
  its own MotionBehaviors row, or share with `EXECUTING` with a
  modifier? I'd give it its own row — keeps the table dumb and the
  visual distinct.
- **Should overlay verbs (`WAKING`, `ATTRACTING_ATTENTION`) appear in
  the Expression enum at all,** or are they handled by a separate
  overlay-renderer path? I'd lean: they're Expression values, just
  marked as "overlay" via a flag, so the same FaceParams /
  MotionBehaviors machinery applies.
- **`STRAINING → stressed V target` wiring.** Where does the
  30-second timer live — in `VerbSystem` (it triggers from a
  verb-state) or in `EmotionSystem` (since it's an emotional impulse)?
  Probably `VerbSystem` polls its own elapsed-in-state and pokes
  `EmotionSystem::setHeldTarget` once the threshold passes — that
  keeps the threshold logic next to the verb that owns it. Worth
  flagging because it's the first place behaviour modules talk to
  each other.

None of these block starting; they're things to decide as you reach
each step.
