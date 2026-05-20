# Firmware overview

The robot is an ESP32-S3 board driving a 240×240 round GC9A01 TFT and a
single SG92R servo. The firmware is a layered C++ application that consumes
agent lifecycle events over WebSocket and translates them into face
expressions and arm motion.

## Source layout

```
robot_v3/
  robot_v3.ino                   # Arduino entry — setup() / loop()
  User_Setup.h                   # TFT_eSPI compile-time pin config
  src/
    core/      AsciiCopy, DebugLog          # utilities
    hal/       Display, Motion, MotionBehaviors,
               Settings, Provisioning, ProvisioningUI, WiFiManager
    bridge/    BridgeClient                  # WebSocket transport
    agents/    AgentEvents, BridgeControl    # parse bridge frames
    behaviour/ EmotionSystem, EmotionBlend,
               EmotionTriangulation, VerbSystem
    app/       EventRouter, SceneContextFill
    face/      FrameController (orchestrator)
               Scene, SceneTypes, FaceRenderer,
               EffectsRenderer, MoodRingRenderer, ActivityDots,
               TextScene, VerbTimeline,
               FaceEnums, FacePrimitives,
               FACE_CONFIG.h, FACE_CONFIG_DATA.h
```

## Layering

```
                 ┌─────────────────────────────┐
   wifi  ──►     │  BridgeClient (ws)          │
                 └──────────────┬──────────────┘
                                ▼
                 ┌─────────────────────────────┐
                 │  AgentEvents + BridgeControl│   parse JSON frames
                 └──────────────┬──────────────┘
                                ▼
                 ┌─────────────────────────────┐
                 │  EventRouter                │   wire agent state →
                 │     ↳ VerbSystem            │   verbs / emotion drivers
                 │     ↳ EmotionSystem         │
                 └──────────────┬──────────────┘
                                ▼
                 ┌─────────────────────────────┐
                 │  SceneContextFill           │   snapshot world →
                 └──────────────┬──────────────┘   Face::SceneContext
                                ▼
                 ┌─────────────────────────────┐
                 │  FrameController (face)     │   render one frame
                 │  MotionBehaviors → Motion   │   drive servo
                 └─────────────────────────────┘
```

There are no cyclic dependencies. The face renderer never reads agent
state directly — it sees only the immutable `SceneContext` snapshot
assembled each loop.

## Boot sequence (`robot_v3.ino`)

`setup()` runs in this strict order. Each step depends on the previous:

1. **Serial** — `Serial.begin(115200)`.
2. **HAL init** — `Settings::begin()` (load palette/face-mode from NVS),
   `Display::begin()` (alloc 240×240×16 sprite in **internal SRAM**, not
   PSRAM — PSRAM is not DMA-safe for the SPI master), `Motion::begin()`
   (attach servo, 50 Hz, 500–2400 µs), `MotionBehaviors::begin()`
   (servo safe range ±45°), `Face::begin()` (FrameController state +
   RNG seed), `ProvisioningUI::begin()`.
3. **Provisioning** — load saved networks. If none, or boot button held
   ≥800 ms on GPIO 0, or a one-shot portal request is queued, run the
   blocking soft-AP captive portal (`robot-XXXX` / `192.168.4.1`).
   Otherwise try each remembered network in turn until one connects.
4. **Behaviour wiring** — `EventRouter::begin()` constructs the verb
   and emotion systems and registers all callbacks against `AgentEvents`
   and `BridgeControl`.
5. **Bridge** — `Bridge::begin(host, port, token)` opens the WebSocket
   and starts auto-reconnect + heartbeat.

`loop()` runs at ~100 Hz (10 ms minimum sleep). Each iteration:

```
WifiMgr::tick()              # auto-reconnect on drop
Bridge::tick()               # pump WS state machine, parse frames
EventRouter::tick()          # advance verbs + emotions, apply drivers
SceneContextFill::fill(ctx)  # snapshot world into Face::SceneContext
MotionBehaviors::tick(ctx)   # decide servo target from ctx
Motion::tick()               # advance jog/pattern/thinking, ~50 Hz write
Face::tick(ctx)              # render at most one frame (throttled)
delay(10)
```

## Frame timing

Different subsystems run at different rates:

