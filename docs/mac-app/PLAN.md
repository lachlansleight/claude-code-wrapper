# Mac app: finalised plan

**Status:** Phase 1 done (`robot_v3/host/`). Phase 2 done (`sultana-dock/`,
see its `README.md`). Phase 3 next.
**Written:** 2026-10-24 against `main` at `0baabc3`.
**Names:** the robot and project are called Sultana. The Mac app is
**Sultana Dock**, the host build of the firmware core is **SultanaCore**
(C API prefix `sultana_core_`), and its CLI is **`sultana-replay`**.
**Supersedes:** the "Recommended architecture", "Alternatives considered"
and "Work plan" sections of [`README.md`](README.md), and the TypeScript /
JavaScriptCore material in [`ENGINE.md`](ENGINE.md) and
[`MACOS.md`](MACOS.md). Everything else in those files still applies.

---

## 1. Decision

**The Mac app compiles the firmware's own behaviour and face C++ and runs it
in-process.** There is no port. The firmware stays the single copy of the
logic; the Mac app and the robot run the same object code modulo the HAL.

Why this instead of the TypeScript port the discovery recommended:

- The discovery rejected a Swift port as "a third copy of logic that's still
  changing". That applies equally to the TS port. The editor's engine is
  already a second copy and `ENGINE.md` §3–§4 lists where it has drifted.
- The firmware code involved is nearly pure C++. Across the 21 source files
  the Arduino surface is `millis()`, `random()` / `randomSeed()`,
  `esp_random()`, `Serial.printf` and a `TFT_eSprite` with ten drawing
  methods. No `PROGMEM`, no `String` on the hot path, and ArduinoJson is
  header-only.
- It was tried before this plan was written (§10): all 21 files compiled
  unmodified on macOS against ~150 lines of stub headers, a Claude-shaped
  replay produced the exact verb sequence `ENGINE.md` §2 describes, and the
  firmware's own renderer drew frames at its own 30 fps throttle.

What this buys beyond the Dock app:

- **Quirk-for-quirk fidelity by construction.** Goal 3 of the discovery is
  automatic, and every firmware fix reaches the Mac app on rebuild.
- **A host test harness for the firmware.** The repo has no test suite. The
  same host build is a CLI: event log in, frames and state out.
- **A path to an exact simulator in the face editor**, by compiling the same
  core to WebAssembly later. Not in scope here.

Known limits, accepted:

- One face per process. The firmware keeps its state in `static` variables.
  The TS plan had the same limit. Multi-face needs a firmware refactor (§9).
- Text and debug render modes draw nothing on the host in v1. Face mode only.
- The RNG, the physical servo lag and the panel's colour response are not
  reproduced. §4.7 lists exactly what is and isn't identical.

---

## 2. Architecture

```
bridge  (node plugin/dist/index.js)
   │  ws://host:port/ws?token=…      ← the same broadcast the robot receives
   ▼
Swift app (AppKit, main thread)
   ├─ BridgeSocket        URLSessionWebSocketTask; robot's ping/reconnect policy
   │     │  text frame
   │     ├──────────────► Signals        bounce / badge from raw frames, BEFORE the latch
   │     └──────────────► SultanaCore      C API over the firmware (libSultanaCore.a)
   │                        ├─ EventRouter → AgentEvents latch → VerbSystem / EmotionSystem
   │                        ├─ SceneContextFill → FrameEffective → FrameController → Scene
   │                        ├─ MotionBehaviors → Motion  (servo write = store the angle)
   │                        └─ Display_host: 240×240 RGB565 sprite; pushFrame() = copy out
   │     ◄──────────────── send callback: {"type":"request_sessions"} every 5 s while unlatched
   ├─ display link (60 Hz) → sultana_core_tick(now) → frame changed? → RGBA → CGImage
   ├─ DockTileView        NSDockTile.contentView; pushed only when pixels changed, ≤ 30 Hz
   ├─ RobotPanel          NSPanel: body art + two arm layers (±θ, rate-limited) + round screen
   └─ Preferences         host/port in UserDefaults, token in Keychain, bounce + fps options
```

Threading contract: **the core is single-instance and not thread-safe.**
Every call into it happens on the main thread. WebSocket callbacks hop to
main before injecting.

---

## 3. Repo layout

