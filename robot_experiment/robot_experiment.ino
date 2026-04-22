// robot_experiment — ESP32 firmware that connects to the Claude Code bridge,
// surfaces state on a 128x32 SSD1306 OLED, and exposes an event-like API for
// the rest of the firmware to react to Claude Code activity.
//
// Module map:
//   config.h          — wifi + bridge credentials (copy from config.example.h)
//   WiFiManager       — connect & auto-reconnect
//   BridgeClient      — WebSocket transport, JSON decode, send helpers
//   ClaudeEvents      — event structs, polled state, callback registry
//   Display           — OLED renderer (fully state-driven; no imperative API)
//   Motion            — servo abstraction + non-blocking keyframe patterns
//   AttractScheduler  — triggers attention waggles when Claude is idle
//   DebugLog          — LOG_* macros over Serial
//
// Required Arduino libraries (install via Library Manager):
//   WebSockets       by Markus Sattler
//   ArduinoJson      by Benoit Blanchon (v7+)
//   Adafruit GFX Library
//   Adafruit SSD1306
//   ESP32Servo       by Kevin Harrington

#include "AttractScheduler.h"
#include "BridgeClient.h"
#include "ClaudeEvents.h"
#include "DebugLog.h"
#include "Display.h"
#include "Motion.h"
#include "WiFiManager.h"
#include "config.h"

// ---- Example event handlers ------------------------------------------------
//
// The OLED renders itself straight from ClaudeEvents::state(), so handlers
// are only needed for side effects outside the display. These stubs show
// where to drop robot behavior (LEDs, servos, logging, etc.) — they're not
// required for the display to update.

static void onPermissionRequest(const ClaudeEvents::PermissionRequestEvent& e) {
  LOG_EVT("perm request id=%s tool=%s", e.request_id, e.tool_name);
  // Bridge::sendPermissionVerdict(e.request_id, "allow");  // auto-approve demo
}

static void onHook(const ClaudeEvents::HookEvent& e) {
  LOG_EVT("hook %s tool=%s", e.hook_type, e.tool_name);
}

// ---- setup / loop ----------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  LOG_INFO("robot_experiment boot");

  Display::begin();
  Motion::begin();
  AttractScheduler::begin();

  WifiMgr::connect(WIFI_SSID, WIFI_PASSWORD);
  ClaudeEvents::setWifiConnected(true);
  Display::invalidate();

  ClaudeEvents::onHook(onHook);
  ClaudeEvents::onPermissionRequest(onPermissionRequest);

  Bridge::begin(BRIDGE_HOST, BRIDGE_PORT, BRIDGE_TOKEN);
}

void loop() {
  WifiMgr::tick(WIFI_SSID, WIFI_PASSWORD);
  ClaudeEvents::setWifiConnected(WifiMgr::isConnected());

  Bridge::tick();

  // Polled usage: the robot body can inspect ClaudeEvents::state() each
  // pass instead of relying on callbacks.
  //
  //   const auto& st = ClaudeEvents::state();
  //   if (st.working) {  /* blink an LED, hold a pose, etc. */ }

  AttractScheduler::tick();
  Motion::tick();
  Display::tick();
}
