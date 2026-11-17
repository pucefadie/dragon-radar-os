#include "gesture_manager.h"
#include <stdlib.h>
GestureEvent GestureManager::update(bool down, int16_t x, int16_t y, uint32_t now) {
  GestureEvent event;
  if (down && !active) { active = true; startX = lastX = x; startY = lastY = y; startTime = now; moved = false; }
  else if (down && active) { lastX = x; lastY = y; if (abs(x - startX) > GESTURE_TAP_SLOP || abs(y - startY) > GESTURE_TAP_SLOP) moved = true; }
  else if (!down && active) {
    active = false;
    const int dx = lastX - startX, dy = lastY - startY;
    event.x = startX; event.y = startY;
    if (abs(dy) >= GESTURE_SWIPE_MIN && abs(dy) > abs(dx))
      event.type = dy < 0 ? Gesture::SwipeUp : Gesture::SwipeDown;
    else if (!moved)
      event.type = now - startTime >= GESTURE_HOLD_MS ? Gesture::Hold : Gesture::Tap;
  }
  return event;
}
