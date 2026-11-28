#include "app_manager.h"
void AppManager::gesture(const GestureEvent &e) {
  if (launcher) {
    if (e.type == Gesture::SwipeDown) launcher = false;
    else if (e.type == Gesture::Tap && e.x >= 75 && e.x < 391 && e.y >= 90 && e.y < 370) {
      current = static_cast<AppId>((e.x >= 233 ? 1 : 0) + (e.y >= 230 ? 2 : 0)); launcher = false;
    }
  } else if (e.type == Gesture::SwipeUp && e.y >= GESTURE_BOTTOM_EDGE) {
    selected = static_cast<uint8_t>(current);
    launcher = true;
  }
  else if (e.type == Gesture::Tap && current == AppId::Debug) page = (page + 1) % DEBUG_PAGE_COUNT;
}
void AppManager::longPress() { if (launcher) { current = static_cast<AppId>(selected); launcher = false; } else { selected = static_cast<uint8_t>(current); launcher = true; } }
void AppManager::shortPress() { if (launcher) selected = (selected + 1) % 4; else if (current == AppId::Debug) page = (page + 1) % DEBUG_PAGE_COUNT; }
