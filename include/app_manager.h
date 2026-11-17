#pragma once
#include <stdint.h>
#include "gesture_manager.h"
constexpr uint8_t DEBUG_PAGE_COUNT = 12;
enum class AppId : uint8_t { Radar, Audiobookshelf, Settings, Debug };
class AppManager {
 public:
  void gesture(const GestureEvent &event);
  void longPress();
  void shortPress();
  void showDebugPage(uint8_t n) { current = AppId::Debug; page = n % DEBUG_PAGE_COUNT; launcher = false; }
  AppId active() const { return current; }
  bool launcherOpen() const { return launcher; }
  uint8_t selection() const { return selected; }
  uint8_t debugPage() const { return page; }
 private:
  AppId current = AppId::Radar;
  bool launcher = false;
  uint8_t page = 0;
  uint8_t selected = 0;
};
