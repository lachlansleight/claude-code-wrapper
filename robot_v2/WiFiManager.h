#pragma once

#include <Arduino.h>

namespace WifiMgr {

// Try to associate with `ssid`/`password`, blocking up to `timeoutMs`.
// Returns true on success, false on timeout. Prints dots to Serial.
bool tryConnect(const char* ssid, const char* password, uint32_t timeoutMs);

// Call from loop(); auto-reconnects if the link drops.
void tick(const char* ssid, const char* password);

bool isConnected();
String ip();

}  // namespace WifiMgr
