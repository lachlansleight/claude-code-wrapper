# Firmware Overview (`robot_v2/`)

ESP32-S3 firmware that connects to the agent bridge over WebSocket,
runs a personality state machine driven by `agent_event` frames, and
expresses that state via a procedural face on a 240×240 GC9A01 round
TFT plus a single hobby servo for arm motion. WiFi + bridge credentials
are provisioned on-device via a captive-AP portal.

For the cross-system tour, read
[`AGENT_TO_ROBOT_PIPELINE.md`](../AGENT_TO_ROBOT_PIPELINE.md) first.

## Hardware

| Component             | Pin / bus                       | Notes                                            |
|-----------------------|---------------------------------|--------------------------------------------------|
| ESP32-S3 dev board    | —                               | needs internal SRAM ≥ 256 KB for the framebuffer |
| GC9A01 240×240 TFT    | SPI2 (`User_Setup.h`)           | 16 bpp, DMA, target 60 fps                       |
| SG92R servo           | configured in `config.h`        | clamped to ±45° in `MotionBehaviors`             |
| BOOT button           | GPIO 0                          | hold 800 ms at boot for the provisioning portal  |

Pin numbers, WiFi defaults, and bridge credentials live in `config.h`
(copy `config.example.h` first; gitignored).

## Runtime flow

```
boot
  │
  Provisioning::load(cfg)               ← NVS, falls back to config.h
  │
  hold BOOT?  no nets known?
        └─────────────────► Provisioning::runPortal(cfg)   (blocks until save)
  │
  WifiMgr::tryConnect on each known network in turn
  │
  Personality::begin()
  Bridge::begin(host, port, token)
  │
  ┌───────────────── loop() ─────────────────┐
  │  tickSerialCommands                       │
  │  WifiMgr::tick           auto-reconnect   │
  │  Bridge::tick            ws.loop + poll   │
  │  AgentEvents::tick       linger expiry    │
  │  Personality::tick       state transitions│
  │  MotionBehaviors::tick   per-state motor  │
  │  Motion::tick            slew the PWM     │
  │  Face::tick              render + push    │
  └───────────────────────────────────────────┘
```

The architecture is **polled, not pushed**. Every renderer reads
state, nothing is shoved into them. That keeps modules independent —
adding an LED ring, a dashboard, or a second display means writing one
more `tick()` that observes the same state.

## Module map

| Module           | Files                              | Responsibility |
|------------------|------------------------------------|----------------|
| Entry            | `robot_v2.ino`                     | setup/loop, wires modules together, serial command shim |
| Config           | `config.h`, `config.example.h`     | compile-time defaults (WiFi, bridge) and feature flags |
| Provisioning     | `Provisioning.{h,cpp}`             | NVS-backed multi-network store + captive-AP portal |
| WiFi             | `WiFiManager.{h,cpp}`              | connect-with-timeout + auto-reconnect |
| Bridge transport | `BridgeClient.{h,cpp}`             | WS client, reconnect, JSON send helpers, session polling |
| Settings         | `Settings.{h,cpp}`                 | NVS-backed palette + face/text mode toggle, version counter |
| Event core       | `AgentEvents.{h,cpp}`              | dispatch WS frames, polled `AgentState`, callback registry, session latching |
| Personality      | `Personality.{h,cpp}`              | 13-state machine — see [PERSONALITY.md](PERSONALITY.md) |
| Motion primitives| `Motion.{h,cpp}`                   | servo attach + jog/waggle/thinking-osc/hold with safe-range clamp |
| Motion table     | `MotionBehaviors.{h,cpp}`          | per-state motor recipe — see [MOTION_BEHAVIORS.md](MOTION_BEHAVIORS.md) |
| Display driver   | `Display.{h,cpp}`                  | TFT init, full-screen sprite framebuffer, DMA push |
| Frame controller | `FrameController.{h,cpp}`          | tween FaceParams, layer modulators, dispatch to scene |
| Face renderer    | `FaceRenderer.{h,cpp}`             | draws eyes/mouth/face from `FaceParams` |
| Mood ring        | `MoodRingRenderer.{h,cpp}`         | colour halo around the face |
| Activity dots    | `ActivityDots.{h,cpp}`             | per-turn read/write tally pips |
| Effects overlay  | `EffectsRenderer.{h,cpp}`          | scrolling text streams (read/write content) |
| Scene composer   | `Scene.{h,cpp}`, `SceneTypes.h`    | composes face + ring + effects each frame |
| Text scene       | `TextScene.{h,cpp}`                | non-face text-status mode |
| String utility   | `AsciiCopy.{h,cpp}`                | UTF-8 → ASCII transliteration for the OLED font |
| Logging          | `DebugLog.h`                       | LOG_INFO/WARN/ERR/WS/EVT serial macros |

The face / display layer is described in
[DISPLAY_AND_FACE.md](DISPLAY_AND_FACE.md).

## Provisioning portal

`Provisioning::runPortal(cfg)` puts the chip in `WIFI_AP` mode with SSID
`robot-XXXX` (MAC-derived), serves a one-page HTML form on `/`, and
redirects everything else to `/` as a crude captive portal. `/save`
writes credentials back to NVS and reboots; `/forget` clears.
Multi-network support: up to `kMaxKnownNetworks` (currently 4) entries
are kept; `tryConnect` walks them in order at boot. The OLED shows the
AP SSID + IP via `Display::drawPortalScreen()` while the portal blocks.

## Serial commands

Available on the USB serial console at 115200 baud:

- `reboot` — `ESP.restart()`
- `provision-once` (aliases: `provision_once`, `provision`) — set a
  one-shot flag in NVS and reboot into the portal once
- `clear-provisioning` (alias: `forget-all`) — clear all saved networks
  and the legacy single-net entry

## Working on the firmware

- **No type-check / Arduino-compile is sufficient for testing servo or
  display work.** Test on-device. If no hardware is available, say so
  explicitly rather than guessing.
- TFT_eSPI bakes pin selection in at compile time via
  `robot_v2/User_Setup.h` — the runtime `config.h` cannot override
  display pins. Keep the two in sync if you change wiring.
- DMA on ESP32-S3 needs the Arduino-ESP32 core 3.x (IDF ≥ 2.0.14) and
  `USE_HSPI_PORT` defined in `User_Setup.h`. The sprite framebuffer must
  live in internal SRAM, **not** PSRAM (PSRAM is not DMA-safe for SPI
  master writes on S3).

## Required Arduino libraries

Install via the Library Manager:

- **WebSockets** by Markus Sattler
- **ArduinoJson** by Benoit Blanchon (v7+)
- **TFT_eSPI** by Bodmer (configure via `robot_v2/User_Setup.h`)
- **ESP32Servo** by Kevin Harrington (≥ 3.0 for Arduino-ESP32 core 3.x)

Board: ESP32-S3 with Arduino-ESP32 core 3.x.

## Conventions

- **One namespace per module**, no classes unless there's genuine state
  to encapsulate.
- **Polled state is primary**; callbacks are offered but optional. The
  single `EventHandler` slot in `AgentEvents` is taken by `Personality`.
- **All display strings go through `AsciiCopy::copy`.** Anything from
  vendor payloads can carry UTF-8 punctuation that the OLED font has no
  glyphs for.
- **Non-blocking everywhere except `setup()`** and the provisioning
  portal (which intentionally blocks).
- **`config.h` is gitignored**; secrets stay local.
