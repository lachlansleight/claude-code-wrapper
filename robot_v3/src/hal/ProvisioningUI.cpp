#include "ProvisioningUI.h"

#include "Display.h"
#include "Provisioning.h"

namespace ProvisioningUI {

namespace {

void handlePortalState(const char* ssid, const char* ip) {
  Display::drawPortalScreen(ssid, ip);
}

}  // namespace

void begin() { Provisioning::onPortalState(&handlePortalState); }

}  // namespace ProvisioningUI
