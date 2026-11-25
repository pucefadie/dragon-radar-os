#pragma once

#include <stdint.h>

constexpr uint16_t kMotorStartupPulseMs = 60;
constexpr uint16_t kMotorPulseMs = 120;
constexpr uint16_t kMotorMaxPulseMs = 250;
constexpr uint32_t kMotorCooldownMs = 3000;
static_assert(kMotorStartupPulseMs > 0 && kMotorStartupPulseMs < kMotorPulseMs &&
              kMotorPulseMs <= kMotorMaxPulseMs, "Motor pulse exceeds limit");

enum class MotorTestResult : uint8_t { Started, Busy, CoolingDown, Disabled };

// Output supplies a bounded hardware pulse and can cancel it to force LOW.
template<class Output>
class SphereMotor {
 public:
  explicit SphereMotor(Output &output) : output_(output) {}

  bool begin() {
    enabled_ = output_.begin();
    return enabled_;
  }

  void tick(uint32_t now) {
    if (active_ && uint32_t(now - started_) >= kMotorMaxPulseMs) {
      enabled_ = output_.stop();
      active_ = false;
      stopped_ = now;
      cooling_ = true;
    }
    if (cooling_ && uint32_t(now - stopped_) >= kMotorCooldownMs) cooling_ = false;
  }

  MotorTestResult request(uint32_t now, uint16_t durationMs = kMotorPulseMs) {
    tick(now);
    if (!enabled_) return MotorTestResult::Disabled;
    if (active_) return MotorTestResult::Busy;
    if (cooling_) return MotorTestResult::CoolingDown;
    if (!output_.start(durationMs)) {
      disable();
      return MotorTestResult::Disabled;
    }
    started_ = now;
    active_ = true;
    return MotorTestResult::Started;
  }

  void quiet(uint32_t now) {
    if (!active_) return;
    enabled_=output_.stop();
    active_=false;
    stopped_=now;
    cooling_=true;
  }

  void disable() {
    if (enabled_) output_.stop();
    enabled_ = active_ = false;
  }

 private:
  Output &output_;
  uint32_t started_ = 0, stopped_ = 0;
  bool enabled_ = false, active_ = false, cooling_ = false;
};

class MotorTestCommand {
 public:
  bool consume(char byte) {
    static constexpr char command[] = "motor_test";
    if (byte == '\r' || byte == '\n') {
      const bool match = matched_ == sizeof(command) - 1;
      reset();
      return match;
    }
    if (matched_ < sizeof(command) - 1 && byte == command[matched_]) ++matched_;
    else matched_ = sizeof(command);  // Discard the whole line after the first mismatch.
    return false;
  }
  void reset() { matched_ = 0; }

 private:
  uint8_t matched_ = 0;
};
