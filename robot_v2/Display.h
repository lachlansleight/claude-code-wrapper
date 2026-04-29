#pragma once

// Low-level display driver. Owns the TFT_eSPI instance and a full-screen
// sprite framebuffer; exposes both so higher-level modules (Face, future
// UI overlays) can draw into the sprite and push it via DMA.
//
// Responsibilities:
//   - init panel + backlight
//   - allocate the 240x240x16bpp sprite in internal SRAM
//   - boot splash + provisioning portal screen (one-shot renders)
//   - DMA push of the sprite
//
// Not responsible for: deciding *what* to draw. That's Face.cpp / Personality.
//
// setBrightness() is a no-op when TFT_BL isn't defined in User_Setup.h (our
// current breakout hardwires the backlight to VCC).

#include <Arduino.h>

class TFT_eSprite;   // forward decl — include <TFT_eSPI.h> in callers

namespace Display {

void begin();
void setBrightness(uint8_t pct);

// Sprite accessor. Face.cpp renders into this each frame.
TFT_eSprite& sprite();

// True if the sprite was successfully allocated. Callers should check
// before rendering — on OOM we bail out of the tick loop rather than draw.
bool ready();

// DMA-push the sprite to the panel. Call after rendering one frame.
void pushFrame();

// One-shot portal screen. Bypasses the sprite framebuffer path — renders
// directly to the panel — since the firmware blocks inside the portal loop.
void drawPortalScreen(const char* ssid, const char* ip);

// One-shot boot-time WiFi status overlays. Drawn before the Face takes
// over the framebuffer.
void drawConnecting(const char* ssid);
void drawFailedToConnect();

}  // namespace Display
