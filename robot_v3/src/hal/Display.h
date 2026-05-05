#pragma once

#include <Arduino.h>

class TFT_eSprite;

/**
 * @file Display.h
 * @brief 240×240 GC9A01 round TFT driver, sprite framebuffer and DMA push.
 *
 * Wraps the underlying `TFT_eSPI` library so that the rest of the firmware
 * never talks to the panel directly. The implementation maintains a single
 * 16 bpp `TFT_eSprite` framebuffer (240×240×2 bytes) which **must live in
 * internal SRAM**, since PSRAM is not DMA-safe for SPI master writes on
 * the ESP32-S3. All renderers draw into this sprite; pushFrame() then
 * blits it to the panel via DMA.
 *
 * Pin selection is baked in at compile time via `robot_v3/User_Setup.h`
 * (see TFT_eSPI docs); the runtime `config.h` cannot override it. Three
 * pre-canned full-screen UI states (boot/connecting/portal/failed) live
 * here so they can run before the face renderer is available.
 *
 * The TFT backlight, if wired to `TFT_BL`, is driven via LEDC PWM and is
 * controllable through setBrightness().
 */
namespace Display {

/**
 * Initialize the panel and DMA, allocate the sprite framebuffer, light
 * the backlight, draw a "robot_v3 / Booting" splash and push it. Logs
 * `LOG_ERR` and leaves ready() == false if the framebuffer allocation
 * fails. Must be called once from `setup()`.
 */
void begin();

/**
 * Set the backlight intensity as a percentage 0..100. Clamped to 100. No
 * effect if `TFT_BL` is not defined in the User_Setup.
 */
void setBrightness(uint8_t pct);

/**
 * Get a reference to the shared sprite framebuffer. Renderers draw into
 * this sprite each frame; the result is committed by pushFrame().
 */
TFT_eSprite& sprite();

/**
 * True once the framebuffer has been allocated successfully. If false,
 * pushFrame() and the sprite()-backed draw helpers below are no-ops.
 */
bool ready();

/**
 * DMA the current sprite contents to the panel. Brackets the transfer
 * with `startWrite/endWrite` and waits for the previous DMA to finish
 * first. Called once per frame from FrameController::tick.
 */
void pushFrame();

/**
 * Draw a full-screen "CONFIG MODE" notice with the AP SSID and IP. Used
 * by ProvisioningUI as the portal state handler. Safe to call before the
 * face renderer is ready.
 */
void drawPortalScreen(const char* ssid, const char* ip);

/// Full-screen "Connecting to <ssid>" splash. Drawn during boot WiFi attempts.
void drawConnecting(const char* ssid);

/// Full-screen "Failed to Connect" splash. Shown when all known networks fail.
void drawFailedToConnect();

}  // namespace Display
