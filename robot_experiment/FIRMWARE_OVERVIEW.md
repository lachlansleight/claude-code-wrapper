# robot_experiment firmware overview

ESP32 firmware that connects to a Claude Code Bridge over WebSocket,
reflects session state on a 128×32 SSD1306 OLED, waggles a hobby servo for
attention, and supports on-device WiFi / bridge provisioning via a captive
AP portal. The whole thing is a single Arduino sketch split into focused
modules.

## Hardware

| Component | Pin / bus | Notes |
|-----------|-----------|-------|
| ESP32 dev board | — | any board with BOOT on GPIO0 and I²C on 21/22 |
| SSD1306 OLED 128×32 | I²C SDA 21, SCL 22, addr `0x3C` | |
| SG92R servo | GPIO 5 | powered off USB 5V is usually fine for idle twitches |
| BOOT button | GPIO 0 | active-low; hold 800 ms at boot to enter the config portal |

Pin numbers and I²C address live in `config.h` — copy
`config.example.h` → `config.h` first (gitignored).

## Runtime flow

```
           ┌─────────┐
           │  boot   │
           └────┬────┘
                │
       Provisioning::load(cfg)   ← pulls from NVS, falls back to config.h
                │
    hold BOOT?  │  no NVS?
          └─────┴─────→  Provisioning::runPortal(cfg)   (blocks until save)
                │
       WifiMgr::connect()
                │
       Bridge::begin(host, port, token)
                │
    ┌───────────┴────────────────────────────────────────────┐
    │                     loop()                             │
    │   WifiMgr::tick           auto-reconnect WiFi          │
    │   Bridge::tick            ws.loop() + session polling  │
    │   AttractScheduler::tick  idle-state waggles           │
    │   Motion::tick            servo keyframes              │
    │   Display::tick           redraws OLED                 │
    └────────────────────────────────────────────────────────┘
```

Every I/O layer updates (or reads) a central state struct in `ClaudeEvents`.
Nothing is pushed imperatively — the display, motion scheduler, and attract
scheduler all derive behavior from polled state. That keeps the layers
decoupled and makes it trivial to add a new consumer (e.g. an RGB LED
driver) without touching existing code.

## Module map

| Module | Files | Responsibility |
|--------|-------|----------------|
| Entrypoint | `robot_experiment.ino` | setup/loop, wires every module together |
| Config | `config.h`, `config.example.h` | compile-time defaults for WiFi + bridge |
| Provisioning | `Provisioning.{h,cpp}` | NVS load/save + captive-AP HTTP portal |
| Network | `WiFiManager.{h,cpp}` | WiFi connect + auto-reconnect |
| Bridge transport | `BridgeClient.{h,cpp}` | WS client, reconnect, JSON send helpers, session polling |
| Event core | `ClaudeEvents.{h,cpp}` | envelope dispatch, polled state, callback registry, session latching |
| Tool display | `ToolFormat.{h,cpp}` | tool name → short label + `tool_input` → 1-line detail |
| String utility | `AsciiCopy.{h,cpp}` | UTF-8 → ASCII transliteration (shared) |
| Display | `Display.{h,cpp}` | 128×32 OLED renderer, fully state-driven |
| Motion | `Motion.{h,cpp}` | servo attach + non-blocking keyframe playback |
| Attention | `AttractScheduler.{h,cpp}` | fires waggles on idle entry per a ms-offset schedule |
| Logging | `DebugLog.h` | `LOG_INFO` / `LOG_WARN` / `LOG_ERR` / `LOG_WS` / `LOG_EVT` |

### Docs

- `FIRMWARE_OVERVIEW.md` — this file
- `TOOL_DISPLAY.md` — per-tool label/detail table + how to customize

## Module details

### `Provisioning`
- `load(Config&)` reads five keys from NVS namespace `bridge_cfg`
  (`ssid`, `pass`, `host`, `port`, `token`), falling back to compile-time
  defaults when absent. Returns `true` if every key was present.
- `shouldEnterPortal()` checks whether BOOT (GPIO 0) is held low for
  800 ms after power-on. Setup also auto-enters the portal if NVS is
  empty.