```
robot_v3/
  src/                        unchanged firmware sources
  host/                       NEW: host build of the firmware core
    stubs/                    Arduino.h, TFT_eSPI.h, ESP32Servo.h, Preferences.h, esp_random.h
    hal/                      Display_host.cpp, Bridge_host.cpp  (host impls of src/hal/Display.h,
                              src/bridge/BridgeClient.h)
    core/                     sultana_core.h, sultana_core.cpp     (C API, §4.3)
    cli/                      sultana_replay.cpp, png.cpp           (replay → trace + frames, §4.8)
    third_party/              ArduinoJson 7.4.3 single header (MIT, generated from the installed
                              Arduino library); TFT_eSPI licence for the ported primitives
    fixtures/                 recorded WS logs, golden traces, scenarios.mjs that drove them
    Makefile                  builds build/libSultanaCore.a + build/sultana-replay; make test
sultana-dock/
  SultanaDock.xcodeproj       targets: SultanaCore (static library), Sultana Dock (app)
  SultanaDock/                Swift sources, Info.plist, assets
scripts/
  record-ws.mjs               NEW: subscribe to the bridge, append frames to a JSONL log
```

The Xcode `SultanaCore` target compiles the same firmware file list as
`FIRMWARE_SRCS` in the `Makefile`, by file reference into `robot_v3/`. The
Makefile is the canonical list; keep the two in sync. (CMake was the
original plan; it isn't installed here and plain make covers the need.)

---

## 4. The host core (`robot_v3/host/`)

### 4.1 What gets compiled

| Compiled unmodified | Not compiled (hardware only) |
|---|---|
| `app/EventRouter.cpp`, `app/SceneContextFill.cpp` | `robot_v3.ino` |
| `behaviour/EmotionBlend.cpp`, `EmotionSystem.cpp`, `VerbSystem.cpp` | `hal/WiFiManager.cpp` |
| `agents/AgentEvents.cpp`, `agents/BridgeControl.cpp` | `hal/Provisioning.cpp`, `hal/ProvisioningUI.cpp` |
| `core/AsciiCopy.cpp` | `hal/SerialDebugging.cpp` |
| `face/*.cpp` (all eleven) | `hal/Display.cpp` → replaced by `host/hal/Display_host.cpp` |
| `hal/Motion.cpp`, `hal/MotionBehaviors.cpp`, `hal/Settings.cpp` | `bridge/BridgeClient.cpp` → replaced by `host/hal/Bridge_host.cpp` |
| | `bridge/BridgeDiscovery.cpp` |

`Motion.cpp` and `Settings.cpp` compile unmodified because their hardware
dependencies (`ESP32Servo.h`, `Preferences.h`) are stubbed at the header
level. `Motion.cpp` also includes `src/config.h` for `SERVO_PIN`. That file is
per-machine (there is a `config.example.h` beside it) and today it is
committed with WiFi credentials and a bridge token in it, which should be
fixed independently of this plan. **Resolved in Phase 1:** the host build
uses that same `config.h`. `Motion.cpp` includes it as `"../config.h"`,
relative to itself, which the compiler resolves before any include path, so
it can't be shadowed without editing firmware. The firmware needs the file
anyway; the Makefile stops with a clear message if it's missing.

### 4.2 Stubs

Everything the compiled set needs, and nothing more:

| Header | Must provide |
|---|---|
| `Arduino.h` | `millis()`, `micros()`, `delay()`, `random(n)`, `random(a,b)`, `randomSeed()`, `PI`, `min`/`max`/`constrain`, `Serial.printf`, a minimal `String` (used only by `Settings.cpp` key names) |
| `esp_random.h` | `esp_random()` |
| `ESP32Servo.h` | `class Servo { setPeriodHertz; attach → true; write(angle) stores it }` |
| `Preferences.h` | in-memory `begin/end/clear/putUShort/getUShort/putBool/getBool/putULong/getULong` |
| `TFT_eSPI.h` | `TFT_eSPI` (no-op init/DMA methods, `color565`), `TFT_eSprite` with `createSprite`, `created`, `getPointer`, `fillSprite`, `fillRect`, `drawFastHLine`, `drawLine`, `drawCircle`, `fillCircle`, `fillSmoothCircle`, `drawPixel`, plus no-op text methods (`setTextSize/Datum/Color/Font`, `drawString`, `fontHeight`, `textWidth`). Constants `TFT_BLACK`, `TFT_WHITE`, `TL_DATUM`, `MC_DATUM`. |

**Rasteriser fidelity.** `drawLine`, `drawCircle` and `fillCircle` must use
TFT_eSPI's own algorithms (Bresenham line; midpoint circle; `fillCircle`'s
horizontal-span variant) so edges land on the same pixels as the panel.
`fillSmoothCircle` is the only anti-aliased primitive the face uses (pupils);
port it from `TFT_eSPI.cpp` (`fillSmoothCircle`, around line 4246 in the
copy under `~/Documents/Arduino/libraries/TFT_eSPI-master`). TFT_eSPI is MIT
licensed; keep the attribution header on the copied functions.

