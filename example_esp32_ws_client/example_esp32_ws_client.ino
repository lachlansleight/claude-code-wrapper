// Claude Code Bridge — ESP32 WebSocket client.
//
// Connects directly to the bridge's /ws endpoint and Serial.prints every
// incoming frame. Mirrors what examples/ws-client.html does in a browser.
//
// Requires: Arduino-ESP32 core, WebSockets by Markus Sattler (links2004).
//   Library Manager: "WebSockets" by Markus Sattler (not "WebSockets2_Generic").
//
// Bridge-side setup:
//   - Set BRIDGE_HOST=0.0.0.0 so the bridge listens on the LAN interface.
//   - Note the machine's LAN IP and paste it into BRIDGE_HOST below.
//   - Copy BRIDGE_TOKEN from ~/.claude/settings.json into this sketch.

#include <WiFi.h>
#include <WebSocketsClient.h>

// ---- Config ----------------------------------------------------------------

#define WIFI_SSID     "REPLACE_WITH_YOUR_SSID"
#define WIFI_PASSWORD "REPLACE_WITH_YOUR_PASSWORD"

#define BRIDGE_HOST   "192.168.1.10"
#define BRIDGE_PORT   8787
#define BRIDGE_TOKEN  "REPLACE_WITH_BRIDGE_TOKEN"

// Ping interval (ms). The bridge doesn't require this but it helps detect
// dropped connections quickly.
#define WS_HEARTBEAT_MS  15000

// ---- Implementation --------------------------------------------------------

WebSocketsClient ws;

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

void onWsEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.printf("[ws] connected to %s\n", (const char *)payload);
      break;
    case WStype_DISCONNECTED:
      Serial.println("[ws] disconnected");
      break;
    case WStype_TEXT:
      // Frames from the bridge are always JSON with a `type` field. We just
      // print the raw payload; decoding is left to the caller if needed.
      Serial.printf("[ws] %.*s\n", (int)length, payload);
      break;
    case WStype_BIN:
      Serial.printf("[ws] binary frame len=%u (unexpected)\n", (unsigned)length);
      break;
    case WStype_PING:
      Serial.println("[ws] ping");
      break;
    case WStype_PONG:
      Serial.println("[ws] pong");
      break;
    case WStype_ERROR:
      Serial.printf("[ws] error: %.*s\n", (int)length, payload);
      break;
    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[bridge-ws-client] boot");
  connectWiFi();

  // Browsers can't set headers on WebSocket, so the bridge also accepts the
  // token via the `token` query param. We do the same here — simpler than
  // juggling custom headers.
  const String path = String("/ws?token=") + BRIDGE_TOKEN;
  ws.begin(BRIDGE_HOST, BRIDGE_PORT, path.c_str());
  ws.onEvent(onWsEvent);

  // Retry every 2s if the bridge is down or restarts.
  ws.setReconnectInterval(2000);

  // Send WS-level ping; disconnect if no pong within 3s, after 2 failures.
  ws.enableHeartbeat(WS_HEARTBEAT_MS, 3000, 2);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi dropped, reconnecting...");
    connectWiFi();
  }
  ws.loop();
}
