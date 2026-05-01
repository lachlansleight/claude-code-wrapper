#include "Display.h"

#include <TFT_eSPI.h>

#include "DebugLog.h"
#include "Settings.h"
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
  tft.fillScreen(Settings::color565(Settings::NamedColor::Background));
  tft.initDMA();

  fb.setColorDepth(16);
  fb.createSprite(kW, kH);
  fbReady = fb.created();
  if (!fbReady) {
    LOG_ERR("framebuffer alloc failed (%d bytes)", kW * kH * 2);
    backlightInit();
    return;
  }
  fb.fillSprite(Settings::color565(Settings::NamedColor::Background));

  backlightInit();

  // Boot splash — also exercises the DMA path.
  fb.fillSprite(Settings::color565(Settings::NamedColor::Background));
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(Settings::color565(Settings::NamedColor::Foreground),
                  Settings::color565(Settings::NamedColor::Background));
  fb.setTextSize(3);
  fb.drawString("Sultana", kW / 2, kH / 2 - 10);
  fb.setTextSize(2);
  fb.drawString("Booting", kW / 2, kH / 2 + 15);
  fb.setTextDatum(TL_DATUM);

  const uint16_t ringColor = Settings::color565(Settings::NamedColor::Excited);
  for (int16_t rad = 109 + 1; rad <= 115; ++rad) {
    fb.drawCircle(120, 120, rad, ringColor);
  }
  pushFrame();
}

void drawConnecting(const char* ssid) {
  if (!fbReady) return;
  fb.fillSprite(Settings::color565(Settings::NamedColor::Background));
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(Settings::color565(Settings::NamedColor::Foreground),
                  Settings::color565(Settings::NamedColor::Background));
  fb.setTextSize(2);
  fb.drawString("Connecting to", kW / 2, kH / 2 - 12);
  fb.setTextColor(Settings::color565(Settings::NamedColor::Excited),
                  Settings::color565(Settings::NamedColor::Background));
  fb.drawString(ssid && *ssid ? ssid : "?", kW / 2, kH / 2 + 14);
  fb.setTextDatum(TL_DATUM);
  pushFrame();
}

void drawFailedToConnect() {
  if (!fbReady) return;
  fb.fillSprite(Settings::color565(Settings::NamedColor::Background));
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(Settings::color565(Settings::NamedColor::Blocked),
                  Settings::color565(Settings::NamedColor::Background));
  fb.setTextSize(2);
  fb.drawString("Failed to", kW / 2, kH / 2 - 12);
  fb.drawString("Connect", kW / 2, kH / 2 + 14);
  fb.setTextDatum(TL_DATUM);
  pushFrame();
}

void drawPortalScreen(const char* ssid, const char* ip) {
  if (!fbReady) return;
  fb.fillSprite(Settings::color565(Settings::NamedColor::Background));
  fb.setTextDatum(MC_DATUM);
  fb.setTextSize(2);
  fb.setTextColor(Settings::color565(Settings::NamedColor::WantsAt),
                  Settings::color565(Settings::NamedColor::Background));
  fb.drawString("CONFIG MODE", kW / 2, kH / 2 - 36);
  fb.setTextColor(Settings::color565(Settings::NamedColor::Foreground),
                  Settings::color565(Settings::NamedColor::Background));
  fb.drawString(ssid, kW / 2, kH / 2);
  fb.setTextSize(1);
  fb.drawString(ip, kW / 2, kH / 2 + 28);
  fb.setTextDatum(TL_DATUM);
  pushFrame();
}

}  // namespace Display
