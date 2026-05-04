#include "Provisioning.h"

#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include "../config.h"
#include "../core/DebugLog.h"

namespace Provisioning {

namespace {

static constexpr const char* kNamespace = "bridge_cfg";
static constexpr const char* kKeySsid = "ssid";
static constexpr const char* kKeyPass = "pass";
static constexpr const char* kKeyHost = "host";
static constexpr const char* kKeyPort = "port";
static constexpr const char* kKeyToken = "token";
static constexpr const char* kKeyNets = "nets";
static constexpr const char* kKeyPortalOnce = "portal_once";
static constexpr const char* kApPassword = "";
static constexpr uint32_t kHoldMs = 800;

#ifndef PORTAL_BUTTON_PIN
#define PORTAL_BUTTON_PIN 0
#endif

PortalStateHandler sPortalStateHandler = nullptr;

String apSsid() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[16];
  snprintf(buf, sizeof(buf), "robot-%04X", (uint16_t)((mac >> 32) ^ (mac & 0xFFFFFFFF)));
  return String(buf);
}

size_t parseNetworks(const String& packed, NetEntry* out, size_t maxCount,
                     const String& defaultHost, uint16_t defaultPort,
                     const String& defaultToken) {
  size_t n = 0;
  int start = 0;
  const int len = (int)packed.length();
  while (start < len && n < maxCount) {
    int nl = packed.indexOf('\n', start);
    if (nl < 0) nl = len;
    const String line = packed.substring(start, nl);
    int p1 = line.indexOf('\t');
    if (p1 >= 0) {
      int p2 = line.indexOf('\t', p1 + 1);
      int p3 = (p2 >= 0) ? line.indexOf('\t', p2 + 1) : -1;
      int p4 = (p3 >= 0) ? line.indexOf('\t', p3 + 1) : -1;

      out[n] = NetEntry();
      if (p2 < 0) {
        out[n].ssid = line.substring(0, p1);
        out[n].password = line.substring(p1 + 1);
        out[n].bridge_host = defaultHost;
        out[n].bridge_port = defaultPort;
        out[n].bridge_token = defaultToken;
      } else if (p3 >= 0 && p4 >= 0) {
        out[n].ssid = line.substring(0, p1);
        out[n].password = line.substring(p1 + 1, p2);
        out[n].bridge_host = line.substring(p2 + 1, p3);
        out[n].bridge_port = (uint16_t)line.substring(p3 + 1, p4).toInt();
        out[n].bridge_token = line.substring(p4 + 1);
      }

      if (out[n].ssid.length() > 0) {
        if (out[n].bridge_host.length() == 0) out[n].bridge_host = defaultHost;
        if (out[n].bridge_port == 0) out[n].bridge_port = defaultPort;
        if (out[n].bridge_token.length() == 0) out[n].bridge_token = defaultToken;
        ++n;
      }
    }
    start = nl + 1;
  }
  return n;
}

String packNetworks(const NetEntry* entries, size_t count) {
  String out;
  for (size_t i = 0; i < count; ++i) {
    if (entries[i].ssid.length() == 0) continue;
    out += entries[i].ssid;
    out += '\t';
    out += entries[i].password;
    out += '\t';
    out += entries[i].bridge_host;
    out += '\t';
    out += String(entries[i].bridge_port);
    out += '\t';
    out += entries[i].bridge_token;
    out += '\n';
  }
  return out;
}

String htmlEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    switch (c) {
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '&':
        out += "&amp;";
        break;
      case '"':
        out += "&quot;";
        break;
      case '\'':
        out += "&#39;";
        break;
      default:
        out += c;
    }
  }
  return out;
}

