# Display & Face (`robot_v3`)

The display layer of the firmware — how the current **effective
`Face::Expression`** becomes pixels on the GC9A01. Covers the hardware,
the sprite framebuffer, the `FaceParams` model, modulators layered on
top, mood ring, text mode, and effects overlays.

## Hardware: GC9A01 round TFT

- Single-chip TFT controller from Galaxycore. 1.28" round IPS panel,
  240×240 px, 16-bit RGB565.
- 4-wire SPI (CS, SCK, MOSI, DC) plus optional RST and a backlight
  enable (BLK). MISO unused — write-only from the MCU side.
- 3.3 V logic and rail; most breakouts have an onboard LDO so 5 V VIN
  works too.
- Backlight is active-high. Tie BLK to 3.3 V if you don't need
  brightness control, or wire it to a PWM-capable GPIO.
- **RAM is undefined at power-on.** Init must clear the framebuffer
  *before* sending display-on (0x29) or it briefly shows garbage.
  TFT_eSPI handles this for us.
- **Vendor init has lots of undocumented commands** (~70% are factory
  tuning registers). Don't write an init from scratch — copy from a
  working library.

## Library + driver setup

We use **TFT_eSPI** by Bodmer with DMA on ESP32-S3, targeting 60 fps
via a full-screen 16 bpp `TFT_eSprite` framebuffer (240 × 240 × 2 =
~115 KB) in internal SRAM. PSRAM is **not** DMA-safe for SPI master
writes on S3; the framebuffer must stay in SRAM.

TFT_eSPI bakes pin selection in at compile time via
`robot_v3/User_Setup.h` — runtime `config.h` cannot override display
pins. If you change wiring, change both files.

Required defines in `User_Setup.h`:

- `GC9A01_DRIVER`
- `USE_HSPI_PORT` (selects SPI2 for DMA on S3 — without this people
  report only the first frame rendering)
- `SPI_FREQUENCY 80000000` (80 MHz for headroom; 40 MHz also fine)

Avoid strapping pins (GPIO 0/45/46) for `TFT_DC`.

## `Display` module

Public surface (`robot_v3/src/hal/Display.h`):

- `Display::begin()` — TFT init, sprite alloc, `Display::ready()` true on success.
- `Display::sprite()` — `TFT_eSprite&` everyone draws into.
- `Display::pushFrame()` — DMA push the sprite to the panel.
- `Display::drawConnecting(ssid)`, `drawFailedToConnect()`, `drawPortalScreen()`
  — one-shot overlays drawn directly to the panel during the blocking
  setup phases (provisioning portal, WiFi connect).

That's the entire driver surface. Everything else — face shapes, mood
ring, text, etc. — composes onto the sprite via `Scene` / `FaceRenderer`
/ etc., not into Display directly.

## Render path each frame

```
Face::FrameController::tick(ctx)
  ├─ pick FaceParams target for current effective Expression
  ├─ tween from previous target (250 ms, smoothstep)
  ├─ apply modulators on top of the tweened params:
  │     breath, body-bob, thinking tilt-flip, idle-glance,
  │     gaze wander, blink, mood-ring colour ease
  ├─ if ctx.face_mode:     Scene::renderScene(sprite, params, ...)
  │     else:             TextScene::renderTextScene(sprite, ...)
  └─ Display::pushFrame()
```

Run rate is capped at ~30 fps (`kTickIntervalMs = 33`), bumped to ~60
fps (`kTickIntervalStreamMs = 16`) while text streams (read/write
content from `EffectsRenderer`) are visible or fading, so scrolling
tracks `millis()` smoothly.

## `FaceParams`

The semantic state of the face. Every parameter is interpolable, which
is what makes `lerpParams` between any two states produce a smooth
animation. Defined in `SceneTypes.h`:

| Field          | Range / units   | What it does                                                |
|----------------|-----------------|-------------------------------------------------------------|
| `eye_dy`       | px              | vertical offset of both eye centres                         |
| `eye_rx`       | px              | eye horizontal radius                                       |
| `eye_ry`       | px              | eye vertical radius (drops to 0 during blink)               |
| `eye_stroke`   | px              | eye outline thickness                                       |
| `eye_curve`    | bend (∩ if > 0) | parabola bend for ^_^ / sad / round                         |
| `pupil_dx`     | px              | pupil horizontal offset (multiplied by thinking tilt sign)  |
| `pupil_dy`     | px              | pupil vertical offset                                       |
| `pupil_r`      | px              | pupil radius                                                |
| `mouth_dy`     | px              | vertical offset of mouth centre                             |
| `mouth_w`      | px              | mouth width                                                 |
| `mouth_curve`  | bend (∩ frown / ∪ smile) | parabola bend for the mouth                       |
| `mouth_open_h` | px              | open-mouth oval height (0 = closed line)                    |
| `mouth_thick`  | px              | mouth stroke thickness                                      |
| `face_rot`     | degrees         | whole-face rotation (× thinking tilt sign in THINKING)      |
| `face_y`       | px              | whole-face vertical offset (target + body-bob)              |
| `ring_r/g/b`   | 0–255           | mood ring colour (eased separately)                         |

