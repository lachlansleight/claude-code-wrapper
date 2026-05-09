# Configuration, HAL and utilities

This is the smaller-but-load-bearing layer: display init, settings
storage, WiFi/bridge provisioning, and a couple of utilities that
everything else depends on.

## Display

`src/hal/Display.{h,cpp}`. 240×240 round GC9A01 panel via TFT_eSPI.

```cpp
namespace Display {
    void begin();
    void setBrightness(uint8_t pct);    // 0..100
    TFT_eSprite& sprite();
    bool ready();
    void pushFrame();                   // DMA blit, brackets startWrite/endWrite

    void drawConnecting(const char* ssid);
    void drawPortalScreen(const char* ssid, const char* ip);
    void drawFailedToConnect();
}
```

### Critical constraints

- **Pins are baked at compile time** by TFT_eSPI from
  `robot_v3/User_Setup.h`. `config.h` does not control display wiring.
  If you change the panel wiring, edit `User_Setup.h` and recompile.
- **The framebuffer must live in internal SRAM**, not PSRAM. The
  ESP32-S3's SPI master DMA is not safe against PSRAM. `Display::begin()`
  allocates a 240×240×16 bpp = 115 200-byte sprite in SRAM. If
  allocation fails, `ready()` returns false and the firmware will not
  render usefully.
- **Backlight** is LEDC PWM on `TFT_BL` if defined.

`pushFrame()` is the only path to the panel; renderers all draw into
`Display::sprite()` and `FrameController` calls `pushFrame()` at the
end of every frame it emits.

## Settings

`src/hal/Settings.{h,cpp}`. NVS-backed runtime config with a schema
version for migration.

### Storage

- NVS namespace: `settings_v3`.
- A version key gates the entire namespace; on schema mismatch the
  whole namespace is wiped and defaults loaded.

### Stored values

- **Palette** — `Settings::Rgb888 colorRgb(NamedColor)` /
  `setColorRgb(...)`. The `NamedColor` enum has slots for:
  - Structural: `Background`, `Foreground`.
  - Verbs: `Thinking`, `Reading`, `Writing`, `Executing`, `Straining`,
    `Sleeping`.
  - Emotions: `Happy`, `Joyful`, `Excited`, `Sad`,
    `EmotionSleepy/Distressed/Blissed/Depressed/Shocked/Disappointed/Frustrated`.
  - Special: `Attention`.

  `color565()` and `color565Scaled(c, scale255)` are convenience
  wrappers for the renderers (since the sprite is 16-bpp).
- **Face mode** — `bool faceModeEnabled()` toggles between face
  rendering and text-mode.
- **Motors disabled** — `bool motorsDisabled()` globally disables
  servo writes.

### Versioning

`uint32_t settingsVersion()` returns a monotonic counter that bumps
on every successful mutation. `FrameController` reads this each frame
and invalidates its emotion-tween smoothing if the version changed,
so palette/preset edits take effect in one frame instead of bleeding
through the smoothing window.

## Provisioning

`src/hal/Provisioning.{h,cpp}` and `ProvisioningUI.{h,cpp}`. WiFi +
bridge credential store with multi-network memory and a captive AP
portal.

### Storage

- NVS namespace: `bridge_cfg`.
- **Multi-network list** — `nets` key, tab/newline-delimited, FIFO
  most-recent-first.
- **Legacy single-set** — `ssid`, `password`, `bridge_host`,
  `bridge_port`, `bridge_token`. Used as the fallback when the
  multi-network list is empty.

### Boot decision

`robot_v3.ino` runs the portal if any of:

- No network is configured.
- The boot button (GPIO 0) is held ≥ 800 ms during boot.
- A one-shot portal request is queued via
  `requestOneTimePortal()` (consumed at startup).

Otherwise it tries each remembered network in order via
`WifiMgr::tryConnect(ssid, password, 15000)` until one succeeds; if
none do, it shows the failed-to-connect splash and re-enters the
portal.

### Portal

- Soft-AP SSID `robot-XXXX` (last bytes of MAC), IP `192.168.4.1`.
- HTTP form for SSID, password, bridge host/port/token.
- On submit: persist + reboot.
- `ProvisioningUI::begin()` registers a state callback so
  `Display::drawPortalScreen()` can show the SSID and IP.

### Public API

```cpp
namespace Provisioning {
    bool   load(Config&);
    void   save(const Config&);
    size_t loadNetworks(NetEntry*, size_t maxCount);
    void   rememberNetwork(const NetEntry&);
    void   clear();

    void   requestOneTimePortal();
    bool   consumeOneTimePortalRequest();

    bool   shouldEnterPortal();        // samples boot button for 800 ms
    void   onPortalState(PortalStateHandler);
    bool   runPortal(Config&);         // blocking
}
```

## WiFiManager

`src/hal/WiFiManager.{h,cpp}`. Thin wrapper over the ESP-IDF WiFi
APIs.

```cpp
namespace WifiMgr {
    bool tryConnect(const char* ssid, const char* password, uint32_t timeoutMs);
    void tick(const char* ssid, const char* password);   // auto-reconnect
}
```

Auto-reconnect is best-effort: if the link drops while the bridge is
in the middle of a WebSocket frame, BridgeClient will see the
disconnect on its next tick and queue a reconnect.

## Utilities

### DebugLog

`src/core/DebugLog.h`. Logging macros over `Serial.printf`:

| Macro            | Tag       | Use for                                |
|------------------|-----------|----------------------------------------|
| `LOG_INFO(…)`    | `[INFO ]` | General lifecycle                      |
| `LOG_WARN(…)`    | `[WARN ]` | Recoverable problems                   |
| `LOG_ERR(…)`     | `[ERROR]` | Errors, allocation failures            |
| `LOG_WS(…)`      | `[ws   ]` | WebSocket transport                    |
| `LOG_EVT(…)`     | `[evt  ]` | Parsed agent events                    |

All append a trailing newline.

### AsciiCopy

`src/core/AsciiCopy.{h,cpp}`. UTF-8 → display-safe ASCII sanitizer.

The TFT bitmap fonts only carry 7-bit ASCII glyphs, but bridge
payloads are arbitrary UTF-8. `AsciiCopy` decodes UTF-8 codepoints
and:

- Substitutes common non-ASCII (curly quotes → `"` / `'`, em dash →
  `--`, ellipsis → `...`, arrows → `->`, degree → `deg`, …).
- Drops unrecognised non-ASCII bytes and control bytes (newlines/CRs
  preserved only if you call `copyPreserveNewlines()`).
- Truncates cleanly to the destination capacity (no half-written
  multi-byte sequences).

```cpp
namespace AsciiCopy {
    void copy(char* dst, size_t cap, const char* src);
    void copyPreserveNewlines(char* dst, size_t cap, const char* src);
    void basename(const char* path, char* out, size_t cap);
}
```

**Every string that goes into `SceneContext` for display passes
through this.** If you add a new string field and skip the sanitizer,
you'll eventually see garbage glyphs from a non-ASCII path or
emoji-laden assistant text.

## config.h vs config.example.h

`robot_v3/src/config.h` is the per-board build config (pin
assignments, calibration constants). `config.example.h` is checked in;
`config.h` is the local copy users edit. Note that **TFT pin
assignments do not live here** — TFT_eSPI bakes them from
`robot_v3/User_Setup.h`. Servo pin and other non-display pins do live
in `config.h`.