| Subsystem            | Rate            | Source                                    |
|----------------------|-----------------|-------------------------------------------|
| Main loop            | ~100 Hz         | `delay(10)` floor in `loop()`             |
| Servo writes         | ~50 Hz          | `Motion::tick()` cadence                  |
| Face render (idle)   | 30 Hz (33 ms)   | `kFrameAnim.tick_interval_ms`             |
| Face render (stream) | 60 Hz (16 ms)   | bumps while VerbReading/Writing active    |
| Emotion raw follow   | τ ≈ 300 ms      | `kEmotionSim.raw_tau_ms`                  |
| Mood ring smoothing  | τ ≈ 200 ms      | FrameController internal                  |
| Stream effect alpha  | τ ≈ 100 ms      | FrameController internal                  |
| Emotion geometry tween | τ ≈ 100 ms    | `kFrameAnim.emotion_geometry_smooth_tau_ms` |

## Module index

| Module                | Purpose                                                                 |
|-----------------------|-------------------------------------------------------------------------|
| `BridgeClient`        | WebSocket transport, JSON parse, heartbeat, auto-reconnect              |
| `AgentEvents`         | Parse `agent_event` envelopes into `AgentState` and semantic callbacks  |
| `BridgeControl`       | Parse non-semantic control frames (palette, display mode, servo override) |
| `EventRouter`         | Composition root: wires bridge → behaviour, applies derived drivers     |
| `VerbSystem`          | Discrete verb state machine (Sleeping/Thinking/Reading/Writing/…)       |
| `EmotionSystem`       | Continuous (valence, activation) model with hysteresis snap             |
| `EmotionBlend`        | Continuous FaceParams blend over Delaunay triangulation                 |
| `EmotionTriangulation`| Generated table: 14 anchors, 17 triangles                               |
| `SceneContextFill`    | Snapshot live state into a flat `Face::SceneContext`                    |
| `FrameController`     | Owns *all* per-frame animation state; renders one frame                 |
| `Scene` / `TextScene` | Render-mode dispatch (face or text/debug)                               |
| `FaceRenderer`        | Stateless geometry: eyes + mouth from a resolved `FaceParams`           |
| `EffectsRenderer`     | Read/write token streams, waking/attention rim                          |
| `MoodRingRenderer`    | 6-px ring around face perimeter                                         |
| `ActivityDots`        | Per-turn read/write tool count rings                                    |
| `VerbTimeline`        | Sample sparse field overrides for the active verb                       |
| `Motion`              | Layered single-channel servo HAL                                        |
| `MotionBehaviors`     | Map `Expression` → motion mode (table-driven)                           |
| `Settings`            | NVS-backed palette + face/text mode + motors-disabled                   |
| `Display`             | GC9A01 panel, sprite framebuffer, splash screens                        |
| `Provisioning*`       | WiFi + bridge credential store + captive portal                         |

## Key invariants and gotchas

- **Framebuffer must live in internal SRAM.** PSRAM is not DMA-safe for SPI
  master writes on ESP32-S3.
- **TFT_eSPI bakes pins at compile time.** Display wiring is in
  `robot_v3/User_Setup.h`, not `config.h`.
- **`FrameController` owns all face animation state.** Renderers below it are
  stateless. Nothing else should mutate tweens/blinks/gaze.
- **Body bob follows arm position, not arm period.** `FrameController` maps
  `Motion::currentOffsetDeg()` across effective `arm_min_deg`…`arm_max_deg`
  after `tickEffectiveParams` and `MotionBehaviors::tick`. Tune arm in the last
  four `kBaseTargets` cells or verb timeline overrides (ms for period/interval).
- **Bridge classifies tools, firmware classifies access.**
  `activity-classify.ts` (bridge) maps tool name → `ActivityKind`;
  `AgentEvents::classifyActivity` (firmware) maps `ActivityKind` plus shell
  command shape → READ vs WRITE. They serve different layers — don't unify.
- **All display strings pass through `AsciiCopy`.** Bridge payloads are UTF-8;
  TFT bitmap fonts are 7-bit ASCII.
- **Session latching.** First `session_id` seen is sticky; further sessions
  filtered out unless the bridge's active list says the latched one is gone.