**Clock.** `millis()` returns a global the C API sets on every `init`,
`inject` and `tick` call. It must **never be 0 at runtime**: several
firmware fields use `0` as "unset" (`sLastEffectsMs`, `sPendingSnapSinceMs`,
`emotionLastAdvanceMs`, `lingerUntil`). The host maps its monotonic clock to
`uint32_t` with a +10 000 ms offset. It wraps after 49.7 days, as the device
does.

**RNG.** A seedable xorshift behind `random()` / `esp_random()` so the CLI is
deterministic with `--seed`. The app seeds from time.

### 4.3 C API (`host/core/sultana_core.h`)

```c
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*sultana_send_fn)(const char* json, void* user);    /* outbound WS text */
typedef void (*sultana_log_fn)(const char* line, void* user);     /* firmware LOG_* lines */

void   sultana_core_init(uint32_t now_ms, uint32_t rng_seed);        /* = setup() minus WiFi/provisioning */
void   sultana_core_set_send_callback(sultana_send_fn, void* user);
void   sultana_core_set_log_callback(sultana_log_fn, void* user);    /* NULL = stderr */
void   sultana_core_set_connected(bool connected, uint32_t now_ms);  /* → EventRouter::onBridgeConnection */
bool   sultana_core_inject(const char* json, size_t len, uint32_t now_ms); /* one WS text frame; false if not JSON */
bool   sultana_core_tick(uint32_t now_ms);                           /* one loop() iteration; true if a frame was pushed */
const uint16_t* sultana_core_frame565(void);                         /* 240×240, valid until the next tick */
uint32_t sultana_core_frame_count(void);                             /* frames pushed since init */
void   sultana_core_frame_rgba(uint8_t* out);                        /* 240×240×4, 5/6-bit channels expanded */
int8_t sultana_core_arm_offset_deg(void);                            /* Motion::currentOffsetDeg(), already ±45-clamped and rounded */
size_t sultana_core_state_json(char* out, size_t cap);               /* debug snapshot: verb, effective verb, expression, v/a, latch, pending, read/write counts, arm */
#ifdef __cplusplus
}
#endif
```

- `now_ms` is the host's monotonic clock from any epoch; the core adds the
  +10 000 ms offset itself, so a host can start at 0.
- `sultana_core_init` runs `Settings::begin`, `Display::begin`, `Motion::begin`,
  `MotionBehaviors::begin`, `Face::begin`, `EventRouter::begin`, then wires
  `Bridge::onMessage(&EventRouter::onBridgeMessage)` and
  `Bridge::onConnection(&EventRouter::onBridgeConnection)`. It never calls
  `Bridge::begin`.
- `sultana_core_inject` parses with ArduinoJson (`JsonDocument`, same as
  `BridgeClient::handleText`) and calls the registered message handler. A
  parse failure logs and drops the frame, as the device does.
- `sultana_core_tick` is `loop()` without WiFi and serial:
  `Bridge::tick` (no-op) → `EventRouter::tick` → `SceneContextFill::fill` →
  `Face::tickEffectiveParams` → `MotionBehaviors::tick` → `Motion::tick` →
  `Face::tick`. Returns whether `Display::pushFrame` ran. The face throttles
  itself to 33 ms / 16 ms internally, so calling tick at 60 Hz or 120 Hz
  gives the same behaviour as the device.
- `inject` takes `now_ms` because the firmware stamps state with `millis()`
  inside handlers (`VerbSystem::setVerb` records `enteredAt`). On the device
  that stamp is within a millisecond of receipt; passing the receive time
  keeps it so on the host.
- `Bridge_host.cpp` implements `Bridge::requestSessions()` by emitting
  `{"type":"request_sessions"}` through the send callback. `EventRouter::tick`
  calls it every 5 s while connected and unlatched; the Swift socket must
  forward it or the latch will never recover after a reconnect.
- Face mode is forced: after each inject the API resets the render mode to
  face. `config_change.display_mode` is otherwise obeyed by the compiled
  `BridgeControl`, and text mode would draw a blank screen on the host.

### 4.4 Frame output

- The sprite is RGB565, as on the device. `sultana_core_frame_rgba` expands
  each channel the way a panel does (`r8 = (r5 << 3) | (r5 >> 2)`, etc.).
