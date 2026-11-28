#include "app_manager.h"
void AppManager::gesture(const GestureEvent &e) {
  if (launcher) {
    if (e.type == Gesture::SwipeDown) launcher = false;
    else if (e.type == Gesture::Tap) {
      const int8_t tile=launcherTileAt(e.x,e.y);
      if (tile>=0) { current=static_cast<AppId>(tile); launcher=false; }
    }
  } else if (e.type == Gesture::SwipeUp && e.y >= GESTURE_BOTTOM_EDGE) {
    selected = static_cast<uint8_t>(current);
    launcher = true;
  }
  else if (e.type == Gesture::Tap && current == AppId::Debug) page = (page + 1) % DEBUG_PAGE_COUNT;
}
void AppManager::longPress() { if (launcher) { current = static_cast<AppId>(selected); launcher = false; } else { selected = static_cast<uint8_t>(current); launcher = true; } }
void AppManager::shortPress() { if (launcher) selected = (selected + 1) % 4; else if (current == AppId::Debug) page = (page + 1) % DEBUG_PAGE_COUNT; }
