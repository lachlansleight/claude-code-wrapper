// robot_v2 — ESP32-S3 firmware that connects to the Claude Code bridge,
// drives a procedural face on a 240x240 round GC9A01 TFT, and surfaces
// Claude Code activity via personality state + motion behaviours.
//
// Module map (see ../docs/firmware/OVERVIEW.md for the full tour):
//   config.h          — wifi + bridge credentials (copy from config.example.h)
//   Provisioning      — NVS-backed runtime config + AP-mode config portal
//   WiFiManager       — connect & auto-reconnect
//   BridgeClient      — WebSocket transport, JSON decode, send helpers
//   AgentEvents       — event structs, polled state, callback registry,
//                       session latching
//   ToolFormat        — tool → short label + one-line detail (legacy, unused
//                       by Face; kept for future overlays)
//   AsciiCopy         — UTF-8 → ASCII string helpers (shared)
//   Display           — TFT driver: sprite framebuffer + DMA push
//   Personality       — 8-state machine driven by bridge hooks
//   Face              — renders personality state into the Display sprite
//   Motion            — servo primitives (jog / waggle / thinking osc /
//                       hold) with safe-range clamping
//   MotionBehaviors   — per-state motion table; the single tuning surface
//                       for arm behaviour
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
#include "AgentEvents.h"
#include "DebugLog.h"
#include "Display.h"
#include "FrameController.h"
#include "Motion.h"
#include "MotionBehaviors.h"
#include "Personality.h"
#include "Provisioning.h"
#include "WiFiManager.h"
#include "config.h"

static Provisioning::Config cfg;
static bool g_forceHardcodedProvisioning = false;

static void processSerialCommand(const String& raw) {
  String cmd = raw;
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.length() == 0) return;

  if (cmd == "reboot") {
    LOG_INFO("serial: reboot requested");
    delay(100);
    ESP.restart();
    return;
  }
  if (cmd == "provision-once" || cmd == "provision_once" || cmd == "provision") {
    LOG_INFO("serial: one-time provisioning requested");
    Provisioning::requestOneTimePortal();
    delay(100);
    ESP.restart();
    return;
  }
  if (cmd == "clear-provisioning" || cmd == "forget-all") {
    LOG_INFO("serial: clearing all provisioning data");
    Provisioning::clear();
    return;
  }

  LOG_WARN("serial: unknown command \"%s\"", cmd.c_str());
}

static void tickSerialCommands() {
  static char buf[96];
  static size_t len = 0;
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      buf[len] = '\0';
      processSerialCommand(String(buf));
      len = 0;
      continue;
    }
    if (len < sizeof(buf) - 1) buf[len++] = c;
  }
}

static void onPermissionRequest(const AgentEvents::PermissionRequestEvent& e) {
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

  const bool haveNvs    = Provisioning::load(cfg);
  const bool buttonHeld = Provisioning::shouldEnterPortal();
  const bool oneShotPortalRequested = Provisioning::consumeOneTimePortalRequest();

#ifdef FORCE_HARDCODED_PROVISIONING
  g_forceHardcodedProvisioning = FORCE_HARDCODED_PROVISIONING;
#else
  g_forceHardcodedProvisioning = false;
#endif

  // Build the candidate-network list: nets-from-NVS, with the legacy
  // single-net entry promoted to the head if it's not already in there.
  Provisioning::NetEntry nets[Provisioning::kMaxKnownNetworks];
  size_t netCount = 0;
  if (g_forceHardcodedProvisioning) {
    LOG_INFO("provisioning: FORCE_HARDCODED_PROVISIONING enabled");
    nets[0].ssid         = WIFI_SSID;
    nets[0].password     = WIFI_PASSWORD;
    nets[0].bridge_host  = BRIDGE_HOST;
    nets[0].bridge_port  = BRIDGE_PORT;
    nets[0].bridge_token = BRIDGE_TOKEN;
    netCount = 1;
  } else {
    netCount = Provisioning::loadNetworks(nets, Provisioning::kMaxKnownNetworks);
    if (haveNvs && cfg.wifi_ssid.length() > 0) {
      bool present = false;
      for (size_t i = 0; i < netCount; ++i) {
        if (nets[i].ssid == cfg.wifi_ssid) { present = true; break; }
      }
      if (!present && netCount < Provisioning::kMaxKnownNetworks) {
        // Shift down so legacy entry sits at the head (most-recent).
        for (size_t i = netCount; i > 0; --i) nets[i] = nets[i - 1];
        nets[0].ssid         = cfg.wifi_ssid;
        nets[0].password     = cfg.wifi_password;
        nets[0].bridge_host  = cfg.bridge_host;
        nets[0].bridge_port  = cfg.bridge_port;
        nets[0].bridge_token = cfg.bridge_token;
        ++netCount;
      }
    }
  }

  if (buttonHeld || oneShotPortalRequested || netCount == 0) {
    LOG_INFO("provisioning: entering portal (button=%d one-shot=%d nets=%u)",
             buttonHeld, oneShotPortalRequested, (unsigned)netCount);
    Provisioning::runPortal(cfg);  // blocks; reboots on save/forget
  }

  bool wifiUp = false;
  for (size_t i = 0; i < netCount; ++i) {
    Display::drawConnecting(nets[i].ssid.c_str());
    if (WifiMgr::tryConnect(nets[i].ssid.c_str(),
                            nets[i].password.c_str(),
                            /*timeoutMs=*/15000)) {
      cfg.wifi_ssid     = nets[i].ssid;
      cfg.wifi_password = nets[i].password;
      cfg.bridge_host   = nets[i].bridge_host;
      cfg.bridge_port   = nets[i].bridge_port;
      cfg.bridge_token  = nets[i].bridge_token;
      if (!g_forceHardcodedProvisioning) {
        Provisioning::rememberNetwork(nets[i]);
      }
      wifiUp = true;
      break;
    }
  }

  if (!wifiUp) {
    LOG_WARN("wifi: all %u known networks failed; entering portal",
             (unsigned)netCount);
    Display::drawFailedToConnect();
    delay(3000);
    Provisioning::runPortal(cfg);  // blocks; reboots
  }

  AgentEvents::setWifiConnected(true);
  Face::invalidate();

  Personality::begin();
  AgentEvents::onPermissionRequest(onPermissionRequest);

  Bridge::begin(cfg.bridge_host.c_str(), cfg.bridge_port,
                cfg.bridge_token.c_str());
}

void loop() {
  tickSerialCommands();
  WifiMgr::tick(cfg.wifi_ssid.c_str(), cfg.wifi_password.c_str());
  AgentEvents::setWifiConnected(WifiMgr::isConnected());

  Bridge::tick();
  AgentEvents::tick();

  Personality::tick();
  MotionBehaviors::tick();
  Motion::tick();
  Face::tick();
}