- The host decides whether anything changed by `memcmp` of the 115 KB 565
  buffer against the last one it consumed. `Face::tick` renders on its
  schedule whether or not pixels moved; the memcmp is what makes an idle or
  sleeping face free for the Dock.
- The buffer is square. The GC9A01 is round. **The host masks to a circle**
  when drawing; the core does not.

### 4.5 Settings

`Settings.cpp` runs on the in-memory `Preferences` stub. The palette can
still be changed live via the bridge's `setColor` (the editor's robot
buttons) and `BridgeControl` applies it. Nothing persists across launches in
v1. If persistence is wanted later, the stub gets load/save hooks the app
wires to `UserDefaults`.

### 4.6 Logging

`DebugLog.h`'s `LOG_*` macros expand to `Serial.printf`. The `Serial` stub
routes through the log callback; the CLI prints to stderr, the app to
`os_log` and a debug window if one is built.

### 4.7 What is and isn't identical to the robot

| Identical | Not identical |
|---|---|
| Every rule in `ENGINE.md` §2 (latch, verbs, overlays, linger, strain, emotion dynamics, held drivers, activity dots) | Blink and glance timing (random) |
| Frame cadence (33 / 16 ms throttle) | Very large frames: the ESP32 can fail to allocate the `JsonDocument` and drop the frame; the host never will |
| Face geometry, mood ring, stream effects, pixel placement of hard-edged primitives | Pupil anti-aliasing: identical only once `fillSmoothCircle` is ported exactly |
| Arm angle commanded each tick (`Motion::currentOffsetDeg`) | Arm angle *reached*: the SG92R lags; the app rate-limits the drawn arm (§5.6) |
| Body bob (tracks the commanded angle on the device too) | Panel colour response and brightness |
| | Text / debug render modes (blank on host) |

### 4.8 CLI harness (`sultana-replay`)

```
sultana-replay fixtures/claude-turn.jsonl [--seed 1] [--trace] [--frames out/ --frame-every 250]
               [--step 1] [--tail 4000] [--frame-hash] [--final-state] [--logs]
```

- **Input:** JSONL, one line per WebSocket text frame as the robot would
  receive it, wrapped with a receive time:
  `{"t": <ms since recording start>, "frame": {…}}`. Also accepted:
  `{"t", "text": "<raw frame>"}` and `{"t", "connected": true|false}` for
  socket drops. `scripts/record-ws.mjs` writes this from a live bridge. The
  app's replay mode (§5.8) reads the same file, so one recording drives both.
- **Clock:** the log's `t` drives `now_ms` in `--step` increments, default
  1 ms (16 ms, the original plan, made the 33 ms face throttle fire every
  48 ms). Output depends only on the log and `--seed`.
- `--trace` prints every received frame (`<<`), every frame the core sends
  (`>>`), and a line whenever the effective verb, expression, latch or
  pending permission changes, with valence, activation and arm angle.
- `--frames` writes PNGs with a built-in writer (no stb dependency).
  `--frame-hash` hashes every pushed frame, to check whether anything on
  screen changed.
- **Golden traces:** `fixtures/NAME.trace.txt` is the `--trace --seed 1`
  output for `NAME.jsonl`. `make test` diffs them; `make golden` rewrites
  them after an intended behaviour change. This is the firmware's first
  regression test.
- **Fixtures** come from `fixtures/scenarios.mjs`, which posts scripted
  Claude Code hooks through a real bridge while `record-ws.mjs` records.
  `scripts/simulate-hooks.js` was not used: it is randomised and never
  produces Executing, Straining or AttractingAttention.

Build: `make` in `robot_v3/host`. Clang or gcc; on Windows, MSYS2 (untried).
`##__VA_ARGS__` in `DebugLog.h` rules out plain MSVC.

---

## 5. The Mac app (`sultana-dock/`)

Swift, AppKit, **minimum macOS 14** (`NSScreen.displayLink`,
`SMAppService`). Not sandboxed in v1. No App Store.

### 5.1 BridgeSocket

- `URLSessionWebSocketTask` to `ws://<host>:<port>/ws?token=<token>`.
- Mirror the robot: ping every 15 s, 3 s timeout, two consecutive failures
  → close → reconnect after 2 s. A 401 at the handshake is a bad token;
  surface it in the UI rather than retrying silently.
- `didOpen` → `sultana_core_set_connected(true)`; close or error →
  `set_connected(false)`. This is the device's `WStype_CONNECTED` /
  `WStype_DISCONNECTED` timing, not the `hello` frame.
- Every text frame goes to **both** `Signals` and `sultana_core_inject`, on the
  main thread, in arrival order.
