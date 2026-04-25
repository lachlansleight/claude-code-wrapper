// robot_v2 — ESP32-S3 firmware that connects to the Claude Code bridge,
// drives a procedural face on a 240x240 round GC9A01 TFT, and surfaces
// Claude Code activity via personality state + motion behaviours.
//
// Module map (see FIRMWARE_OVERVIEW.md for the full tour):
//   config.h          — wifi + bridge credentials (copy from config.example.h)
//   Provisioning      — NVS-backed runtime config + AP-mode config portal
//   WiFiManager       — connect & auto-reconnect
//   BridgeClient      — WebSocket transport, JSON decode, send helpers
//   ClaudeEvents      — event structs, polled state, callback registry,
//                       session latching
//   ToolFormat        — tool → short label + one-line detail (legacy, unused
//                       by Face; kept for future overlays)
//   AsciiCopy         — UTF-8 → ASCII string helpers (shared)
//   Display           — TFT driver: sprite framebuffer + DMA push
//   Personality       — 8-state machine driven by bridge hooks
//   Face              — renders personality state into the Display sprite
//   Motion            — servo abstraction + non-blocking keyframe patterns
//   MotionBehaviors   — state-driven arm gestures; reads Personality
//   AttractScheduler  — (legacy) idle attention waggles, replaced by
//                       MotionBehaviors. File retained, no longer ticked.
//   AmbientMotion     — (legacy) tool-edge jogs + thinking osc, replaced
//                       by MotionBehaviors. File retained, no longer ticked.
//   DebugLog          — LOG_* macros over Serial
//
// Required Arduino libraries (install via Library Manager):
//   WebSockets       by Markus Sattler
//   ArduinoJson      by Benoit Blanchon (v7+)
//   TFT_eSPI         by Bodmer (configure via robot_v2/User_Setup.h —
//                    see that file's header for install steps)
//   ESP32Servo       by Kevin Harrington (≥ 3.0 for Arduino-ESP32 core 3.x)
//
// Board: ESP32-S3 with Arduino-ESP32 core 3.x (DMA on S3 needs IDF ≥ 2.0.14).

#include "BridgeClient.h"
#include "ClaudeEvents.h"
#include "DebugLog.h"
#include "Display.h"
#include "Face.h"
#include "Motion.h"
#include "MotionBehaviors.h"
#include "Personality.h"
#include "Provisioning.h"
#include "WiFiManager.h"
#include "config.h"

static Provisioning::Config cfg;

// Personality::begin() registers the ClaudeEvents::onHook handler — only
// one hook handler is allowed at a time, so any other per-hook side effects
// should live inside Personality's handler (or we add a lightweight
// multi-dispatch later). Non-hook events can still register separately.

static void onPermissionRequest(const ClaudeEvents::PermissionRequestEvent& e) {
  LOG_EVT("perm request id=%s tool=%s", e.request_id, e.tool_name);
  // Bridge::sendPermissionVerdict(e.request_id, "allow");  // auto-approve demo
}

// ---- setup / loop ----------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  LOG_INFO("robot_v2 boot");

  Display::begin();
  Face::begin();
  Motion::begin();
  MotionBehaviors::begin();

  const bool haveNvs = Provisioning::load(cfg);
  const bool buttonHeld = Provisioning::shouldEnterPortal();
  if (buttonHeld || !haveNvs) {
    LOG_INFO("provisioning: entering portal (button=%d nvs=%d)",
             buttonHeld, haveNvs);
    Provisioning::runPortal(cfg);  // blocks; reboots on save/forget
  }

  WifiMgr::connect(cfg.wifi_ssid.c_str(), cfg.wifi_password.c_str());
  ClaudeEvents::setWifiConnected(true);
  Face::invalidate();

  Personality::begin();
  ClaudeEvents::onPermissionRequest(onPermissionRequest);

  Bridge::begin(cfg.bridge_host.c_str(), cfg.bridge_port,
                cfg.bridge_token.c_str());
}

void loop() {
  WifiMgr::tick(cfg.wifi_ssid.c_str(), cfg.wifi_password.c_str());
  ClaudeEvents::setWifiConnected(WifiMgr::isConnected());

  Bridge::tick();

  Personality::tick();
  MotionBehaviors::tick();
  Motion::tick();
  Face::tick();
}
