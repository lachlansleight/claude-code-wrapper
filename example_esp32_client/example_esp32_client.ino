// Claude Code Bridge — ESP32 polling client.
//
// Polls the bridge's /api/firebaseData proxy every 3 seconds for the agent
// state and shows `working` + `lastMessage.summary` on Serial and a 128x32
// I2C OLED (SSD1306 + Adafruit GFX).
//
// The bridge runs on the dev machine and proxies Firebase over HTTPS on our
// behalf, so the ESP32 only needs plain HTTP on the LAN — no root CA pinning
// required on-device.
//
// Requires: Arduino-ESP32 core, ArduinoJson 6.x or 7.x.
// v6: use DynamicJsonDocument (JsonDocument is abstract). v7: JsonDocument is concrete.
//
// Libraries: Adafruit GFX, Adafruit SSD1306, Adafruit BusIO (dependency).
//
// Bridge-side setup:
//   - Set BRIDGE_HOST=0.0.0.0 (default is 127.0.0.1, which blocks LAN clients)
//   - Note the machine's LAN IP (ipconfig / ifconfig)
//   - Copy BRIDGE_TOKEN from ~/.claude/settings.json into this sketch

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ---- Config ----------------------------------------------------------------

#define WIFI_SSID     "Realtime"
#define WIFI_PASSWORD "east4east"

// LAN address of the machine running the bridge.
#define BRIDGE_HOST   "192.168.1.100"
#define BRIDGE_PORT   8787
#define BRIDGE_TOKEN  "e0112a5b1f05"

#define POLL_INTERVAL_MS  2000
#define HTTP_TIMEOUT_MS   5000

// I2C OLED 128x32 (SSD1306). Change SDA/SCL/address if your board differs.
#define OLED_SDA        21
#define OLED_SCL        22
#define OLED_I2C_ADDR   0x3C  // try 0x3D if the display is not detected
#define OLED_RESET      -1
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   32

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int lastStarting = -1;
int lastWorking = -1;

// ---- Implementation --------------------------------------------------------

// Text from the start of `s` up to (but not including) the first ! ? . or newline.
String firstSentence(const char *s) {
  if (!s || !*s) {
    return "";
  }
  const String t = String(s);
  for (unsigned i = 0; i < t.length(); i++) {
    const char c = t[i];
    if (c == '!' || c == '?' || c == '.' || c == '\n') {
      return t.substring(0, i + 1);
    }
  }
  return t;
}

void updateDisplay(bool starting, bool working, const char *summary) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  if (starting) {
    display.setTextWrap(false);
    display.setTextSize(2);
    display.print("Ready!");
  } else if (working) {
    display.setTextWrap(false);
    display.setTextSize(2);
    display.print("Hmmm...");
  } else {
    display.setTextWrap(true);
    display.setTextSize(1);
    String line = firstSentence(summary);
    if (line.length() == 0) {
      line = "(none)";
    }
    display.print(line);
  }
  display.display();
}

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

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println(F("SSD1306 allocation failed — check I2C wiring / address"));
    for (;;) {
      delay(1000);
    }
  }
  display.clearDisplay();
  display.display();

  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi dropped, reconnecting...");
    connectWiFi();
  }

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(16384);
#endif
  if (fetchAgentState(doc)) {
    const bool starting = doc["starting"].is<bool>() ? doc["starting"].as<bool>() : false;

    // `working` is a bool; default to false if absent or wrong type.
    const bool working = doc["working"].is<bool>() ? doc["working"].as<bool>() : false;

    // `lastMessage.summary` is a string; may be absent before the first Stop hook.
    const char *summary = doc["lastMessage"]["summary"].is<const char *>()
                              ? doc["lastMessage"]["summary"].as<const char *>()
                              : "(none)";

    if(lastStarting == -1 || lastWorking == -1) {
      lastStarting = 0;
      lastWorking = 0;
    }
    if(starting && lastStarting == 0) {
      Serial.println("starting!");
    } else if(working && lastWorking == 0) {
      Serial.println("working!");
    } else {
      Serial.println(summary);
    }

    lastStarting = starting ? 1 : 0;
    lastWorking = working ? 1 : 0;

    // Serial.printf("working=%s | summary=%s\n", working ? "true" : "false", summary);
    updateDisplay(starting, working, summary);
  } else {
    Serial.println("Failed :(");
  }

  delay(POLL_INTERVAL_MS);
}