static const char kPortalHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>robot config</title>
<style>
  :root { color-scheme: dark; }
  body {
    font-family: -apple-system, system-ui, sans-serif;
    background: #0e0e10; color: #e4e4e7;
    margin: 0; padding: 1.5rem; max-width: 28rem; margin-inline: auto;
  }
  h1 { font-size: 1.1rem; font-weight: 600; margin: 0 0 1.25rem; }
  h1 span { color: #a1a1aa; font-weight: 400; font-size: 0.9rem; display: block; margin-top: 0.25rem; }
  label { display: block; font-size: 0.78rem; color: #a1a1aa; margin: 0.9rem 0 0.25rem; text-transform: uppercase; letter-spacing: 0.04em; }
  input {
    width: 100%; box-sizing: border-box;
    background: #18181b; color: #f4f4f5;
    border: 1px solid #27272a; border-radius: 6px;
    padding: 0.55rem 0.7rem; font-size: 0.95rem; font-family: inherit;
  }
  input:focus { outline: none; border-color: #a78bfa; }
  .row { display: flex; gap: 0.75rem; }
  .row > div { flex: 1; }
  .row > div.port { flex: 0 0 6rem; }
  .actions { margin-top: 1.5rem; display: flex; gap: 0.75rem; }
  button {
    flex: 1; padding: 0.65rem; border-radius: 6px; font-size: 0.95rem;
    font-weight: 500; cursor: pointer; font-family: inherit;
    border: 1px solid transparent;
  }
  button.primary { background: #a78bfa; color: #0e0e10; }
  button.primary:hover { background: #c4b5fd; }
  button.ghost { background: transparent; color: #e4e4e7; border-color: #3f3f46; }
  button.ghost:hover { border-color: #a1a1aa; }
  .hint { color: #71717a; font-size: 0.78rem; margin-top: 0.35rem; }
</style>
</head><body>
<h1>robot_v3
  <span>provisioning portal</span>
</h1>

<form method="POST" action="/save">
  <label for="ssid">WiFi SSID</label>
  <input id="ssid" name="ssid" value="%SSID%" autocomplete="off" required>

  <label for="pass">WiFi password</label>
  <input id="pass" name="pass" type="password" value="%PASS%" autocomplete="off">

  <label>Bridge endpoint</label>
  <div class="row">
    <div><input name="host" value="%HOST%" placeholder="host / IP" required></div>
    <div class="port"><input name="port" value="%PORT%" type="number" min="1" max="65535" required></div>
  </div>

  <label for="token">Bridge token</label>
  <input id="token" name="token" value="%TOKEN%" autocomplete="off">

  <div class="actions">
    <button class="primary" type="submit">Save &amp; reboot</button>
    <button class="ghost" type="submit" formaction="/forget">Forget</button>
    <button class="ghost" type="submit" formaction="/forget_all"
            onclick="return confirm('Delete ALL saved provisioned networks and bridge settings?');">
      Force delete all
    </button>
  </div>
</form>
</body></html>
)HTML";

static const char kSavedHtml[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>saved</title>
<style>body{font-family:-apple-system,system-ui,sans-serif;background:#0e0e10;color:#e4e4e7;padding:2rem;text-align:center}</style>
</head><body><h2>Saved. Rebooting...</h2><p>You can close this tab.</p></body></html>
)HTML";

String renderPortal(const Config& c) {
  String page = FPSTR(kPortalHtml);
  page.replace("%SSID%", htmlEscape(c.wifi_ssid));
  page.replace("%PASS%", htmlEscape(c.wifi_password));
  page.replace("%HOST%", htmlEscape(c.bridge_host));
  page.replace("%PORT%", String(c.bridge_port));
  page.replace("%TOKEN%", htmlEscape(c.bridge_token));
  return page;
}

}  // namespace

bool load(Config& out) {
  Preferences p;
  p.begin(kNamespace, true);

  const bool hasAll = p.isKey(kKeySsid) && p.isKey(kKeyPass) && p.isKey(kKeyHost) &&
                      p.isKey(kKeyPort) && p.isKey(kKeyToken);

  out.wifi_ssid = p.getString(kKeySsid, WIFI_SSID);
  out.wifi_password = p.getString(kKeyPass, WIFI_PASSWORD);
  out.bridge_host = p.getString(kKeyHost, BRIDGE_HOST);
  out.bridge_port = p.getUShort(kKeyPort, BRIDGE_PORT);
  out.bridge_token = p.getString(kKeyToken, BRIDGE_TOKEN);

  p.end();
  return hasAll;
}

void save(const Config& c) {
  Preferences p;
  p.begin(kNamespace, false);
  p.putString(kKeySsid, c.wifi_ssid);
  p.putString(kKeyPass, c.wifi_password);
  p.putString(kKeyHost, c.bridge_host);
  p.putUShort(kKeyPort, c.bridge_port);
  p.putString(kKeyToken, c.bridge_token);
  p.end();
  LOG_INFO("provisioning: saved");
}

size_t loadNetworks(NetEntry* out, size_t maxCount) {
  if (!out || maxCount == 0) return 0;
  Preferences p;
  p.begin(kNamespace, true);
  const String packed = p.getString(kKeyNets, "");
  const String defaultHost = p.getString(kKeyHost, BRIDGE_HOST);
  const uint16_t defaultPort = p.getUShort(kKeyPort, BRIDGE_PORT);
  const String defaultToken = p.getString(kKeyToken, BRIDGE_TOKEN);
  p.end();
  return parseNetworks(packed, out, maxCount, defaultHost, defaultPort, defaultToken);
}

void rememberNetwork(const NetEntry& entry) {
  if (entry.ssid.length() == 0) return;

  NetEntry entries[kMaxKnownNetworks];
  size_t count = loadNetworks(entries, kMaxKnownNetworks);

  size_t writeIdx = 0;
  for (size_t i = 0; i < count; ++i) {
    if (entries[i].ssid != entry.ssid) {
      if (writeIdx != i) entries[writeIdx] = entries[i];
      ++writeIdx;
    }
  }
  count = writeIdx;

  if (count > kMaxKnownNetworks - 1) count = kMaxKnownNetworks - 1;
  for (size_t i = count; i > 0; --i) entries[i] = entries[i - 1];
  entries[0] = entry;
  count += 1;

  const String packed = packNetworks(entries, count);

  Preferences p;
  p.begin(kNamespace, false);
  p.putString(kKeyNets, packed);
  p.putString(kKeySsid, entry.ssid);
  p.putString(kKeyPass, entry.password);
  p.putString(kKeyHost, entry.bridge_host);
  p.putUShort(kKeyPort, entry.bridge_port);
  p.putString(kKeyToken, entry.bridge_token);
  p.end();
  LOG_INFO("provisioning: remembered \"%s\" (%u total)", entry.ssid.c_str(), (unsigned)count);
}

void clear() {
  Preferences p;
  p.begin(kNamespace, false);
  p.clear();
  p.end();
  LOG_INFO("provisioning: cleared");
}

void requestOneTimePortal() {
  Preferences p;
  p.begin(kNamespace, false);
  p.putBool(kKeyPortalOnce, true);
  p.end();
}

bool consumeOneTimePortalRequest() {
  Preferences p;
  p.begin(kNamespace, false);
  const bool requested = p.getBool(kKeyPortalOnce, false);
  if (requested) p.putBool(kKeyPortalOnce, false);
  p.end();
  return requested;
}

bool shouldEnterPortal() {
  pinMode(PORTAL_BUTTON_PIN, INPUT_PULLUP);
  const uint32_t t0 = millis();
  while (millis() - t0 < kHoldMs) {
    if (digitalRead(PORTAL_BUTTON_PIN) != LOW) return false;
    delay(10);
  }
  return true;
}

void onPortalState(PortalStateHandler handler) { sPortalStateHandler = handler; }

void runPortal(const Config& currentCfg) {
  WiFi.mode(WIFI_AP);
  const String ssid = apSsid();
  WiFi.softAP(ssid.c_str(), kApPassword);
  delay(100);
  const String ip = WiFi.softAPIP().toString();
  LOG_INFO("portal: ssid=%s ip=%s", ssid.c_str(), ip.c_str());
  if (sPortalStateHandler) sPortalStateHandler(ssid.c_str(), ip.c_str());

  WebServer server(80);
  Config staged = currentCfg;

  server.on("/", HTTP_GET, [&]() { server.send(200, "text/html", renderPortal(staged)); });

  server.on("/save", HTTP_POST, [&]() {
    staged.wifi_ssid = server.arg("ssid");
    staged.wifi_password = server.arg("pass");
    staged.bridge_host = server.arg("host");
    staged.bridge_port = (uint16_t)server.arg("port").toInt();
    staged.bridge_token = server.arg("token");
    save(staged);
    server.send(200, "text/html", FPSTR(kSavedHtml));
    delay(500);
    ESP.restart();
  });

  server.on("/forget", HTTP_POST, [&]() {
    clear();
    server.send(200, "text/html", FPSTR(kSavedHtml));
    delay(500);
    ESP.restart();
  });

  server.on("/forget_all", HTTP_POST, [&]() {
    clear();
    server.send(200, "text/html", FPSTR(kSavedHtml));
    delay(500);
    ESP.restart();
  });

  server.onNotFound([&]() {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  server.begin();
  LOG_INFO("portal: http server started");
  for (;;) {
    server.handleClient();
    delay(2);
  }
}

}  // namespace Provisioning