- `runPortal(cfg)` puts the ESP32 in `WIFI_AP` mode with SSID
  `robot-XXXX` (MAC-derived), serves a single-page HTML form on `/`,
  redirects everything else to `/` as a crude captive portal, writes
  `/save` payloads back to NVS, and reboots. `/forget` clears NVS.
  The OLED shows AP SSID + IP the whole time via
  `Display::drawPortalScreen()`.

### `WiFiManager`
- `connect(ssid, pass)` blocks until WiFi associates — intentionally,
  because there's nothing useful to do before it does.
- `tick(ssid, pass)` checks every 2 s and calls `WiFi.begin` again if
  the link dropped.

### `BridgeClient`
- Wraps Markus Sattler's `WebSocketsClient`. URL is
  `ws://<host>:<port>/ws?token=<token>`.
- Heartbeat 15 s / 3 s timeout / 2 strike-outs before reconnect.
  Auto-reconnect every 2 s.
- `tick()` also polls the bridge for the active session list every 5 s
  while `latched_session` is empty. Polling stops the moment we latch.
- `sendPermissionVerdict`, `sendChatMessage`, `sendRaw` are the send
  helpers used by the rest of the firmware.
- All decoded frames are forwarded to `ClaudeEvents::dispatch(doc)`.

### `ClaudeEvents`
Central event + state module.

- **State** (`ClaudeState`, queried via `state()`): `wifi_connected`,
  `ws_connected`, `working`, `session_id`, `latched_session`,
  `last_hook`, `last_tool`, `current_tool` + `tool_detail` +
  `current_tool_end_ms`, `last_summary`, `pending_permission` /
  `pending_tool` / `pending_detail`, `last_event_ms`.
- **Callbacks** (`on*()`): one-slot registry for hello, inbound,
  outbound, permission request/resolved, hook, session, raw,
  connection-change.
- **Dispatch**: `dispatch()` is a flat table mapping envelope `type`
  → handler. Each handler updates state and optionally fires a
  user-registered callback. Unknown types are logged.
- **Session latching**: when `latched_session` is empty, any incoming
  session id claims the latch. While latched, hook events from other
  sessions are dropped. `SessionEnd` for the latched session, or its
  absence from a fresh `active_sessions` list, releases the latch.
  Disconnect also releases it so a reconnect re-picks.
