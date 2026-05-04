#pragma once

#include <Arduino.h>

namespace Provisioning {

struct Config {
  String wifi_ssid;
  String wifi_password;
  String bridge_host;
  uint16_t bridge_port;
  String bridge_token;
};

struct NetEntry {
  String ssid;
  String password;
  String bridge_host;
  uint16_t bridge_port;
  String bridge_token;
};

using PortalStateHandler = void (*)(const char* ssid, const char* ip);

static constexpr size_t kMaxKnownNetworks = 8;

bool load(Config& out);
void save(const Config& c);
size_t loadNetworks(NetEntry* out, size_t maxCount);
void rememberNetwork(const NetEntry& entry);
void clear();

void requestOneTimePortal();
bool consumeOneTimePortalRequest();
bool shouldEnterPortal();

void onPortalState(PortalStateHandler handler);
void runPortal(const Config& currentCfg);

}  // namespace Provisioning
