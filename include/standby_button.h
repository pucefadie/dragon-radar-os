#pragma once
#include <stdint.h>
// A held press enters standby once; its release must never wake the device.
constexpr uint32_t STANDBY_HOLD_MS = 3000;
class StandbyButtonGate {
 public:
  bool shouldEnter(bool pressed,uint32_t holdMs,bool otaActive) const {
    return pressed && holdMs >= STANDBY_HOLD_MS && !otaActive;
  }
  void entered(bool buttonPressed) { waitingRelease=buttonPressed; }
  bool shouldWake(bool pressed,bool releaseEvent) {
    if (waitingRelease) { if (!pressed) waitingRelease=false; return false; }
    return releaseEvent;
  }
 private:
  bool waitingRelease=false;
};
