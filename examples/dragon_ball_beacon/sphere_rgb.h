#pragma once

#include <stdint.h>

namespace sphereRgb {
struct ChainConfig {
  uint8_t dataPin;
  uint8_t pixelCount;
  bool enabled;
};

// Reserved for a future output backend. No GPIO setup is performed in M3a.
constexpr ChainConfig kChain{23, 3, false};
}
