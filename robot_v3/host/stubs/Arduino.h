#pragma once
// Host stand-in for <Arduino.h>: only what robot_v3's face, behaviour and
// agents code uses. The clock, RNG and serial sink are provided by
// host/core/sultana_core.cpp.

#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif

using std::max;
using std::min;

template <class T>
inline T constrain(T v, T lo, T hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// ---- Clock --------------------------------------------------------------
// The host sets this before every call into the firmware. It is never 0:
// several firmware fields use 0 to mean "unset".
extern uint32_t g_sultana_millis;
inline uint32_t millis() { return g_sultana_millis; }
inline uint32_t micros() { return g_sultana_millis * 1000u; }
inline void delay(uint32_t) {}

// ---- RNG ----------------------------------------------------------------
// Seedable and deterministic so replays are reproducible.
uint32_t sultana_random_u32();
void sultana_random_seed(uint32_t seed);
inline void randomSeed(unsigned long seed) {
  if (seed != 0) sultana_random_seed((uint32_t)seed);
}
inline long random(long howbig) {
  if (howbig <= 0) return 0;
  return (long)(sultana_random_u32() % (uint32_t)howbig);
}
inline long random(long howsmall, long howbig) {
  if (howsmall >= howbig) return howsmall;
  return howsmall + random(howbig - howsmall);
}

// ---- Serial -------------------------------------------------------------
// LOG_* in core/DebugLog.h expand to Serial.printf; lines go to the host's
// log sink.
void sultana_log_write(const char* text, size_t len);
struct SultanaHostSerial {
  void begin(unsigned long) {}
  int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) sultana_log_write(buf, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1));
    return n;
  }
  size_t print(const char* s) {
    const size_t n = strlen(s);
    sultana_log_write(s, n);
    return n;
  }
  size_t println(const char* s = "") {
    const size_t n = print(s);
    sultana_log_write("\n", 1);
    return n + 1;
  }
};
inline SultanaHostSerial Serial;

// ---- String -------------------------------------------------------------
// Only hal/Settings.cpp uses String (NVS key names).
class String : public std::string {
 public:
  String() {}
  String(const char* s) : std::string(s ? s : "") {}
  String(const std::string& s) : std::string(s) {}
  explicit String(unsigned v) : std::string(std::to_string(v)) {}
  explicit String(int v) : std::string(std::to_string(v)) {}
  String& operator+=(const String& o) {
    std::string::operator+=(o);
    return *this;
  }
  String& operator+=(const char* o) {
    std::string::operator+=(o ? o : "");
    return *this;
  }
};
inline String operator+(const String& a, const char* b) {
  String r(a);
  r += b;
  return r;
}
