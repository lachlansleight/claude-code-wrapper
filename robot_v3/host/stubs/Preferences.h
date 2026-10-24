#pragma once
// Host stand-in for the ESP32 NVS Preferences API. In-memory only: the
// palette survives sultana_core_init() within a process, not across launches.
#include <stdint.h>

#include <map>
#include <string>

class Preferences {
 public:
  bool begin(const char* ns, bool) {
    ns_ = ns ? ns : "";
    return true;
  }
  void end() {}
  bool clear() {
    store().erase(ns_);
    return true;
  }
  size_t putUShort(const char* k, uint16_t v) { return put(k, v, 2); }
  uint16_t getUShort(const char* k, uint16_t d) { return (uint16_t)get(k, d); }
  size_t putBool(const char* k, bool v) { return put(k, v ? 1 : 0, 1); }
  bool getBool(const char* k, bool d) { return get(k, d ? 1 : 0) != 0; }
  size_t putULong(const char* k, uint32_t v) { return put(k, v, 4); }
  uint32_t getULong(const char* k, uint32_t d) { return get(k, d); }

 private:
  using Map = std::map<std::string, std::map<std::string, uint32_t>>;
  static Map& store() {
    static Map m;
    return m;
  }
  size_t put(const char* k, uint32_t v, size_t bytes) {
    store()[ns_][k] = v;
    return bytes;
  }
  uint32_t get(const char* k, uint32_t d) {
    auto& m = store()[ns_];
    auto it = m.find(k);
    return it == m.end() ? d : it->second;
  }
  std::string ns_;
};
