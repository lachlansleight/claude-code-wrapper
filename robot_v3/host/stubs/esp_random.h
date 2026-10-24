#pragma once
// Host stand-in for ESP-IDF's hardware RNG: draws from the same seedable
// generator as random(), so FrameController's randomSeed(esp_random()) is
// deterministic for a given sultana_core_init seed.
#include <stdint.h>
uint32_t sultana_random_u32();
inline uint32_t esp_random() { return sultana_random_u32(); }
