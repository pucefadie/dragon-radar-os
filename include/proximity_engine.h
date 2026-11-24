#pragma once
#include <stdint.h>

enum class ProximityState : uint8_t { Searching, Weak, Stable, Approaching, MovingAway, Near, Found, Lost };
enum class SearchEvent : uint8_t { None, SphereFound };
enum class SignalQuality : uint8_t { Unavailable, Weak, Fair, Strong };

constexpr uint8_t kProximityMaxLevel = 15;

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
  uint8_t level = 0;
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
  void updateLevel();
  ProximityConfig cfg_;
  int8_t history_[5]{}, raw_ = 0;
  uint8_t count_ = 0, index_ = 0, pendingCount_ = 0, level_ = 1;
  float filtered_ = 0, reference_ = 0;
  uint32_t lastSample_ = 0;
  uint32_t resumedAt_ = 0;
  bool seen_ = false, valid_ = false, found_ = false;
  bool suspended_ = false, resumePending_ = false;
  SignalQuality quality_ = SignalQuality::Unavailable;
  ProximityState trend_ = ProximityState::Stable, pending_ = ProximityState::Stable;
};

// Game progress is separate from the transient signal filter and radio sessions.
class SphereSearch {
 public:
  static constexpr uint8_t count=7;
  void setAssociated(uint8_t mask);
  bool select(uint8_t id);
  uint8_t selected() const { return selected_; }
  uint8_t associated() const { return associated_; }
  uint8_t found() const { return found_; }
  uint8_t foundCount() const;
  ProximitySnapshot snapshot(uint8_t id, uint32_t now) const;
  void sample(uint8_t id, int8_t rssi, uint32_t received, uint32_t now);
  void invalidate(uint8_t id);
  void setSuspended(bool suspended, uint32_t now);
  SearchEvent confirm(uint32_t now, bool paired);
  void newGame();

 private:
  static uint8_t maskFor(uint8_t id) { return id>=1 && id<=count ? uint8_t(1u<<(id-1)) : 0; }
  ProximityEngine signals_[count];
  uint8_t associated_=0, found_=0, selected_=0;
};
