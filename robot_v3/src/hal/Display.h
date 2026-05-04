#pragma once

#include <Arduino.h>

class TFT_eSprite;

namespace Display {

void begin();
void setBrightness(uint8_t pct);
TFT_eSprite& sprite();
bool ready();
void pushFrame();

void drawPortalScreen(const char* ssid, const char* ip);
void drawConnecting(const char* ssid);
void drawFailedToConnect();

}  // namespace Display
