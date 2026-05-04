#include "src/core/DebugLog.h"
#include "src/app/EventRouter.h"
#include "src/bridge/BridgeClient.h"
#include "src/agents/AgentEvents.h"
#include "src/hal/Display.h"
#include "src/hal/Motion.h"
#include "src/hal/Provisioning.h"
#include "src/hal/ProvisioningUI.h"
#include "src/hal/Settings.h"
#include "src/hal/WiFiManager.h"

namespace {
Provisioning::Config gCfg;
}

void setup() {
  Serial.begin(115200);
  delay(100);

  Settings::begin();
  Display::begin();
  Motion::begin();
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

  AgentEvents::begin();
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
  delay(10);
}
