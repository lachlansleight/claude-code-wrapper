#include "src/core/DebugLog.h"
#include "src/app/EventRouter.h"
#include "src/app/SceneContextFill.h"
#include "src/bridge/BridgeClient.h"
#include "src/face/FrameController.h"
#include "src/face/FrameEffective.h"
#include "src/face/SceneTypes.h"
#include "src/hal/Display.h"
#include "src/hal/Motion.h"
#include "src/hal/MotionBehaviors.h"
#include "src/hal/Provisioning.h"
#include "src/hal/ProvisioningUI.h"
#include "src/hal/SerialDebugging.h"
#include "src/hal/Settings.h"
#include "src/hal/WiFiManager.h"

namespace {
Provisioning::Config gCfg;
constexpr uint32_t kSceneContextLogMs = 500;
uint32_t sLastSceneContextLogMs = 0;
}

void setup() {
  Serial.begin(115200);
  delay(100);

  Settings::begin();
  Display::begin();
  Motion::begin();
  MotionBehaviors::begin();
  Face::begin();
  ProvisioningUI::begin();

  const bool hasProvisioned = Provisioning::load(gCfg);
  const bool forcePortal = Provisioning::consumeOneTimePortalRequest();
  if (!hasProvisioned || forcePortal || Provisioning::shouldEnterPortal()) {
    Provisioning::runPortal(gCfg);
  }

  // Try remembered networks most-recent-first. If no list exists yet,
  // fall back to the legacy single provisioned entry from gCfg.
  Provisioning::NetEntry nets[Provisioning::kMaxKnownNetworks];
  size_t netCount = Provisioning::loadNetworks(nets, Provisioning::kMaxKnownNetworks);
  if (netCount == 0 && gCfg.wifi_ssid.length() > 0) {
    nets[0].ssid = gCfg.wifi_ssid;
    nets[0].password = gCfg.wifi_password;
    nets[0].bridge_host = gCfg.bridge_host;
    nets[0].bridge_port = gCfg.bridge_port;
    nets[0].bridge_token = gCfg.bridge_token;
    netCount = 1;
  }

  bool wifiUp = false;
  for (size_t i = 0; i < netCount; ++i) {
    LOG_INFO("wifi attempt %u/%u: \"%s\"",
             (unsigned)(i + 1),
             (unsigned)netCount,
             nets[i].ssid.c_str());
    Display::drawConnecting(nets[i].ssid.c_str());
    if (!WifiMgr::tryConnect(nets[i].ssid.c_str(), nets[i].password.c_str(), 15000)) {
      continue;
    }

    gCfg.wifi_ssid = nets[i].ssid;
    gCfg.wifi_password = nets[i].password;
    gCfg.bridge_host = nets[i].bridge_host;
    gCfg.bridge_port = nets[i].bridge_port;
    gCfg.bridge_token = nets[i].bridge_token;
    Provisioning::rememberNetwork(nets[i]);
    wifiUp = true;
    break;
  }

  if (!wifiUp) {
    Display::drawFailedToConnect();
    LOG_WARN("wifi connect failed on all remembered networks, entering provisioning portal");
    Provisioning::runPortal(gCfg);
  }

  EventRouter::begin();
  Bridge::onMessage(&EventRouter::onBridgeMessage);
  Bridge::onConnection(&EventRouter::onBridgeConnection);
  Bridge::begin(gCfg.bridge_host.c_str(), gCfg.bridge_port, gCfg.bridge_token.c_str());

  LOG_INFO("robot_v3 phase1+2 foundation ready");
}

void loop() {
  SerialDebugging::tick();
  WifiMgr::tick(gCfg.wifi_ssid.c_str(), gCfg.wifi_password.c_str());
  Bridge::tick();
  EventRouter::tick();

  Face::SceneContext ctx;
  SceneContextFill::fill(ctx);
  const uint32_t now = millis();
  Face::tickEffectiveParams(ctx, now);
  MotionBehaviors::tick(ctx);
  Motion::tick();
  Face::tick(ctx);

  #if DEBUG_SERIAL_VERBOSE
  const uint32_t now = millis();
  if (now - sLastSceneContextLogMs >= kSceneContextLogMs) {
    sLastSceneContextLogMs = now;
    LOG_INFO(
        "[ctx] expr=%s V=%d A=%d latch=%s pend=%s rw=%u/%u ws=%d st=%.40s",
        Face::expressionName(ctx.effective_expression),
        (int)(ctx.mood_v * 100.0f), (int)(ctx.mood_a * 100.0f), ctx.latched_session[0] ? ctx.latched_session : "-",
        ctx.pending_permission[0] ? ctx.pending_permission : "-", (unsigned)ctx.read_tools_this_turn,
        (unsigned)ctx.write_tools_this_turn, ctx.ws_connected ? 1 : 0, ctx.status_line);
  }
  #endif

  delay(10);
}
