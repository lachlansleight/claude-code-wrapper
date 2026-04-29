#include "Display.h"

#include <TFT_eSPI.h>

#include "DebugLog.h"
#include "config.h"
#include "SceneTypes.h"

namespace Display {

static constexpr int16_t kW = 240;
static constexpr int16_t kH = 240;

static TFT_eSPI    tft;
static TFT_eSprite fb(&tft);
static bool        fbReady = false;

// ---- Backlight ------------------------------------------------------------

static void backlightInit() {
#ifdef TFT_BL
  ledcAttach(TFT_BL, 5000, 8);
  ledcWrite(TFT_BL, 255);
#endif
}

void setBrightness(uint8_t pct) {
#ifdef TFT_BL
  if (pct > 100) pct = 100;
  ledcWrite(TFT_BL, (uint32_t)pct * 255 / 100);
#else
  (void)pct;
#endif
}

// ---- Frame push -----------------------------------------------------------

void pushFrame() {
  if (!fbReady) return;
  tft.startWrite();
  tft.dmaWait();
  tft.pushImageDMA(0, 0, kW, kH, (uint16_t*)fb.getPointer());
  // Block until the transfer finishes so callers can safely mutate the
  // sprite immediately after this returns. At 80 MHz SPI, 115 KB ≈ 12 ms;
  // with our 33 ms tick budget we're well within headroom.
  tft.dmaWait();
  tft.endWrite();
}

TFT_eSprite& sprite() { return fb; }
bool ready()          { return fbReady; }

// ---- Lifecycle ------------------------------------------------------------

void begin() {
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.initDMA();

  fb.setColorDepth(16);
  fb.createSprite(kW, kH);
  fbReady = fb.created();
  if (!fbReady) {
    LOG_ERR("framebuffer alloc failed (%d bytes)", kW * kH * 2);
    backlightInit();
    return;
  }
  fb.fillSprite(TFT_BLACK);

  backlightInit();

  // Boot splash — also exercises the DMA path.
  fb.fillSprite(TFT_BLACK);
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(TFT_WHITE, TFT_BLACK);
  fb.setTextSize(3);
  fb.drawString("Sultana", kW / 2, kH / 2 - 10);
  fb.setTextSize(2);
  fb.drawString("Booting", kW / 2, kH / 2 + 15);
  fb.setTextDatum(TL_DATUM);

  const uint16_t ringColor = Face::rgb888To565(73, 245, 173);
  for (int16_t rad = 109 + 1; rad <= 115; ++rad) {
    fb.drawCircle(120, 120, rad, ringColor);
  }
  pushFrame();
}

void drawConnecting(const char* ssid) {
  if (!fbReady) return;
  fb.fillSprite(TFT_BLACK);
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(TFT_WHITE, TFT_BLACK);
  fb.setTextSize(2);
  fb.drawString("Connecting to", kW / 2, kH / 2 - 12);
  fb.setTextColor(Face::rgb888To565(73, 245, 173), TFT_BLACK);
  fb.drawString(ssid && *ssid ? ssid : "?", kW / 2, kH / 2 + 14);
  fb.setTextDatum(TL_DATUM);
  pushFrame();
}

void drawFailedToConnect() {
  if (!fbReady) return;
  fb.fillSprite(TFT_BLACK);
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(TFT_RED, TFT_BLACK);
  fb.setTextSize(2);
  fb.drawString("Failed to", kW / 2, kH / 2 - 12);
  fb.drawString("Connect", kW / 2, kH / 2 + 14);
  fb.setTextDatum(TL_DATUM);
  pushFrame();
}

void drawPortalScreen(const char* ssid, const char* ip) {
  if (!fbReady) return;
  fb.fillSprite(TFT_BLACK);
  fb.setTextDatum(MC_DATUM);
  fb.setTextSize(2);
  fb.setTextColor(TFT_YELLOW, TFT_BLACK);
  fb.drawString("CONFIG MODE", kW / 2, kH / 2 - 36);
  fb.setTextColor(TFT_WHITE, TFT_BLACK);
  fb.drawString(ssid, kW / 2, kH / 2);
  fb.setTextSize(1);
  fb.drawString(ip, kW / 2, kH / 2 + 28);
  fb.setTextDatum(TL_DATUM);
  pushFrame();
}

}  // namespace Display
