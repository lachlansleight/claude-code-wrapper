# GC9A01 Display — Research Notes

Background research for swapping the SSD1306 128x32 OLED in `robot_experiment/`
out for a 240x240 round colour TFT driven by the GC9A01 chip in `robot_v2/`.

## The chip

- **GC9A01 / GC9A01A**: single-chip TFT controller, integrated GRAM, made by
  Galaxycore. Almost always paired with a 1.28" round IPS panel at **240×240
  px, 16-bit RGB565** (262K colour mode also supported but RGB565 is what
  every Arduino library uses).
- Interface to MCU: **4-wire SPI** (CS, SCK, MOSI/SDA, DC) plus optional
  RST and a backlight enable (BLK/BL). MISO is not used — display is
  write-only from the MCU's perspective.
- SPI clock: rated to ~80 MHz on ESP32. 40 MHz is a safe default; full-frame
  refresh at 40 MHz is roughly 30 fps without DMA, ~60 fps with DMA.
- No touch on the bare panel. (Some breakouts add a CST816S capacitive
  touch IC on a separate I²C bus — not relevant unless we pick one of those.)
- Power: 3.3 V logic and rail. Most breakouts have an onboard LDO so 5 V VIN
  works too, but **logic must be 3.3 V** — the ESP32 is fine, an Arduino Uno
  needs level shifting.

### Quirks worth knowing before writing init code

- **RAM is undefined at power-on.** If you send the display-on command (0x29)
  inside your init sequence, the panel will show garbage until the first
  full draw. Standard workaround: init → clear framebuffer → only then
  send 0x29. Most libraries do this for you, but it bit several people on
  the TFT_eSPI issue tracker.
