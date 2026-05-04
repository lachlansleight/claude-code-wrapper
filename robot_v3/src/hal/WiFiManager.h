#pragma once

#include <Arduino.h>

namespace WifiMgr {

bool tryConnect(const char* ssid, const char* password, uint32_t timeoutMs);
void tick(const char* ssid, const char* password);
bool isConnected();
String ip();

}  // namespace WifiMgr
