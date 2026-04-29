#pragma once

// Runtime configuration loaded from NVS (ESP32 flash-backed key-value store),
// with compile-time fallbacks from config.h. Entry into the config portal is
// triggered by holding the BOOT button (GPIO0) at power-on, or automatically
// when no usable creds exist.
//
// Portal flow:
//   - ESP32 switches to SoftAP mode, SSID = "robot-XXXX" (MAC suffix).
//   - User connects; any HTTP request redirects to a config form.
//   - On save → values written to NVS → device reboots and loads the new
//     config via the normal Stat-mode path.

#include <Arduino.h>

namespace Provisioning {

struct Config {
  String   wifi_ssid;
  String   wifi_password;
  String   bridge_host;
  uint16_t bridge_port;
  String   bridge_token;
};

struct NetEntry {
  String ssid;
  String password;
};

// Max known networks remembered across reboots. New connects past this
// cap evict the least-recently-used entry.
static constexpr size_t kMaxKnownNetworks = 8;

// Populate `out` from NVS; missing fields fall back to compile-time defaults
// in config.h. Returns true if every field came from NVS (i.e. the device
// has been provisioned at least once).
bool load(Config& out);

// Persist `c` into NVS. Overwrites any existing values.
void save(const Config& c);

// Load the saved network list, ordered most-recently-connected first.
// Writes up to `maxCount` entries into `out` and returns how many.
size_t loadNetworks(NetEntry* out, size_t maxCount);

// Promote (or insert) `ssid`/`password` to the head of the saved network
// list. Existing entry with the same SSID is updated (password may have
// changed) and bumped to the front. List is capped at kMaxKnownNetworks
// (oldest evicted). Also mirrors into the legacy `ssid`/`pass` NVS keys
// so `Provisioning::load()` and `WifiMgr::tick()` keep working without
// list awareness. Persists immediately.
void rememberNetwork(const char* ssid, const char* password);

// Wipe all provisioning keys from NVS.
void clear();

// True if the BOOT button (GPIO0 by default) is held low at boot. Call from
// setup() before starting WiFi.
bool shouldEnterPortal();

// Bring up the SoftAP + HTTP config form. Blocking — returns only after the
// user clicks Save (which reboots) or Forget (which clears + reboots).
// `currentSsid` is displayed on the OLED.
void runPortal(const Config& currentCfg);

}  // namespace Provisioning
