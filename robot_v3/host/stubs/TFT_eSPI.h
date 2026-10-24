#pragma once
// Host stand-in for TFT_eSPI: a 240x240 RGB565 software sprite exposing the
// subset of TFT_eSprite that robot_v3's face code calls.
//
// The drawing routines are ported from TFT_eSPI 2.5.44 (TFT_eSPI.cpp and
// Extensions/Sprite.cpp, by Bodmer, FreeBSD licence, with parts derived from
// Adafruit_GFX, BSD licence; see third_party/TFT_eSPI-LICENSE.txt) so that
// every primitive lands on the same pixels as the device:
//   drawPixel, readPixel, drawFastHLine, drawFastVLine, fillRect, fillSprite,
//   drawLine (the sprite's run-length Bresenham), drawCircle and fillCircle
//   (midpoint variants), fillSmoothCircle (anti-aliased, with fastBlend and
//   sqrt_fraction).
// Viewports and datums are not used by the firmware and are fixed at the
// full sprite. Pixels are stored in native RGB565 order; the device stores
// them byte-swapped for DMA, which does not change what is displayed.
//
// Text calls are no-ops: only TextScene (text/debug render modes) uses them,
// and the host forces face mode.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#define TL_DATUM 0
#define MC_DATUM 4

template <typename T>
static inline void transpose(T& a, T& b) {
  T t = a;
  a = b;
  b = t;
}

template <typename A, typename F, typename B>
static inline uint16_t fastBlend(A alpha, F fgc, B bgc) {
  uint32_t rxb = bgc & 0xF81F;
  rxb += ((fgc & 0xF81F) - rxb) * (alpha >> 2) >> 6;
  uint32_t xgx = bgc & 0x07E0;
  xgx += ((fgc & 0x07E0) - xgx) * alpha >> 8;
  return (rxb & 0xF81F) | (xgx & 0x07E0);
}

class TFT_eSPI {
 public:
  static uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  }
};

class TFT_eSprite : public TFT_eSPI {
 public:
  explicit TFT_eSprite(TFT_eSPI* = nullptr) {}

  void setColorDepth(int8_t) {}
  void* createSprite(int16_t w, int16_t h) {
    _iwidth = _vpW = w;
    _vpH = h;
    _img.assign((size_t)w * (size_t)h, 0);
    return _img.data();
  }
  void deleteSprite() { _img.clear(); }
  bool created() const { return !_img.empty(); }
  void* getPointer() { return _img.data(); }
  const uint16_t* pixels() const { return _img.data(); }
  int16_t width() const { return (int16_t)_vpW; }
  int16_t height() const { return (int16_t)_vpH; }

  void drawPixel(int32_t x, int32_t y, uint32_t color) {
    if (!created()) return;
    if ((x < 0) || (y < 0) || (x >= _vpW) || (y >= _vpH)) return;
    _img[x + y * _iwidth] = (uint16_t)color;
  }

  // Alpha-blended pixel (TFT_eSPI::drawPixel with alpha). Returns the colour
  // written.
  uint16_t drawPixel(int32_t x, int32_t y, uint32_t color, uint8_t alpha, uint32_t bg_color) {
    if (bg_color == 0x00FFFFFF) bg_color = readPixel(x, y);
    color = fastBlend(alpha, color, bg_color);
    drawPixel(x, y, color);
    return (uint16_t)color;
  }

  uint16_t readPixel(int32_t x, int32_t y) const {
    if (!created()) return 0xFFFF;
    if ((x < 0) || (y < 0) || (x >= _vpW) || (y >= _vpH)) return 0xFFFF;
    return _img[x + y * _iwidth];
  }

