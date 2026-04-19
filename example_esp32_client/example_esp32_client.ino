// Claude Code Bridge — ESP32 polling client.
//
// Polls the bridge's /api/firebaseData proxy every 3 seconds for the agent
// state and logs `working` + `lastMessage.summary` to Serial.
//
// The bridge runs on the dev machine and proxies Firebase over HTTPS on our
// behalf, so the ESP32 only needs plain HTTP on the LAN — no root CA pinning
// required on-device.
//
// Requires: Arduino-ESP32 core, ArduinoJson (v7.x).
//
// Bridge-side setup:
//   - Set BRIDGE_HOST=0.0.0.0 (default is 127.0.0.1, which blocks LAN clients)
//   - Note the machine's LAN IP (ipconfig / ifconfig)
//   - Copy BRIDGE_TOKEN from ~/.claude/settings.json into this sketch

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ---- Config ----------------------------------------------------------------

#define WIFI_SSID     "REPLACE_WITH_YOUR_SSID"
#define WIFI_PASSWORD "REPLACE_WITH_YOUR_PASSWORD"

// LAN address of the machine running the bridge.
#define BRIDGE_HOST   "192.168.1.10"
#define BRIDGE_PORT   8787
#define BRIDGE_TOKEN  "REPLACE_WITH_BRIDGE_TOKEN"

#define POLL_INTERVAL_MS  3000
#define HTTP_TIMEOUT_MS   5000

// ---- Implementation --------------------------------------------------------

void connectWiFi() {
  Serial.printf("Connecting to WiFi SSID \"%s\"", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nConnected. IP: %s\n", WiFi.localIP().toString().c_str());
}

// Fetch the agent state JSON into `out`. Returns true on success.
bool fetchAgentState(JsonDocument &out) {
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);

  const String url = String("http://") + BRIDGE_HOST + ":" + BRIDGE_PORT + "/api/firebaseData";
  if (!http.begin(url)) {
    Serial.println("http.begin failed");
    return false;
  }
  http.addHeader("Authorization", String("Bearer ") + BRIDGE_TOKEN);

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("HTTP %d\n", status);
    http.end();
    return false;
  }

  DeserializationError err = deserializeJson(out, http.getStream());
  http.end();
  if (err) {
    Serial.printf("JSON parse error: %s\n", err.c_str());
    return false;
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[bridge-client] boot");
  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi dropped, reconnecting...");
    connectWiFi();
  }

  JsonDocument doc;
  if (fetchAgentState(doc)) {
    // `working` is a bool; default to false if absent or wrong type.
    const bool working = doc["working"].is<bool>() ? doc["working"].as<bool>() : false;

    // `lastMessage.summary` is a string; may be absent before the first Stop hook.
    const char *summary = doc["lastMessage"]["summary"].is<const char *>()
                              ? doc["lastMessage"]["summary"].as<const char *>()
                              : "(none)";

    Serial.printf("working=%s | summary=%s\n", working ? "true" : "false", summary);
  }

  delay(POLL_INTERVAL_MS);
}
