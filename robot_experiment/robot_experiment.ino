// robot_experiment — ESP32 firmware that connects to the Claude Code bridge,
// surfaces state on a 128x32 SSD1306 OLED, and exposes an event-like API for
// the rest of the firmware to react to Claude Code activity.
//
// Module map:
//   config.h        — wifi + bridge credentials (copy from config.example.h)
//   WiFiManager     — connect & auto-reconnect
//   BridgeClient    — WebSocket transport, JSON decode, send helpers
//   ClaudeEvents    — event structs, polled state, callback registry
//   Display         — OLED renderer (status bar + scrolling log)
//   DebugLog        — LOG_* macros over Serial
//
// Required Arduino libraries (install via Library Manager):
//   WebSockets       by Markus Sattler
//   ArduinoJson      by Benoit Blanchon (v7+)
//   Adafruit GFX Library
//   Adafruit SSD1306
//
// Board: any Arduino-ESP32 target. Default I2C pins (SDA=21, SCL=22) are used
// for the OLED.

#include "BridgeClient.h"
#include "ClaudeEvents.h"
#include "DebugLog.h"
#include "Display.h"
#include "WiFiManager.h"
#include "config.h"

// ---- Example event handlers ------------------------------------------------
//
// These exist to prove the wiring works and to show the two usage patterns:
//   * Hook-driven side effects (display log + whatever else the robot does)
//   * Polled state via ClaudeEvents::state() (see loop())

static void onHello(const char* client_id, const char* server_version) {
  Display::logf("hello v%s", server_version);
}

static void onHook(const ClaudeEvents::HookEvent& e) {
  // 21-char budget; prefer tool name when present, else hook type alone.
  if (e.tool_name && *e.tool_name) {
    Display::logf("%s:%s", e.hook_type, e.tool_name);
  } else {
    Display::logf("%s", e.hook_type);
  }
}

static void onPermissionRequest(const ClaudeEvents::PermissionRequestEvent& e) {
  // Invalidate so the status bar updates immediately with the pending id.
  Display::invalidate();
  Display::logf("PERM? %s %s", e.request_id, e.tool_name);
  // A more adventurous firmware could auto-allow trusted tools:
  //   Bridge::sendPermissionVerdict(e.request_id, "allow");
}

static void onPermissionResolved(const ClaudeEvents::PermissionResolvedEvent& e) {
  Display::invalidate();
  Display::logf("%s %s %s",
                e.request_id,
                e.behavior,
                e.applied ? "ok" : "late");
}

static void onInbound(const ClaudeEvents::InboundMessageEvent& e) {
  Display::logf("in: %.15s", e.content);
}

static void onOutbound(const ClaudeEvents::OutboundReplyEvent& e) {
  Display::logf("out: %.14s", e.content);
}

static void onConnection(bool connected) {
  Display::invalidate();
  Display::logf(connected ? "bridge up" : "bridge down");
}

// ---- setup / loop ----------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  LOG_INFO("robot_experiment boot");

  Display::begin();

  WifiMgr::connect(WIFI_SSID, WIFI_PASSWORD);
  ClaudeEvents::setWifiConnected(true);
  Display::invalidate();

  ClaudeEvents::onHello(onHello);
  ClaudeEvents::onHook(onHook);
  ClaudeEvents::onPermissionRequest(onPermissionRequest);
  ClaudeEvents::onPermissionResolved(onPermissionResolved);
  ClaudeEvents::onInboundMessage(onInbound);
  ClaudeEvents::onOutboundReply(onOutbound);
  ClaudeEvents::onConnectionChange(onConnection);

  Bridge::begin(BRIDGE_HOST, BRIDGE_PORT, BRIDGE_TOKEN);
}

void loop() {
  WifiMgr::tick(WIFI_SSID, WIFI_PASSWORD);
  ClaudeEvents::setWifiConnected(WifiMgr::isConnected());

  Bridge::tick();

  // Polled usage pattern: whatever the robot body does, it can inspect
  // ClaudeEvents::state() each pass instead of relying on callbacks.
  //
  //   const auto& st = ClaudeEvents::state();
  //   if (st.working) {  /* blink an LED, hold a pose, etc. */ }

  Display::tick();
}