- The vendor's reference init sequence uses **a lot of undocumented commands**
  (~70% are not in the public datasheet — they're factory tuning registers).
  Don't try to write an init from scratch; copy from a working library.
- Datasheet PDF: <https://www.waveshare.com/w/upload/5/5e/GC9A01A.pdf>

### Backlight (BLK / BL)

- Active-high. Floating or HIGH = backlight on; LOW = off.
- It's just a gate to the LED — drive it from any GPIO. For dimming, attach
  an ESP32 LEDC PWM channel (5 kHz is plenty, the LED won't flicker).
- If we don't care about brightness control we can tie BLK to 3.3 V directly
  and save a GPIO.

## Library options for ESP32 / Arduino

| Library | Pros | Cons |
|---|---|---|
| **TFT_eSPI** (Bodmer) | Fastest, DMA, sprites, anti-aliased fonts/arcs, smooth gauges, huge ecosystem | Pins are baked in at compile time via `User_Setup.h` — awkward when the library is shared between projects. Not great for dynamic configuration. |
| **Arduino_GFX** (moononournation) | Pins passed at runtime via constructor → much friendlier when `config.h` owns the wiring. Supports DMA on ESP32. Adafruit_GFX-compatible API. | Slightly slower than TFT_eSPI in micro-benchmarks; smaller font selection out of the box. |
| **Adafruit GC9A01A** | Familiar Adafruit_GFX API, shares fonts with the SSD1306 library we're already using | Slowest of the three; no DMA. |
| **LVGL** | Real widget toolkit, animations, anti-aliasing | Heavy — ~30 KB RAM for the draw buffer alone, large flash footprint, steep learning curve. Overkill for our use case. |
| **esp_lcd_gc9a01** (Espressif IDF component) | Native to ESP-IDF | Not Arduino-friendly; we're on the Arduino core. |

### Decision: TFT_eSPI

We're going with **TFT_eSPI** on an **ESP32-S3** with DMA, targeting 60 fps
via a full-screen sprite framebuffer. The eventual goal is procedural
animation of a fullscreen face, so the sprite engine and anti-aliased
primitives (`drawSmoothArc`, `drawSmoothCircle`, anti-aliased fonts) earn
their keep.

Tradeoff we're accepting: TFT_eSPI bakes pins and driver selection into
`User_Setup.h` at compile time. We'll keep a project-local
`User_Setup.h` (or use `User_Setup_Select.h` to point at one) and keep
the display pin numbers in `config.h` for documentation, even though
TFT_eSPI itself won't read them at runtime.

### ESP32-S3 + DMA gotchas

- DMA on ESP32-S3 needs **ESP-IDF ≥ 2.0.14** (i.e. recent Arduino-ESP32
  3.x board package). Older cores silently drop frames after the first.
- `USE_HSPI_PORT` must be defined in `User_Setup.h` for DMA to work
  reliably on S3 with GC9A01. Without it, people report only the first
  frame rendering.
- DMA stability is sensitive to the **TFT_DC pin choice** — pick a normal
  GPIO with no strapping role; avoid GPIO 0/45/46.
- The sprite buffer used as a DMA source **must live in DMA-capable RAM**.
  Allocate the sprite with `setColorDepth(16)` then `createSprite(240, 240)`;
  TFT_eSPI's allocator handles this on S3 internal SRAM. Don't put it in
  PSRAM — PSRAM is not DMA-safe for SPI master writes on S3.

## Wiring (suggested)

ESP32-S3 doesn't have the legacy VSPI/HSPI peripheral split — the SPI
peripherals are pin-flexible via the GPIO matrix. TFT_eSPI on S3 still
uses the symbol `USE_HSPI_PORT` to select SPI2 (which is what we want for
DMA), but we can map any free GPIOs to it.

| Display pin | ESP32-S3 GPIO | Notes |
|---|---|---|
| VCC | 3V3 | |
| GND | GND | |
| SCL / SCK | 12 | SPI2 clock |
| SDA / MOSI | 11 | SPI2 data out |
| DC | 13 | Data/Command — avoid strapping pins (0, 45, 46) |
| CS | 10 | Chip select |
| RST | 14 | Hard reset |
| BLK / BL | 9 | PWM via LEDC for `Display::setBrightness()` |

Pins are suggestions — anything already claimed in `config.h` (servo,
WiFi provisioning button, etc.) gets first pick. Mirror these into
`config.h` as `#define`s and into `User_Setup.h` so the two stay in
sync; if they drift, `User_Setup.h` wins because TFT_eSPI doesn't read
the runtime values.

## Memory & performance budget

- Full framebuffer at 16 bpp = 240 × 240 × 2 = **115 200 bytes**. ESP32-S3
  has 512 KB internal SRAM, so this fits comfortably without touching
  PSRAM (and as noted above, we *can't* put it in PSRAM if we want DMA).
- Hitting **60 fps requires SPI clock ≥ 40 MHz with DMA**. The math:
  pushing 115 KB at 40 MHz = ~23 ms per frame = ~43 fps theoretical max
  on its own; with DMA the CPU is free during the push so we can overlap
  the next frame's render with the current frame's transmit. Bump SPI to
  **80 MHz** (the GC9A01 tolerates it on short, well-grounded wiring) for
  comfortable 60 fps headroom.
- Pattern: one persistent `TFT_eSprite` of 240×240×16bpp as the
  framebuffer. Render the next frame into it, then push with
  `tft.pushImageDMA(0, 0, 240, 240, sprite.getPointer())`. Call
  `tft.dmaWait()` (or check `tft.dmaBusy()`) before mutating the sprite
  for the next frame.
- Cooperative loop discipline: rendering the face must not block the
  WebSocket / servo / event ticks. Either time-slice the render across
  multiple `tick()` calls or commit to a fixed frame budget (e.g.
  ≤ 8 ms/frame of CPU work, leaving the rest for everything else).

## Migration impact on existing firmware

The `Display` namespace in `robot_v2/Display.h` has a tiny public surface
(`begin`, `tick`, `invalidate`, `drawPortalScreen`). The state-driven
contract — Display reads `ClaudeEvents::state()` and decides what to draw —
is the right shape and shouldn't change. What changes:

- **Layout.** 128×32 → 240×240 round. The current header/separator/body
  rows don't translate; we get a circular canvas to design for.
  Likely shape: header arc along the top, body text in the middle band,
  status icons around the rim or in a footer arc. Worth a fresh layout
  pass rather than trying to stretch the OLED layout.
- **Colour.** RGB565 instead of monochrome. State indicators (working,
  permission pending) become much more legible with colour coding.
- **Fonts.** Adafruit_GFX-style bitmap fonts work in Arduino_GFX. Larger
  panel = room for bigger glyphs; the existing AsciiCopy table can keep
  driving body text.
- **Init order.** Add the "init → clear → display-on" dance so we don't
  flash garbage on boot.
- **Backlight.** Add `Display::setBrightness(uint8_t pct)` backed by an
  ESP32 LEDC channel on the BLK pin. Default to 100% on boot; the API
  exists for future use (idle dim, theatrics) but nothing calls it yet.

## Decisions (locked in)

1. **Hardware**: generic Amazon GC9A01 breakout, no specific brand. Pinout
   to be confirmed against the actual board on first wire-up; the labels
   on these generic boards are usually the standard set
   (VCC/GND/SCL/SDA/RES/DC/CS/BLK).
2. **Library**: TFT_eSPI with project-local `User_Setup.h`. `GC9A01_DRIVER`
   + `USE_HSPI_PORT` + `SPI_FREQUENCY 80000000`.
3. **MCU**: ESP32-S3, DMA enabled, targeting 60 fps.
4. **Framebuffer**: full 240×240×16bpp `TFT_eSprite` in internal SRAM
   (~115 KB). Required for the eventual procedural face animation; using
   it from day one means the rendering pipeline doesn't need to change
   when we add the face.
5. **Backlight**: PWM-capable wiring + `setBrightness()` API, but ship at
   100% — no auto-dim behaviour yet.

## Sources

- [Arduino_GFX library (moononournation)](https://github.com/moononournation/Arduino_GFX)
- [TFT_eSPI library (Bodmer)](https://github.com/Bodmer/TFT_eSPI)
- [Adafruit 1.28" 240x240 GC9A01A breakout](https://www.adafruit.com/product/6178)
- [DroneBot Workshop GC9A01 guide](https://dronebotworkshop.com/gc9a01/)
- [GC9A01A datasheet PDF (Waveshare mirror)](https://www.waveshare.com/w/upload/5/5e/GC9A01A.pdf)
- [TFT_eSPI #2559 — init/garbage discussion](https://github.com/Bodmer/TFT_eSPI/discussions/2559)
- [StudioPieters GC9A01 guide](https://www.studiopieters.nl/gc9a01/)
- [FritzenLab — seamless graphics on GC9A01A](https://fritzenlab.net/2024/07/02/gc9a01a-round-lcd-240x240-display/)
- [Espressif esp_lcd_gc9a01 component](https://components.espressif.com/components/espressif/esp_lcd_gc9a01)
