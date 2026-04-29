#include "Provisioning.h"

#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include "DebugLog.h"
#include "Display.h"
#include "config.h"

namespace Provisioning {

// ---- Constants -------------------------------------------------------------

static constexpr const char* kNamespace = "bridge_cfg";

// NVS keys. Max 15 chars each.
static constexpr const char* kKeySsid  = "ssid";
static constexpr const char* kKeyPass  = "pass";
static constexpr const char* kKeyHost  = "host";
static constexpr const char* kKeyPort  = "port";
static constexpr const char* kKeyToken = "token";
// Known-networks list, packed as `ssid\tpass\nssid\tpass\n...`.
// SSIDs / passwords containing literal '\t' or '\n' are not supported,
// which is fine for the standards-compliant inputs the portal accepts.
static constexpr const char* kKeyNets  = "nets";

// BOOT button on most ESP32 dev boards. Active low when pressed.
#ifndef PORTAL_BUTTON_PIN
#define PORTAL_BUTTON_PIN 0
#endif

// How long the button must be held at boot before we enter the portal.
static constexpr uint32_t kHoldMs = 800;

// AP password is empty → open network. Fine for a local config portal that
// only serves one form and reboots after save.
static constexpr const char* kApPassword = "";

// ---- NVS -------------------------------------------------------------------

bool load(Config& out) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/true);

  const bool hasAll =
      p.isKey(kKeySsid) && p.isKey(kKeyPass) &&
      p.isKey(kKeyHost) && p.isKey(kKeyPort) && p.isKey(kKeyToken);

  out.wifi_ssid     = p.getString(kKeySsid,  WIFI_SSID);
  out.wifi_password = p.getString(kKeyPass,  WIFI_PASSWORD);
  out.bridge_host   = p.getString(kKeyHost,  BRIDGE_HOST);
  out.bridge_port   = p.getUShort(kKeyPort,  BRIDGE_PORT);
  out.bridge_token  = p.getString(kKeyToken, BRIDGE_TOKEN);

  p.end();
  return hasAll;
}

void save(const Config& c) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putString(kKeySsid,  c.wifi_ssid);
  p.putString(kKeyPass,  c.wifi_password);
  p.putString(kKeyHost,  c.bridge_host);
  p.putUShort(kKeyPort,  c.bridge_port);
  p.putString(kKeyToken, c.bridge_token);
  p.end();
  LOG_INFO("provisioning: saved");
}

static size_t parseNetworks(const String& packed, NetEntry* out, size_t maxCount) {
  size_t n = 0;
  int start = 0;
  const int len = (int)packed.length();
  while (start < len && n < maxCount) {
    int nl = packed.indexOf('\n', start);
    if (nl < 0) nl = len;
    int tab = packed.indexOf('\t', start);
    if (tab >= 0 && tab < nl) {
      out[n].ssid     = packed.substring(start, tab);
      out[n].password = packed.substring(tab + 1, nl);
      if (out[n].ssid.length() > 0) ++n;
    }
    start = nl + 1;
  }
  return n;
}

static String packNetworks(const NetEntry* entries, size_t count) {
  String out;
  for (size_t i = 0; i < count; ++i) {
    if (entries[i].ssid.length() == 0) continue;
    out += entries[i].ssid;
    out += '\t';
    out += entries[i].password;
    out += '\n';
  }
  return out;
}

size_t loadNetworks(NetEntry* out, size_t maxCount) {
  if (!out || maxCount == 0) return 0;
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/true);
  const String packed = p.getString(kKeyNets, "");
  p.end();
  return parseNetworks(packed, out, maxCount);
}

void rememberNetwork(const char* ssid, const char* password) {
  if (!ssid || !*ssid) return;

  NetEntry entries[kMaxKnownNetworks];
  size_t count = loadNetworks(entries, kMaxKnownNetworks);

  // Drop any existing entry with the same SSID — we'll reinsert at the head.
  size_t writeIdx = 0;
  for (size_t i = 0; i < count; ++i) {
    if (entries[i].ssid != ssid) {
      if (writeIdx != i) entries[writeIdx] = entries[i];
      ++writeIdx;
    }
  }
  count = writeIdx;

  // Shift down to make room at index 0 (cap at kMaxKnownNetworks - 1 so
  // the new entry plus the kept ones fit).
  if (count > kMaxKnownNetworks - 1) count = kMaxKnownNetworks - 1;
  for (size_t i = count; i > 0; --i) entries[i] = entries[i - 1];
  entries[0].ssid     = ssid;
  entries[0].password = password ? password : "";
  count += 1;

  const String packed = packNetworks(entries, count);

  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putString(kKeyNets, packed);
  // Keep legacy single-net keys in sync — `load()` and `WifiMgr::tick()`
  // both still read these for the "current" network.
  p.putString(kKeySsid, ssid);
  p.putString(kKeyPass, password ? password : "");
  p.end();
  LOG_INFO("provisioning: remembered \"%s\" (%u total)", ssid, (unsigned)count);
}

void clear() {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.clear();
  p.end();
  LOG_INFO("provisioning: cleared");
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

// ---- HTML ------------------------------------------------------------------

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
<h1>robot_experiment
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
  <div class="hint">Shared secret from ~/.claude/settings.json → env.BRIDGE_TOKEN</div>

  <div class="actions">
    <button class="primary" type="submit">Save &amp; reboot</button>
    <button class="ghost" type="submit" formaction="/forget">Forget</button>
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

// ---- Templating ------------------------------------------------------------

static String htmlEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    switch (c) {
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '&':  out += "&amp;";  break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += c;
    }
  }
  return out;
}

static String renderPortal(const Config& c) {
  String page = FPSTR(kPortalHtml);
  page.replace("%SSID%",  htmlEscape(c.wifi_ssid));
  page.replace("%PASS%",  htmlEscape(c.wifi_password));
  page.replace("%HOST%",  htmlEscape(c.bridge_host));
  page.replace("%PORT%",  String(c.bridge_port));
  page.replace("%TOKEN%", htmlEscape(c.bridge_token));
  return page;
}

// ---- Portal ----------------------------------------------------------------

static String apSsid() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[16];
  snprintf(buf, sizeof(buf), "robot-%04X",
           (uint16_t)((mac >> 32) ^ (mac & 0xFFFFFFFF)));
  return String(buf);
}

void runPortal(const Config& currentCfg) {
  WiFi.mode(WIFI_AP);
  const String ssid = apSsid();
  WiFi.softAP(ssid.c_str(), kApPassword);
  delay(100);
  const IPAddress ip = WiFi.softAPIP();
  LOG_INFO("portal: ssid=%s ip=%s", ssid.c_str(), ip.toString().c_str());

  Display::drawPortalScreen(ssid.c_str(), ip.toString().c_str());

  WebServer server(80);
  Config staged = currentCfg;

  server.on("/", HTTP_GET, [&]() {
    server.send(200, "text/html", renderPortal(staged));
  });

  server.on("/save", HTTP_POST, [&]() {
    staged.wifi_ssid     = server.arg("ssid");
    staged.wifi_password = server.arg("pass");
    staged.bridge_host   = server.arg("host");
    staged.bridge_port   = (uint16_t)server.arg("port").toInt();
    staged.bridge_token  = server.arg("token");
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

  // Captive-portal-ish catchall: redirect every unknown request to /.
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
