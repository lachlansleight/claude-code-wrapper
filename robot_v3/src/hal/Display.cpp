#include "Display.h"

#include <TFT_eSPI.h>

#include "../core/DebugLog.h"

namespace Display {

namespace {

static constexpr int16_t kW = 240;
static constexpr int16_t kH = 240;

// Display-level fallback colors so hal/Display has no hal/Settings dependency.
static constexpr uint16_t kBackground565 = 0x0000;
static constexpr uint16_t kForeground565 = 0xFFFF;
static constexpr uint16_t kAccent565 = 0xFD20;
static constexpr uint16_t kWarn565 = 0xF8E0;
static constexpr uint16_t kError565 = 0xF800;

TFT_eSPI tft;
TFT_eSprite fb(&tft);
bool fbReady = false;

void backlightInit() {
#ifdef TFT_BL
  ledcAttach(TFT_BL, 5000, 8);
  ledcWrite(TFT_BL, 255);
#endif
}

}  // namespace

void setBrightness(uint8_t pct) {
#ifdef TFT_BL
  if (pct > 100) pct = 100;
  ledcWrite(TFT_BL, (uint32_t)pct * 255 / 100);
#else
  (void)pct;
#endif
}

void pushFrame() {
  if (!fbReady) return;
  tft.startWrite();
  tft.dmaWait();
  tft.pushImageDMA(0, 0, kW, kH, (uint16_t*)fb.getPointer());
  tft.dmaWait();
  tft.endWrite();
}

TFT_eSprite& sprite() { return fb; }
bool ready() { return fbReady; }

void begin() {
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(kBackground565);
  tft.initDMA();

  fb.setColorDepth(16);
  fb.createSprite(kW, kH);
  fbReady = fb.created();
  if (!fbReady) {
    LOG_ERR("framebuffer alloc failed (%d bytes)", kW * kH * 2);
    backlightInit();
    return;
  }
  fb.fillSprite(kBackground565);

  backlightInit();

  fb.fillSprite(kBackground565);
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(kForeground565, kBackground565);
  fb.setTextSize(3);
  fb.drawString("robot_v3", kW / 2, kH / 2 - 10);
  fb.setTextSize(2);
  fb.drawString("Booting", kW / 2, kH / 2 + 15);
  fb.setTextDatum(TL_DATUM);
  for (int16_t rad = 110; rad <= 115; ++rad) fb.drawCircle(120, 120, rad, kAccent565);
  pushFrame();
}

void drawConnecting(const char* ssid) {
  if (!fbReady) return;
  fb.fillSprite(kBackground565);
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(kForeground565, kBackground565);
  fb.setTextSize(2);
  fb.drawString("Connecting to", kW / 2, kH / 2 - 12);
  fb.setTextColor(kAccent565, kBackground565);
  fb.drawString(ssid && *ssid ? ssid : "?", kW / 2, kH / 2 + 14);
  fb.setTextDatum(TL_DATUM);
  pushFrame();
}

void drawFailedToConnect() {
  if (!fbReady) return;
  fb.fillSprite(kBackground565);
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(kError565, kBackground565);
  fb.setTextSize(2);
  fb.drawString("Failed to", kW / 2, kH / 2 - 12);
  fb.drawString("Connect", kW / 2, kH / 2 + 14);
  fb.setTextDatum(TL_DATUM);
  pushFrame();
}

void drawPortalScreen(const char* ssid, const char* ip) {
  if (!fbReady) return;
  fb.fillSprite(kBackground565);
  fb.setTextDatum(MC_DATUM);
  fb.setTextSize(2);
  fb.setTextColor(kWarn565, kBackground565);
  fb.drawString("CONFIG MODE", kW / 2, kH / 2 - 36);
  fb.setTextColor(kForeground565, kBackground565);
  fb.drawString(ssid && *ssid ? ssid : "robot", kW / 2, kH / 2);
  fb.setTextSize(1);
  fb.drawString(ip && *ip ? ip : "-", kW / 2, kH / 2 + 28);
  fb.setTextDatum(TL_DATUM);
  pushFrame();
}

}  // namespace Display
