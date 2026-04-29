#include "WiFiManager.h"

#include <WiFi.h>

#include "DebugLog.h"

namespace WifiMgr {

static uint32_t lastCheck = 0;

bool tryConnect(const char* ssid, const char* password, uint32_t timeoutMs) {
  LOG_INFO("wifi connecting to \"%s\" (timeout %lums)", ssid,
           (unsigned long)timeoutMs);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  delay(50);
  WiFi.begin(ssid, password);
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 >= timeoutMs) {
      Serial.println();
      LOG_WARN("wifi connect timed out for \"%s\"", ssid);
      WiFi.disconnect(false, false);
      return false;
    }
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  LOG_INFO("wifi connected ip=%s", WiFi.localIP().toString().c_str());
  return true;
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