  void fillSprite(uint32_t color) {
    if (!created()) return;
    fillRect(0, 0, _vpW, _vpH, color);
  }
  void fillScreen(uint32_t color) { fillSprite(color); }

  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) {
    if (!created()) return;
    if ((y < 0) || (x >= _vpW) || (y >= _vpH)) return;
    if (x < 0) {
      w += x;
      x = 0;
    }
    if ((x + w) > _vpW) w = _vpW - x;
    if (w < 1) return;
    while (w--) _img[_iwidth * y + x++] = (uint16_t)color;
  }

  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) {
    if (!created()) return;
    if ((x < 0) || (x >= _vpW) || (y >= _vpH)) return;
    if (y < 0) {
      h += y;
      y = 0;
    }
    if ((y + h) > _vpH) h = _vpH - y;
    if (h < 1) return;
    int32_t yp = x + _iwidth * y;
    while (h--) {
      _img[yp] = (uint16_t)color;
      yp += _iwidth;
    }
  }

  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (!created()) return;
    if ((x >= _vpW) || (y >= _vpH)) return;
    if (x < 0) {
      w += x;
      x = 0;
    }
    if (y < 0) {
      h += y;
      y = 0;
    }
    if ((x + w) > _vpW) w = _vpW - x;
    if ((y + h) > _vpH) h = _vpH - y;
    if ((w < 1) || (h < 1)) return;
    for (int32_t j = 0; j < h; ++j) {
      uint16_t* row = &_img[(size_t)(y + j) * _iwidth + x];
      for (int32_t i = 0; i < w; ++i) row[i] = (uint16_t)color;
    }
  }

  // TFT_eSprite::drawLine: Bresenham split into horizontal/vertical runs.
  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    if (!created()) return;
    bool steep = abs(y1 - y0) > abs(x1 - x0);
    if (steep) {
      transpose(x0, y0);
      transpose(x1, y1);
    }
    if (x0 > x1) {
      transpose(x0, x1);
      transpose(y0, y1);
    }
    int32_t dx = x1 - x0, dy = abs(y1 - y0);
    int32_t err = dx >> 1, ystep = -1, xs = x0, dlen = 0;
    if (y0 < y1) ystep = 1;
    if (steep) {
      for (; x0 <= x1; x0++) {
        dlen++;
        err -= dy;
        if (err < 0) {
          err += dx;
          if (dlen == 1) drawPixel(y0, xs, color);
          else drawFastVLine(y0, xs, dlen, color);
          dlen = 0;
          y0 += ystep;
          xs = x0 + 1;
        }
      }
      if (dlen) drawFastVLine(y0, xs, dlen, color);
    } else {
      for (; x0 <= x1; x0++) {
        dlen++;
        err -= dy;
        if (err < 0) {
          err += dx;
          if (dlen == 1) drawPixel(xs, y0, color);
          else drawFastHLine(xs, y0, dlen, color);
          dlen = 0;
          y0 += ystep;
          xs = x0 + 1;
        }
      }
      if (dlen) drawFastHLine(xs, y0, dlen, color);
    }
  }

  // TFT_eSPI::drawCircle: optimised midpoint circle.
  void drawCircle(int32_t x0, int32_t y0, int32_t r, uint32_t color) {
    if (r <= 0) return;
    int32_t f = 1 - r;
    int32_t ddF_y = -2 * r;
    int32_t ddF_x = 1;
    int32_t xs = -1;
    int32_t xe = 0;
    int32_t len = 0;
    bool first = true;
    do {
      while (f < 0) {
        ++xe;
        f += (ddF_x += 2);
      }
      f += (ddF_y += 2);
      if (xe - xs > 1) {
        if (first) {
          len = 2 * (xe - xs) - 1;
          drawFastHLine(x0 - xe, y0 + r, len, color);
          drawFastHLine(x0 - xe, y0 - r, len, color);
          drawFastVLine(x0 + r, y0 - xe, len, color);
          drawFastVLine(x0 - r, y0 - xe, len, color);
          first = false;
        } else {
          len = xe - xs++;
          drawFastHLine(x0 - xe, y0 + r, len, color);
          drawFastHLine(x0 - xe, y0 - r, len, color);
          drawFastHLine(x0 + xs, y0 - r, len, color);
          drawFastHLine(x0 + xs, y0 + r, len, color);
          drawFastVLine(x0 + r, y0 + xs, len, color);
          drawFastVLine(x0 + r, y0 - xe, len, color);
          drawFastVLine(x0 - r, y0 - xe, len, color);
          drawFastVLine(x0 - r, y0 + xs, len, color);
        }
      } else {
        ++xs;
        drawPixel(x0 - xe, y0 + r, color);
        drawPixel(x0 - xe, y0 - r, color);
        drawPixel(x0 + xs, y0 - r, color);
        drawPixel(x0 + xs, y0 + r, color);
        drawPixel(x0 + r, y0 + xs, color);
        drawPixel(x0 + r, y0 - xe, color);
        drawPixel(x0 - r, y0 - xe, color);
        drawPixel(x0 - r, y0 + xs, color);
      }
      xs = xe;
    } while (xe < --r);
  }

  // TFT_eSPI::fillCircle: midpoint circle as horizontal spans.
  void fillCircle(int32_t x0, int32_t y0, int32_t r, uint32_t color) {
    int32_t x = 0;
    int32_t dx = 1;
    int32_t dy = r + r;
    int32_t p = -(r >> 1);
    drawFastHLine(x0 - r, y0, dy + 1, color);
    while (x < r) {
      if (p >= 0) {
        drawFastHLine(x0 - x, y0 + r, dx, color);
        drawFastHLine(x0 - x, y0 - r, dx, color);
        dy -= 2;
        p -= dy;
        r--;
      }
      dx += 2;
      p += dx;
      x++;
      drawFastHLine(x0 - r, y0 + x, dy + 1, color);
      drawFastHLine(x0 - r, y0 - x, dy + 1, color);
    }
  }

  // TFT_eSPI::fillSmoothCircle: filled circle with an anti-aliased edge.
  void fillSmoothCircle(int32_t x, int32_t y, int32_t r, uint32_t color,
                        uint32_t bg_color = 0x00FFFFFF) {
    if (r <= 0) return;
    drawFastHLine(x - r, y, 2 * r + 1, color);
    int32_t xs = 1;
    int32_t cx = 0;
    int32_t r1 = r * r;
    r++;
    int32_t r2 = r * r;
    for (int32_t cy = r - 1; cy > 0; cy--) {
      int32_t dy2 = (r - cy) * (r - cy);
      for (cx = xs; cx < r; cx++) {
        int32_t hyp2 = (r - cx) * (r - cx) + dy2;
        if (hyp2 <= r1) break;
        if (hyp2 >= r2) continue;
        uint8_t alpha = ~sqrt_fraction(hyp2);
        if (alpha > 246) break;
        xs = cx;
        if (alpha < 9) continue;
        if (bg_color == 0x00FFFFFF) {
          drawPixel(x + cx - r, y + cy - r, color, alpha, bg_color);
          drawPixel(x - cx + r, y + cy - r, color, alpha, bg_color);
          drawPixel(x - cx + r, y - cy + r, color, alpha, bg_color);
          drawPixel(x + cx - r, y - cy + r, color, alpha, bg_color);
        } else {
          uint16_t pcol = drawPixel(x + cx - r, y + cy - r, color, alpha, bg_color);
          drawPixel(x - cx + r, y + cy - r, pcol);
          drawPixel(x - cx + r, y - cy + r, pcol);
          drawPixel(x + cx - r, y - cy + r, pcol);
        }
      }
      drawFastHLine(x + cx - r, y + cy - r, 2 * (r - cx) + 1, color);
      drawFastHLine(x + cx - r, y - cy + r, 2 * (r - cx) + 1, color);
    }
  }

  // Text: no-ops on the host (see file comment).
  void setTextSize(uint8_t) {}
  void setTextDatum(uint8_t) {}
  void setTextFont(uint8_t) {}
  void setTextColor(uint16_t) {}
  void setTextColor(uint16_t, uint16_t, bool = false) {}
  int16_t drawString(const char*, int32_t, int32_t) { return 0; }
  int16_t fontHeight() { return 8; }
  int16_t textWidth(const char* s) { return (int16_t)(6 * strlen(s)); }

 private:
  // TFT_eSPI::sqrt_fraction: fixed-point sqrt, returns the fractional byte.
  static inline uint8_t sqrt_fraction(uint32_t num) {
    if (num > (0x40000000)) return 0;
    uint32_t bsh = 0x00004000;
    uint32_t fpr = 0;
    uint32_t osh = 0;
    while (num > bsh) {
      bsh <<= 2;
      osh++;
    }
    do {
      uint32_t bod = bsh + fpr;
      if (num >= bod) {
        num -= bod;
        fpr = bsh + bod;
      }
      num <<= 1;
    } while (bsh >>= 1);
    return fpr >> osh;
  }

  int32_t _iwidth = 0, _vpW = 0, _vpH = 0;
  std::vector<uint16_t> _img;
};