- **Display derivations** baked into state updates: `UserPromptSubmit`
  wipes `last_summary` (so stale text doesn't linger over a new turn);
  `PreToolUse` also wipes it (so a previous Notification can't
  resurface after the tool lingers out); `inbound_message` /
  `outbound_reply` retire any active tool label immediately.

### `ToolFormat`
Two functions, both consulted by `ClaudeEvents` (for state) and
`Display` (for rendering):

- `label(tool)` → short upper-case string (`"EDIT"`, `"BASH"`, ...).
  MCP tools collapse to `"MCP"`; unknown tools return the raw name.
- `detail(tool, input, out, cap)` → best-effort one-line description
  sourced from `tool_input` (basename for file ops, `command` for
  Bash, `pattern` for Grep/Glob, etc).

Customization lives entirely in this file — see `TOOL_DISPLAY.md`.

### `AsciiCopy`
- `copy(dst, cap, src)` — copies a string while folding UTF-8 down to
  ASCII, substituting smart quotes / em-dashes / arrows / etc., and
  replacing unknown codepoints with `?`. Newlines become spaces;
  control characters are dropped.
- `basename(path, out, cap)` — last segment of a path, handles both
  `/` and `\`, passes through `copy()` so it's ASCII-safe.

Any string that originates from Claude's tool-use blobs should go
through this module before landing on the OLED.

### `Display`
128×32 OLED, fully state-driven.

- Header row (y 0–7): WiFi bars, WS check/cross, right-edge
  spinner-when-working / blink-dot-when-idle.
- Separator line at y=9.
- Body (y 16–31, two 21-char rows). Priority:
  1. Pending permission → `ALLOW? TOOL detail` (hard-wrapped)
  2. Active-or-lingering tool slot → `TOOL detail` (hard-wrapped,
     linger 1 s after `PostToolUse`)
  3. `last_summary` — word-wrapped
  4. `"waiting for bridge"` when WS is down
  5. `"Thinking..."` when `working` with nothing concrete to show
  6. blank
- `drawPortalScreen(ssid, ip)` is a one-shot overlay used while the
  provisioning portal is running (main loop is blocked then anyway).
- Redraw pacing: ≥40 ms between draws; spinner frames advance every
  120 ms; idle blink toggles every 500 ms; an `invalidate()` flag
  forces a repaint.

### `Motion`
- Keyframe-based servo playback. Each `Pattern` is an array of
  `{angle, dwellMs}` keyframes; `tick()` advances frames as their
  dwells expire.
- `playWaggle()` starts the built-in attention pattern if nothing is
  playing, otherwise queues it (depth 1 — a second call while already
  queued replaces the first).
- Adding a new motion pattern: define another `Keyframe[]` + `Pattern`
  constant and a thin `playX()` wrapper. Everything else reuses the
  scheduler.

### `AttractScheduler`
- State machine with four phases: `Boot`, `Working`, `IdleGrace`,
  `Idle`.
- `Boot → Working` on first `working=true` (avoids firing on the
  power-on idle window).
- `Working → IdleGrace` when `working=false`; 3 s debounce window.
  Any `working=true` during grace bounces back to `Working` so
  Stop flaps mid-response don't count.
- `IdleGrace → Idle` after 3 s — schedule starts. Current schedule
  lives in `AttractScheduler.cpp::kSchedule` (ms offsets from idle
  entry, strictly ascending). Each offset that elapses triggers
  `Motion::playWaggle()`. `Idle → Working` on next activity resets
  the schedule.

### `DebugLog`
`LOG_INFO`, `LOG_WARN`, `LOG_ERR`, `LOG_WS`, `LOG_EVT` — all printf
wrappers around `Serial.printf` with a level prefix. Searchable,
greppable, no structured logging pretense.

## Bridge protocol used

Incoming envelopes (as received from the bridge):

| `type` | Payload fields consumed | Effect |
|--------|-------------------------|--------|
| `hello` | `client_id`, `server_version` | log |
| `inbound_message` | `content`, `chat_id` | update `last_summary` |
| `outbound_reply` | `content`, `chat_id` | update `last_summary` |
| `permission_request` | `request_id`, `tool_name`, `input` | populate `pending_*` |
| `permission_resolved` | `request_id`, `behavior`, `applied` | clear `pending_*` if matched |
| `hook_event` | `hook_type`, `payload.session_id`, `payload.tool_name`, `payload.tool_input`, `payload.assistant_text[]` | session latch + state update |
| `session_event` | `event`, `session_id` | log |
| `active_sessions` | `session_ids[]` | latch onto `[0]` if unlatched; drop latch if ours disappeared |
| `pong` | — | keep-alive |
| `error` | `message` | warn |

Outgoing frames:

- `{"type":"permission_verdict","request_id":"abcde","behavior":"allow|deny"}`
- `{"type":"send_message","content":"...","chat_id":"..."}`
- `{"type":"request_sessions"}` — sent every 5 s while unlatched

## Conventions

- **One namespace per module**, no classes unless there's genuine state
  to encapsulate (only `Motion` really needs one, and even it doesn't
  use a class).
- **Polled state is primary**; callbacks are offered but optional.
- **All display strings go through `AsciiCopy::copy`.** The OLED font
  has no Unicode support and will render `?` runs otherwise.
- **Non-blocking everywhere except `setup()`** and the provisioning
  portal (which intentionally blocks).
- **`config.h` is gitignored**; secrets stay local.

## Libraries required

Install via Arduino Library Manager:

- **WebSockets** by Markus Sattler
- **ArduinoJson** by Benoit Blanchon (v7+)
- **Adafruit GFX Library**
- **Adafruit SSD1306**
- **ESP32Servo** by Kevin Harrington

Board support: ESP32 (any variant with I²C + LEDC PWM, which is all of them).
