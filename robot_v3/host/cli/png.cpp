#include "png.h"

#include <stdio.h>

#include <vector>

namespace {

uint32_t crcTable[256];
bool crcReady = false;

void initCrc() {
  for (uint32_t n = 0; n < 256; ++n) {
    uint32_t c = n;
    for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    crcTable[n] = c;
  }
  crcReady = true;
}

uint32_t crc(const uint8_t* data, size_t len, uint32_t c = 0xFFFFFFFFu) {
  if (!crcReady) initCrc();
  for (size_t i = 0; i < len; ++i) c = crcTable[(c ^ data[i]) & 0xFF] ^ (c >> 8);
  return c;
}

void put32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back((uint8_t)(x >> 24));
  v.push_back((uint8_t)(x >> 16));
  v.push_back((uint8_t)(x >> 8));
  v.push_back((uint8_t)x);
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
  put32(out, (uint32_t)data.size());
  const size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  put32(out, crc(&out[start], out.size() - start) ^ 0xFFFFFFFFu);
}

}  // namespace

bool writePngRgb(const char* path, const uint8_t* rgba, int width, int height) {
  // Raw scanlines: filter byte 0 then RGB.
  std::vector<uint8_t> raw;
  raw.reserve((size_t)height * (1 + (size_t)width * 3));
  for (int y = 0; y < height; ++y) {
    raw.push_back(0);
    for (int x = 0; x < width; ++x) {
      const uint8_t* p = rgba + ((size_t)y * width + x) * 4;
      raw.push_back(p[0]);
      raw.push_back(p[1]);
      raw.push_back(p[2]);
    }
  }

  // zlib stream of stored blocks.
  std::vector<uint8_t> z = {0x78, 0x01};
  size_t pos = 0;
  while (pos < raw.size() || raw.empty()) {
    const size_t n = raw.size() - pos > 65535 ? 65535 : raw.size() - pos;
    const bool last = pos + n == raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back((uint8_t)(n & 0xFF));
    z.push_back((uint8_t)(n >> 8));
    z.push_back((uint8_t)(~n & 0xFF));
    z.push_back((uint8_t)((~n >> 8) & 0xFF));
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
    if (raw.empty()) break;
  }
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) {
    a = (a + c) % 65521;
    b = (b + a) % 65521;
  }
  put32(z, (b << 16) | a);

  std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<uint8_t> ihdr;
  put32(ihdr, (uint32_t)width);
  put32(ihdr, (uint32_t)height);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8-bit, RGB, deflate, no filter, no interlace
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", z);
  chunk(out, "IEND", {});

  FILE* f = fopen(path, "wb");
  if (!f) return false;
  const bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
  return fclose(f) == 0 && ok;
}