Per-expression base targets live in `kBaseTargets[]` at the top of
`robot_v3/src/face/FrameController.cpp` — one row per `Face::Expression`,
in enum order.

## Modulators

Applied in order, on top of the tweened params, every frame:

- **Breath** — universal idle modulator, ±1.5 px sine on `eye_dy` / `mouth_dy / 2`,
  4 s period. Suppressed in `FINISHED` (face is celebrating) and `SLEEP`
  (slower body-bob takes over).
- **Body-bob** — vertical face offset on `face_y`, synced to the motor
  period from `MotionBehaviors::periodMsFor(state)`. Per-state amplitude
  in `bodyBobFor()`. When the motor swings to its "up" end the face
  also moves up, reading as one body's rhythm.
- **Thinking tilt-flip** — every 3–6 s while in `THINKING`, smoothly
  flips the sign of `face_rot` and `pupil_dx` over 600 ms. Bakes the
  active sign into the snapshot when leaving THINKING so the un-rotation
  tweens correctly back to upright.
- **Idle glance** — slow random pupil targets (±15 px x, ±10 px y),
  picked every 1–10 s in `IDLE`, eased over 200 ms.
- **Gaze wander** — small per-state pupil oscillation (different
  frequency/amplitude per state).
- **Blink scheduler** — per-state cadence (2–7 s), 80 ms close + 130 ms
  open.
- **Mood ring** — per-state target colour from `Settings::colorRgb(NamedColor)`,
  eased per-channel with a 200 ms time constant
  (`alpha = 1 - exp(-dt / 200ms)`). Re-seeds immediately when
  `Settings::settingsVersion()` ticks (live palette edits via `setColor`).

## Mood ring

Outer annulus around the face. State → colour mapping lives in
`FrameController.cpp::moodColorForState`:

| State              | `NamedColor`     |
|--------------------|------------------|
| `THINKING`         | `Thinking`       |
| `READING`          | `Reading`        |
| `WRITING`          | `Writing`        |
| `EXECUTING`        | `Executing`      |
| `EXECUTING_LONG`   | `ExecutingLong`  |
| `FINISHED`         | `Finished`       |
| `EXCITED`          | `Excited`        |
| `BLOCKED`          | `Blocked`        |
| `WANTS_ATTENTION`  | `WantsAt`        |
| (others)           | `Background`     |

Colour values are persisted in NVS via `Settings`, can be edited live
over the WS `setColor` message:

```json
{ "type": "setColor", "key": "thinking", "color": { "r": 60, "g": 90, "b": 220 } }
```

The ring lerps from the previous colour with the same 200 ms time
constant so palette edits don't snap.

## Text mode

`TextScene` (`TextScene.cpp`) renders the agent state as scrolling
status text instead of a face. Toggled via WS `config_change`:

```json
{ "type": "config_change", "display_mode": "text" }   // or "face"
```

Status content comes from `AgentEvents::state()` — `status_line`
("Thinking" / "Reading" / "Writing" / "Executing" / "Awaiting permission"
/ "Done"), `subtitle_tool` (tool detail), and `body_text` (assistant
message body / thinking content / notification text). The mode is
persisted in NVS (`Settings::faceModeEnabled`).

## Effects overlays

`EffectsRenderer` draws scrolling content from the latest read/write
activity (file paths, byte sizes, shell commands). Fades in/out with a
100 ms time constant when `READING` / `WRITING` enters or leaves.
`ActivityDots` draws per-turn read/write tally pips, which fade out 280 ms
after `READY → IDLE`.

## Adding a state's appearance

1. Add a row to `kBaseTargets[]` in `FrameController.cpp` (same enum index).
2. If the state should drive the mood ring, extend the `Settings::NamedColor`
   enum, add a default in `Settings.cpp`, and extend
   `moodColorForState`.
3. If it has rhythmic motion you want the face to sync to, add a row in
   `bodyBobFor()` for an amplitude.
4. If it needs a unique gaze pattern, add a case in `gazeFor()`.
5. If it needs a unique blink cadence, add a case in `blinkPeriodMsFor()`.

Tuning is done by editing the values and reflashing — there's no live
reload yet (other than mood-ring colours via `setColor`).