- Outbound: the core's send callback writes to the socket. Only
  `request_sessions` is ever sent.

### 5.2 Tick loop

- `NSScreen.main.displayLink(target:selector:)` at the display's rate.
  Each callback: `sultana_core_tick(now)`; if it returns true and the 565
  buffer differs from the last consumed one, convert to RGBA, build a
  `CGImage` over the buffer (`CGDataProvider`, no extra copy), and hand it to
  the Dock tile and the robot window. *(Built: the RGBA buffer belongs to a
  reused `CGContext` and `makeImage()` snapshots it. The display link is
  recreated when the screen configuration changes.)*
- Hold a `ProcessInfo.beginActivity(.userInitiatedAllowingIdleSystemSleep)`
  token while connected so App Nap doesn't stall the timer. *(Built in
  Phase 2: held while connected or replaying.)*
- On `NSWorkspace.willSleepNotification` stop ticking; on `didWake` resume
  and let the socket reconnect. The firmware copes with a large `dt`
  (exponential smoothing, absolute-time comparisons, `Motion` clamps `dt` at
  0.5 s). The display link also stops while the screen is asleep; same story.

### 5.3 Dock tile

- `NSApp.dockTile.contentView` = a view holding the latest `CGImage`,
  clipped to a circle. Corners outside the circle are transparent. Call
  `dockTile.display()` only when a new image arrived, and no more often than
  the **Dock fps cap** preference (default 30; offer 15 and 10).
  *(Built: the screen fills 80% of the tile inside a thin graphite bezel
  (86%), matching Apple's icon grid so it sits at the same size as other
  Dock icons. The Dock shows tiles smaller than 240 px, so the face is
  smoothed when downscaled and nearest-neighbour only when upscaled. CPU
  was measured at each cap; see `sultana-dock/README.md`. 30 fps stays the
  default.)*
