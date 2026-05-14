#include "SerialDebugging.h"

#include <Arduino.h>

#include "../core/DebugLog.h"
#include "Provisioning.h"

namespace SerialDebugging {

namespace {

void dispatch(const String& raw) {
  String cmd = raw;
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.length() == 0) return;

  if (cmd == "/reboot") {
    LOG_INFO("serial: /reboot");
    delay(100);
    ESP.restart();
    return;
  }
  if (cmd == "/provision") {
    LOG_INFO("serial: /provision (one-shot portal on next boot)");
    Provisioning::requestOneTimePortal();
    delay(100);
    ESP.restart();
    return;
  }
  if (cmd == "/clear-provisioning") {
    LOG_INFO("serial: /clear-provisioning (wiping NVS)");
    Provisioning::clear();
    return;
  }

  LOG_WARN("serial: unknown command \"%s\"", cmd.c_str());
}

}  // namespace

void tick() {
  static char buf[96];
  static size_t len = 0;
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      buf[len] = '\0';
      dispatch(String(buf));
      len = 0;
      continue;
    }
    if (len < sizeof(buf) - 1) buf[len++] = c;
  }
}

}  // namespace SerialDebugging
