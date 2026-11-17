#pragma once
#include <stdint.h>

enum class Gesture : uint8_t { None, Tap, SwipeUp, SwipeDown, Hold };
struct GestureEvent { Gesture type = Gesture::None; int16_t x = 0, y = 0; };
class GestureManager {
 public:
  GestureEvent update(bool down, int16_t x, int16_t y, uint32_t now);
 private:
  bool active = false;
  int16_t startX = 0, startY = 0, lastX = 0, lastY = 0;
  uint32_t startTime = 0;
  bool moved = false;
};
constexpr int16_t GESTURE_SWIPE_MIN = 70;
constexpr int16_t GESTURE_TAP_SLOP = 24;
constexpr uint32_t GESTURE_HOLD_MS = 700;
constexpr int16_t GESTURE_BOTTOM_EDGE = 365;
