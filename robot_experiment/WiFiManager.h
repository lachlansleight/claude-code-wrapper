#pragma once

#include <Arduino.h>

namespace WifiMgr {

// Block until WiFi associates. Prints dots to Serial.
void connect(const char* ssid, const char* password);

// Call from loop(); auto-reconnects if the link drops.
void tick(const char* ssid, const char* password);

bool isConnected();
String ip();

}  // namespace WifiMgr
