#include "WiFiManager.h"

#include <WiFi.h>

#include "DebugLog.h"

namespace WifiMgr {

static uint32_t lastCheck = 0;

void connect(const char* ssid, const char* password) {
  LOG_INFO("wifi connecting to \"%s\"", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  LOG_INFO("wifi connected ip=%s", WiFi.localIP().toString().c_str());
}

void tick(const char* ssid, const char* password) {
  const uint32_t now = millis();
  if (now - lastCheck < 2000) return;
  lastCheck = now;
  if (WiFi.status() == WL_CONNECTED) return;
  LOG_WARN("wifi dropped, reconnecting");
  WiFi.disconnect();
  WiFi.begin(ssid, password);
}

bool isConnected() { return WiFi.status() == WL_CONNECTED; }
String ip() { return WiFi.localIP().toString(); }

}  // namespace WifiMgr