- The static bundle icon (shown when the app isn't running) is a still of
  the sleeping face, made with Icon Composer so Tahoe doesn't squircle it.
  *(Phase 2 ships a plain asset-catalog icon rendered from a firmware frame
  by `sultana-dock/tools/make_app_icon.py`; the Icon Composer version waits
  for the Phase 5 design.)*
- `applicationShouldHandleReopen` (click on the icon) shows the robot window.
- Dock menu (`applicationDockMenu`): Show Robot, Always on Top ✓, Replay
  Log…, Preferences…, Quit.

### 5.4 Signals (bounce, later badge)

Decoded in Swift from each raw frame, **before** the core's latch, with a
minimal `Decodable` (`type`, `session_id`, `event.kind`, `event.cause`,
`event.text`). This is what lets a completion in a non-latched session still
bounce.

| Frame | Action | Preference |
|---|---|---|
| `agent_event` / `turn.ended` | `NSApp.requestUserAttention(.informationalRequest)` | style: informational (default) / critical; off |
| `agent_event` / `notification` with `text` starting `"Claude needs"`, or `permission.requested` | same | on by default; off |
| app becomes active | nothing: macOS ends critical requests itself | — |

Bounce is a no-op while the app is frontmost. macOS activates a freshly
launched app, so with no window to show (Phase 2) the app hides itself
right after launch and after a Dock click, which hands focus back to the
previous app. The Phase 3 robot window is a non-activating panel for the
same reason. Badge counts are later work (§7).

Found while building: `requestUserAttention` returns 0-based ids that are
reused, so **0 is a valid id, not a failure**. Informational requests
return 0 too. Cancelling stored ids risks cancelling a different, newer
request, so the app stores none.

### 5.5 Robot window

- `NSPanel` with `[.titled, .closable, .resizable, .nonactivatingPanel,
  .fullSizeContentView]`, `titlebarAppearsTransparent`, title hidden,
  `hidesOnDeactivate = false`, `contentAspectRatio` from the body art,
  `minSize`, `setFrameAutosaveName`.
- **Always on top** toggle: `level = .floating` plus
  `collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]`; back to
  `.normal` / default when off.
- Start with this titlebar style. The borderless "desktop pet" style from
  `MACOS.md` is a later toggle, not v1.
- Stop updating layers while `occlusionState` says the window isn't visible.
  Keep ticking the core and the Dock.

### 5.6 Drawing the robot

Layer tree per `MACOS.md`: body, `armLeft`, `armRight`, round `screen`.

- `screen.contents` = the frame `CGImage`, `magnificationFilter = .nearest`,
  `cornerRadius = w/2`, `masksToBounds = true`. Optionally snap the screen
  layer to a whole multiple of 240 px so every panel pixel is the same size.
- Arms: `θ_cmd = sultana_core_arm_offset_deg()`. The gear mirrors one servo
  to both arms, so `armRight` rotates by `+θ` and `armLeft` by `−θ` about
  their pivots; which sign is "up" is set by the art (Phase 5).
- **Servo lag:** rate-limit the *drawn* angle toward `θ_cmd` at about
  500 °/s (the SG92R is spec'd near 0.1 s per 60°). Tune by eye against the
  real robot. Do not lag the body bob; the firmware's bob follows the
  commanded angle on the device too.
- `CATransaction.setDisableActions(true)` around each frame's updates.

### 5.7 Preferences and secrets

- Host and port in `UserDefaults`; token in the Keychain
  (`kSecClassGenericPassword`). No default token: with nothing configured the
  app shows a "Not configured" state, matching the bridge's refusal to start
  without `BRIDGE_TOKEN`.
- Preferences window: host, port, token (secure field), "Import from
  .env…" (reads `BRIDGE_TOKEN` / `BRIDGE_PORT` from a chosen file), bounce
  options, Dock fps cap, launch at login (`SMAppService.mainApp`).
- Add `NSLocalNetworkUsageDescription` for bridges on other machines.
- *(Built: `SULTANA_BRIDGE_HOST` / `_PORT` / `_TOKEN` environment variables
  override the stored settings without changing them. Ad hoc signed builds
  get a Keychain prompt after every rebuild; an env token skips the
  Keychain entirely.)*

### 5.8 Replay mode (developer feature, built early)

Dock menu ▸ Replay Log… opens a JSONL fixture (§4.8) and feeds it to the
core without a bridge. Two clocks:

- **wall clock**: frames injected at their `t` offsets; normal ticking.
- **log clock**: `now_ms` comes from the log in 16 ms steps, same seed as
  the CLI. Frames are then byte-identical to `sultana-replay --frames`, which is
  the frame-exact check in §8.

This is also how most of Phase 2–3 gets developed: no agent, no bridge.
*(Built in Phase 2: the wall-clock variant, from the menus or with
`SULTANA_REPLAY=/path/log.jsonl` at launch. `SULTANA_DEBUG_DIR` makes the
app write the tile as drawn, a state snapshot and an event log, which is
how the app was verified without watching the screen.)*

### 5.9 Distribution

Run from Xcode for yourself. Developer ID signing and `notarytool` only if
the app is shared. The Local Network prompt and `SMAppService` both behave
better with a signed build, so sign early if either misbehaves.

---

## 6. Phases

Estimates are rough and assume one person.

### Phase 1 — host core and CLI — **done**

Delivered in `robot_v3/host/` (see its `README.md`):

1. Stubs, host HAL, `sultana_core` C API, Makefile.
2. TFT_eSPI's sprite primitives ported line for line, including the
   anti-aliased `fillSmoothCircle`; text calls are no-ops.
3. `sultana-replay` with `--trace`, `--frames`, `--seed`, `--frame-hash`.
4. `scripts/record-ws.mjs`, `fixtures/scenarios.mjs`, and two recorded
   fixtures (`claude-turn`, `two-sessions`) with golden traces.
5. Seeded xorshift RNG, +10 000 ms clock offset, forced face mode.

Checked: the `claude-turn` trace goes Sleeping → Waking → Thinking →
Reading (bridged across a two-tool burst by the linger) → Thinking →
Writing → Thinking → Executing → Straining at exactly +5 s → Thinking →
AttractingAttention → Thinking → Celebrating → Neutral → Sleeping, with the
timings in `ENGINE.md` §2. Pupils carry TFT_eSPI's anti-aliased edge. Same
log and seed give byte-identical PNGs; different seeds change idle blinks
and glances. `make test` passes and fails on a corrupted golden.

Not checked: the ESP32 build. `arduino-cli` can't run on this Mac at the
moment (its x86-only `ctags` needs Rosetta, which isn't installed), so no
firmware build was possible on any branch. Phase 1 changes no firmware
source, and Arduino compiles only the sketch root and `src/`, so
`robot_v3/host/` is copied into the build folder but never compiled.

### Phase 2 — app shell and Dock tile — **done**

Delivered in `sultana-dock/` (see its `README.md`):

1. `SultanaDock.xcodeproj`: a `SultanaCore` static library target over the
   Phase 1 sources, and the `Sultana Dock` app target with a bridging header
   for `sultana_core.h`. The app's Swift lives in a synchronized folder.
2. `BridgeSocket` with the robot's ping / pong / reconnect policy, a
   distinct state for a rejected token, and the core's `request_sessions`
   forwarded.
3. Display-link tick loop, change detection, RGBA conversion, `CGImage`.
4. Dock tile with circular clip, bezel and fps cap; bounce on `turn.ended`
   from any session; attention bounce. The app hides itself when it has no
   window, so bounces aren't swallowed.
5. Settings window (SwiftUI), Keychain token, `.env` import.
6. Replay mode, wall-clock variant. Also, from Phase 3: the App Nap
   activity token.

Checked against a real bridge (scripted Claude Code hooks through the
bridge's Claude adapter, not a live Claude Code session):

- The tile followed `claude-turn` through every state and latched and
  unlatched as the firmware does; `two-sessions` bounced for both turn ends,
  including the one the face's latch dropped.
- A frozen bridge (SIGSTOP) was dropped after two missed pongs at 33 s and
  reconnected within a second of resuming. A killed and restarted bridge
  was back within the 2 s retry, and a new session latched.
- Replaying `claude-turn.jsonl` produced the same states and bounced at the
  recorded times, then reconnected live.
- The Settings view renders correctly offscreen, and `.env` parsing works.
  The window, the open panels and the Keychain write were **not**
  click-tested.
- Not checked: a live Claude Code session end to end, and watching the
  bounce happen (the request ids are consistent with it working; see §5.4).

### Phase 3 — robot window and polish (Mac, ~2 days)

1. `NSPanel` per §5.5 with placeholder body and arm art.
2. Layer tree, round screen, arm rotation, servo-lag rate limit.
3. Always-on-top and all-Spaces toggle; Dock menu; reopen-on-click.
4. Launch at login; sleep / wake handling (the App Nap token landed in
   Phase 2).
5. Replay mode, log-clock variant; frame-exact check against the CLI.

**Done when:** the window can be resized, floated over a full-screen app,
and survives sleep/wake; a log-clock replay in the app matches the CLI's
PNGs byte for byte.

### Phase 4 — behaviour fixes and docs (bridge + firmware, ~1 day)

These are the discovery's "known gaps". They are fixed at the source so the
robot gets them too, and they land *after* Phase 3 so the first side-by-side
comparison is against unchanged behaviour.

1. **Disconnect reset** (`EventRouter::onBridgeConnection(false)`): call
   `VerbSystem::clearVerb()`. `AgentEvents::notifyConnection` already clears
   the latch and `pending_permission`, so the held drivers release on the
   next tick. Sleep / wake and bridge restarts are routine on a Mac; today
   they leave the face stuck in Executing → Straining → sad.
2. **Error turns** (`plugin/src/adapters/claude.ts`): `StopFailure` →
   `turn.ended` with `cause: "error"`, `PostToolUseFailure` →
   `activity.failed`. Then in `EventRouter::onAgentEvent`, celebrate only
   when `cause` is `completed`; for other causes `clearVerb()` and a small
   negative impulse. The exact reaction is an open decision (§8).
3. **Stale docs** listed in `ENGINE.md` §9, plus the robot_v2 "Firmware
   quick-map" in `CLAUDE.md`, and a `docs/README.md` entry for the host
   build and the Mac app.
4. Rebuild and flash the firmware; the sketch is near the partition limit,
   so check the size line.

### Phase 5 — design (you; can run alongside 2–3)

Unchanged from the discovery `README.md`: body and arm art, each arm's
pivot, where the screen sits and its size, which way `+θ` moves the arms,
the rest pose at offset 0, what fills the Dock tile's corners, and the
window's final look.

---

## 7. Later, not in this plan

- **WebAssembly build** of `robot_v3/host` for the face editor: an exact
  firmware preview beside the editable TS engine.
- **Menu-bar face** (`NSStatusItem`) for people who auto-hide the Dock.
- **Badge** with the number of sessions waiting on the user, and a Dock
  menu listing sessions. Needs per-session bookkeeping in `Signals` only.
- **Multiple faces** (one per session). Needs the firmware's `static` state
  moved into an instance struct; a good change for both targets, not a small
  one.
- **Bridge supervision**: the app spawning `node plugin/dist/index.js` as a
  child process, so the Mac is the one thing to launch and the robot's
  subnet sweep finds it. Optional, off by default.
- **Text / debug mode on the host** by porting TFT_eSPI's built-in font 1
  (`glcdfont`) into the sprite stub.
- **Desktop-pet window style** from `MACOS.md`.

---

## 8. Open decisions

| Decision | Plan's default | Alternatives |
|---|---|---|
| App name and bundle id | **decided:** Sultana Dock, `com.lachlansleight.sultana.dock` | — |
| Bounce on `turn.ended` | once (`.informationalRequest`), any session, any cause | `.criticalRequest`; only `cause == completed` |
| Bounce on attention | on | off |
| Robot window style | titlebar-transparent panel first | borderless pet window |
| Error-turn reaction (Phase 4) | no Celebrating; `clearVerb()`; impulse `(−0.3, 0)` | leave as today (Celebrating on every `turn.ended`) |
| Dock fps cap default | **decided:** 30, after measuring (see `sultana-dock/README.md`) | — |
| `SERVO_PIN` for the host build | **resolved:** the firmware's own `config.h` (§4.1) | — |
| Committed `robot_v3/src/config.h` with credentials | out of scope here; untrack it and rotate the token | leave as is |
| Palette persistence on the Mac | none in v1 | `UserDefaults`-backed `Preferences` stub |

---

## 9. Verification

1. **Behaviour, headless:** `sultana-replay … --trace` against the
   tables in `ENGINE.md` §2. Cheap enough to run on every firmware change.
2. **Side by side with the robot:** both on one bridge, run
   `scripts/simulate-hooks.js` (see `BRIDGE.md`). Same verbs at the same
   times; blink and glance timing differ.
3. **Frame-exact, app vs CLI:** replay the same fixture in the app's
   log-clock mode and in the CLI with the same seed; diff the PNGs.
4. **Dock cost:** Instruments, Dock and app CPU at 30 / 15 / 10 fps, idle
   and during a Reading stream.

---

## 10. Appendix: the probe that justified §1

Done on 2026-10-24 on this Mac (Xcode's clang, `-std=gnu++17`), against
`main` at `0baabc3`, in a session scratchpad that will not survive. It takes
under an hour to recreate from §4.2 and is the seed of Phase 1.

- Compiled, unmodified: the 21 firmware files in §4.1. Zero errors on the
  first build.
- Stubs: the five headers in §4.2 (about 150 lines in total), plus a host
  `Display` (sprite + copy-out on `pushFrame`) and a host `Bridge` (inject
  + connection flag + request counter). The stub `fillSmoothCircle` was a
  plain `fillCircle`; text methods were no-ops.
- Harness: replicated `loop()`, injected `active_sessions` then
  `session.started`, `turn.started`, a `file.read` activity, a `shell.exec`
  activity held for 6.5 s, a `"Claude needs…"` notification and
  `turn.ended`, advancing the clock 5 ms per iteration.
- Trace (effective verb / expression / raw valence, activation / commanded
  arm):

```
t=  1005 ms  verb=sleeping             expr=verb_sleeping          v=+0.00 a=0.00 arm=-15
t=  1505 ms  verb=waking               expr=verb_waking            v=+0.06 a=0.06 arm=-27
t=  2500 ms  verb=none                 expr=neutral                v=+0.59 a=0.51 arm=+23
t=  3505 ms  verb=thinking             expr=verb_thinking          v=+0.59 a=0.50 arm=-14
t=  5005 ms  verb=reading              expr=verb_reading           v=+0.58 a=0.50 arm=-18
t=  6800 ms  verb=thinking             expr=verb_thinking          v=+0.57 a=0.50 arm=-20
t=  7305 ms  verb=executing            expr=verb_executing         v=+0.56 a=0.50 arm=-11
t= 12300 ms  verb=straining            expr=verb_straining         v=+0.53 a=0.50 arm=-8
t= 14800 ms  verb=thinking             expr=verb_thinking          v=+0.52 a=0.50 arm=+9
t= 15305 ms  verb=attracting_attention expr=verb_attracting_attention v=+0.51 a=0.50 arm=-22
t= 16300 ms  verb=thinking             expr=verb_thinking          v=+0.51 a=0.50 arm=-4
t= 17305 ms  verb=celebrating          expr=verb_celebrating       v=+0.55 a=0.55 arm=-13
t= 19800 ms  verb=none                 expr=neutral                v=+0.97 a=0.66 arm=-8
frames rendered: 628
```

  Every transition matches `ENGINE.md` §2: the 1.5 s Waking overlay with a
  500 ms blend-out, the 1 s linger back to Thinking, strain promotion at
  5 s, the 3 s Celebrating overlay, and the `+0.7 / +0.9` impulse visible in
  the final valence. 628 frames over 18.8 s is the 33 ms throttle.
- Frames: the firmware's own renderer produced the face, mood ring
  (thinking blue, celebrating yellow, happy green) and the two activity
  dots for the two tools in the turn.
