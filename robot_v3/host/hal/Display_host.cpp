// Host implementation of hal/Display.h. The sprite is a software RGB565
// buffer (stubs/TFT_eSPI.h); pushFrame() snapshots it for the host to read.

#include "hal/Display.h"

#include <TFT_eSPI.h>
#include <string.h>

#include "host_internal.h"

namespace Display {

namespace {
constexpr int16_t kW = 240;
constexpr int16_t kH = 240;
TFT_eSprite fb;
bool fbReady = false;
uint16_t gPushed[kW * kH];
uint32_t gPushCount = 0;
}  // namespace

void begin() {
  fb.setColorDepth(16);
  fb.createSprite(kW, kH);
  fbReady = fb.created();
  fb.fillSprite(0x0000);
  memset(gPushed, 0, sizeof(gPushed));
  gPushCount = 0;
}

void setBrightness(uint8_t) {}
TFT_eSprite& sprite() { return fb; }
bool ready() { return fbReady; }

void pushFrame() {
  if (!fbReady) return;
  memcpy(gPushed, fb.pixels(), sizeof(gPushed));
  ++gPushCount;
}

// Boot splashes are part of the device's WiFi/discovery flow, which the host
// never runs.
void drawPortalScreen(const char*, const char*) {}
void drawConnecting(const char*) {}
void drawFailedToConnect() {}
void drawSearchingForBridge(uint16_t, uint16_t) {}
void drawBridgeNotFound() {}

}  // namespace Display

namespace SultanaHost {
const uint16_t* pushedFrame() { return Display::gPushed; }
uint32_t pushCount() { return Display::gPushCount; }
}  // namespace SultanaHost
