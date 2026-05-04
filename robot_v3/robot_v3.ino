#include "src/core/DebugLog.h"
#include "src/app/EventRouter.h"
#include "src/app/SceneContextFill.h"
#include "src/bridge/BridgeClient.h"
#include "src/face/FrameController.h"
#include "src/face/SceneTypes.h"
#include "src/hal/Display.h"
#include "src/hal/Motion.h"
#include "src/hal/MotionBehaviors.h"
#include "src/hal/Provisioning.h"
#include "src/hal/ProvisioningUI.h"
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

  Display::drawConnecting(gCfg.wifi_ssid.c_str());
  if (!WifiMgr::tryConnect(gCfg.wifi_ssid.c_str(), gCfg.wifi_password.c_str(), 15000)) {
    Display::drawFailedToConnect();
  }

  EventRouter::begin();
  Bridge::onMessage(&EventRouter::onBridgeMessage);
  Bridge::onConnection(&EventRouter::onBridgeConnection);
  Bridge::begin(gCfg.bridge_host.c_str(), gCfg.bridge_port, gCfg.bridge_token.c_str());

  LOG_INFO("robot_v3 phase1+2 foundation ready");
}

void loop() {
  WifiMgr::tick(gCfg.wifi_ssid.c_str(), gCfg.wifi_password.c_str());
  Bridge::tick();
  EventRouter::tick();
  Motion::tick();

  Face::SceneContext ctx;
  SceneContextFill::fill(ctx);
  MotionBehaviors::tick(ctx.effective_expression);
  Face::tick(ctx);

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

  delay(10);
}
