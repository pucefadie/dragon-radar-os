#pragma once
#include <stdint.h>

enum class ProximityState : uint8_t { Searching, Weak, Stable, Approaching, MovingAway, Near, Found, Lost };
enum class SignalQuality : uint8_t { Unavailable, Weak, Fair, Strong };

struct ProximityConfig {
  int8_t weak = -82, near = -60;
  float hysteresis_db = 4.0f, smoothing = 0.35f, reference_smoothing = 0.25f;
  float trend_db = 2.5f, trend_exit_db = 1.0f;
  uint32_t stale_ms = 10000;
  uint8_t trend_samples = 2;
};

struct ProximitySnapshot {
  bool seen = false, fresh = false;
  int8_t raw = 0;
  float filtered = 0;
  ProximityState state = ProximityState::Searching;
  SignalQuality quality = SignalQuality::Unavailable;
  uint32_t age_ms = 0;
};

class ProximityEngine {
 public:
  explicit ProximityEngine(ProximityConfig config = {}) : cfg_(config) {}
  void sample(int8_t rssi, uint32_t received, uint32_t now);
  ProximitySnapshot snapshot(uint32_t now) const;
  bool confirmFound(uint32_t now);
  void invalidate() { valid_ = false; }
  void reset();
  void setSuspended(bool suspended, uint32_t now);

 private:
  void updateTrend(float delta);
  void updateQuality();
  ProximityConfig cfg_;
  int8_t history_[5]{}, raw_ = 0;
  uint8_t count_ = 0, index_ = 0, pendingCount_ = 0;
  float filtered_ = 0, reference_ = 0;
  uint32_t lastSample_ = 0;
  uint32_t resumedAt_ = 0;
  bool seen_ = false, valid_ = false, found_ = false;
  bool suspended_ = false, resumePending_ = false;
  SignalQuality quality_ = SignalQuality::Unavailable;
  ProximityState trend_ = ProximityState::Stable, pending_ = ProximityState::Stable;
};
