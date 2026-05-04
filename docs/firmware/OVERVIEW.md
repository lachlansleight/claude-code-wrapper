# Firmware Overview (`robot_v3/`)

ESP32-S3 firmware that connects to the agent bridge over WebSocket and
expresses behaviour via a procedural face on a 240×240 GC9A01 round TFT
plus a single hobby servo for arm motion. WiFi + bridge credentials are
provisioned on-device via a captive-AP portal.

Unlike `robot_v2`, `robot_v3` does **not** derive behaviour via a single
monolithic `Personality` state machine. Behaviour is composed from:

- `VerbSystem` (discrete “what it’s doing”)
- `EmotionSystem` (continuous “how it feels”)

Face + motion consume a single effective `Face::Expression` derived from
those systems (see [`BEHAVIOUR.md`](BEHAVIOUR.md)).

For the cross-system tour, read
[`AGENT_TO_ROBOT_PIPELINE.md`](../AGENT_TO_ROBOT_PIPELINE.md) first.

## Hardware

| Component             | Pin / bus                       | Notes                                            |
|-----------------------|---------------------------------|--------------------------------------------------|
| ESP32-S3 dev board    | —                               | needs internal SRAM ≥ 256 KB for the framebuffer |
| GC9A01 240×240 TFT    | SPI2 (`User_Setup.h`)           | 16 bpp, DMA, target 60 fps                       |
| SG92R servo           | configured in `config.h`        | clamped to ±45° in `MotionBehaviors`             |
| BOOT button           | GPIO 0                          | hold 800 ms at boot for the provisioning portal  |

Pin numbers, WiFi defaults, and bridge credentials live in
`robot_v3/src/hal/config.h` (copy `config.example.h` first; gitignored).

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
  EventRouter::begin()
  Bridge::begin(host, port, token)
  │
  ┌───────────────── loop() ─────────────────┐
  │  tickSerialCommands                       │
  │  WifiMgr::tick           auto-reconnect   │
  │  Bridge::tick            ws.loop + poll   │
  │  EventRouter::tick       dispatch + behaviour ticks
  │  MotionBehaviors::tick   expression-driven motion
  │  Motion::tick            slew the PWM     │
  │  SceneContextFill::fill  build SceneContext
  │  Face::tick(ctx)         render + push
  └───────────────────────────────────────────┘
```

The architecture is **polled, not pushed**. Every renderer reads
state, nothing is shoved into them. That keeps modules independent —
adding an LED ring, a dashboard, or a second display means writing one
more `tick()` that observes the same state.

## Module map

| Module           | Files                              | Responsibility |
|------------------|------------------------------------|----------------|
| Entry / composition | `robot_v3/robot_v3.ino` | setup/loop, wires modules together |
| Config | `robot_v3/src/hal/config.h`, `config.example.h` | compile-time defaults (WiFi, bridge) |
| Provisioning | `robot_v3/src/hal/Provisioning.*` + `ProvisioningUI.*` | NVS-backed creds store + captive portal, plus optional display glue |
| WiFi | `robot_v3/src/hal/WiFiManager.*` | connect-with-timeout + auto-reconnect |
| Display | `robot_v3/src/hal/Display.*` | TFT init, sprite framebuffer, DMA push |
| Motion (HAL) | `robot_v3/src/hal/Motion.*` | servo primitive + slew + hold override |
| Settings | `robot_v3/src/hal/Settings.*` | NVS-backed palette + face/text mode, version counter |
| Bridge transport | `robot_v3/src/bridge/BridgeClient.*` | WS client + JSON parse → callbacks |
| AgentEvents | `robot_v3/src/agents/AgentEvents.*` | semantic `agent_event` parsing into `AgentState` (side-effect free) |
| BridgeControl | `robot_v3/src/agents/BridgeControl.*` | parses non-semantic WS controls (palette/mode/servo) |
| Behaviour | `robot_v3/src/behaviour/{VerbSystem,EmotionSystem}.*` | verb + emotion state machines |
| Router | `robot_v3/src/app/EventRouter.*` | firmware-owned mapping + deterministic dispatch ordering |
| SceneContextFill | `robot_v3/src/app/SceneContextFill.*` | builds `Face::SceneContext` from behaviour + agent state |
| Face stack | `robot_v3/src/face/*` | `SceneTypes`, renderers, `FrameController::tick(ctx)` |
| MotionBehaviors | `robot_v3/src/hal/MotionBehaviors.*` | expression-indexed motor behaviour table |
| Core utils | `robot_v3/src/core/*` | logging + string sanitization |

The face / display layer is described in [`DISPLAY_AND_FACE.md`](DISPLAY_AND_FACE.md).
The behaviour model is described in [`BEHAVIOUR.md`](BEHAVIOUR.md).

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
  `robot_v3/User_Setup.h` — the runtime `config.h` cannot override
  display pins. Keep the two in sync if you change wiring.
- DMA on ESP32-S3 needs the Arduino-ESP32 core 3.x (IDF ≥ 2.0.14) and
  `USE_HSPI_PORT` defined in `User_Setup.h`. The sprite framebuffer must
  live in internal SRAM, **not** PSRAM (PSRAM is not DMA-safe for SPI
  master writes on S3).

## Required Arduino libraries

Install via the Library Manager:

- **WebSockets** by Markus Sattler
- **ArduinoJson** by Benoit Blanchon (v7+)
- **TFT_eSPI** by Bodmer (configure via `robot_v3/User_Setup.h`)
- **ESP32Servo** by Kevin Harrington (≥ 3.0 for Arduino-ESP32 core 3.x)

Board: ESP32-S3 with Arduino-ESP32 core 3.x.

## Conventions

- **One namespace per module**, no classes unless there's genuine state
  to encapsulate.
- **Polled state is primary**; callbacks are offered but optional.
- **All display strings go through `AsciiCopy::copy`.** Anything from
  vendor payloads can carry UTF-8 punctuation that the OLED font has no
  glyphs for.
- **Non-blocking everywhere except `setup()`** and the provisioning
  portal (which intentionally blocks).
- **`config.h` is gitignored**; secrets stay local.
