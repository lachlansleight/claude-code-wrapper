#include "SerialDebugging.h"

#include <Arduino.h>

#include "../core/DebugLog.h"
#include "Provisioning.h"

namespace SerialDebugging {

namespace {

// Parse a bridge endpoint like "http://192.168.1.150:8787" (scheme and
// trailing path optional) into host + port. Returns false if a host and
// a valid 1..65535 port can't be extracted.
bool parseBridgeUrl(const String& in, String& host, uint16_t& port) {
  String s = in;
  s.trim();

  const int scheme = s.indexOf("://");
  if (scheme >= 0) s = s.substring(scheme + 3);

  const int slash = s.indexOf('/');
  if (slash >= 0) s = s.substring(0, slash);

  const int colon = s.lastIndexOf(':');
  if (colon < 0) return false;

  host = s.substring(0, colon);
  const long p = s.substring(colon + 1).toInt();
  if (host.length() == 0 || p <= 0 || p > 65535) return false;

  port = (uint16_t)p;
  return true;
}

void dispatch(const String& raw) {
  String line = raw;
  line.trim();
  if (line.length() == 0) return;

  // Split into a command token and its (case-preserved) argument. Only
  // the command is lowercased; args like URLs must keep their case.
  const int sp = line.indexOf(' ');
  String cmd = (sp < 0) ? line : line.substring(0, sp);
  String arg = (sp < 0) ? String() : line.substring(sp + 1);
  cmd.toLowerCase();
  arg.trim();

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
  if (cmd == "/set-provisioning-address") {
    String host;
    uint16_t port = 0;
    if (!parseBridgeUrl(arg, host, port)) {
      LOG_WARN("serial: /set-provisioning-address needs a host:port, e.g. "
               "http://192.168.1.150:8787");
      return;
    }
    LOG_INFO("serial: /set-provisioning-address %s:%u (rebooting)", host.c_str(),
             (unsigned)port);
    Provisioning::setBridgeAddress(host, port);
    delay(100);
    ESP.restart();
    return;
  }

  LOG_WARN("serial: unknown command \"%s\"", cmd.c_str());
}

}  // namespace

void displayStartMessage() {
  LOG_INFO("ROBOT V3 BOOTING");
  LOG_INFO("--------------------------------");
  LOG_INFO("Available Serial Commands:");
  LOG_INFO("/reboot - Reboot the robot");
  LOG_INFO("/provision - One-shot portal on next boot");
  LOG_INFO("/clear-provisioning - Wipe NVS");
  LOG_INFO("/set-provisioning-address <url> - Set bridge host/port");
  LOG_INFO("--------------------------------");
}

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
