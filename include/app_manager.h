#pragma once
#include <stdint.h>
#include "gesture_manager.h"
constexpr uint8_t DEBUG_SPHERE_MOTOR_PAGE = 12;
constexpr uint8_t DEBUG_SPHERE_RGB_PAGE = DEBUG_SPHERE_MOTOR_PAGE+1;
constexpr uint8_t DEBUG_SPHERE_OTA_PAGE=DEBUG_SPHERE_RGB_PAGE+1;
constexpr uint8_t DEBUG_PAGE_COUNT=DEBUG_SPHERE_OTA_PAGE+1;
enum class SphereOtaUi : uint8_t { Idle, Confirm, Query, Arming, Starting, Ack, Unknown, Refused };
struct DebugSphereOta { uint8_t id=1; SphereOtaUi state=SphereOtaUi::Idle; };
const DebugSphereOta &debugSphereOta();
bool debugSphereOtaTap(const GestureEvent &event);
void debugSphereOtaUpdate();
struct DebugSphereRgb {
  uint8_t id=1, brightness=50, color=0;
  bool unavailable=false;
};
const DebugSphereRgb &debugSphereRgb();
bool debugSphereRgbTap(const GestureEvent &event);
constexpr int16_t DEBUG_SPHERE_MOTOR_X=82, DEBUG_SPHERE_MOTOR_Y=220;
constexpr int16_t DEBUG_SPHERE_MOTOR_WIDTH=302, DEBUG_SPHERE_MOTOR_HEIGHT=44;
inline bool debugSphereMotorTap(const GestureEvent &event) {
  return event.type == Gesture::Tap &&
         event.x >= DEBUG_SPHERE_MOTOR_X && event.x < DEBUG_SPHERE_MOTOR_X+DEBUG_SPHERE_MOTOR_WIDTH &&
         event.y >= DEBUG_SPHERE_MOTOR_Y && event.y < DEBUG_SPHERE_MOTOR_Y+DEBUG_SPHERE_MOTOR_HEIGHT;
}
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
