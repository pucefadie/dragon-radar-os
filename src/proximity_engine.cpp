#include "proximity_engine.h"
#include <algorithm>

namespace {
// Minimum filtered RSSI for levels 2 through 15, in dBm.
constexpr int8_t kLevelThreshold[kProximityMaxLevel - 1] = {
  -84, -77, -70, -64, -59, -54, -49, -44, -39, -34, -29, -25, -21, -15
};
constexpr float kLevelHysteresisDb = 2.5f;
}

void ProximityEngine::sample(int8_t rssi, uint32_t received, uint32_t now) {
  const uint32_t age = now - received;
  if (suspended_ || age >= cfg_.stale_ms || rssi >= 0 || rssi == INT8_MIN) return;
  if (resumePending_ && age > now - resumedAt_) return;
  resumePending_ = false;
  const bool first = !valid_ || received - lastSample_ >= cfg_.stale_ms;
  if (first) {
    count_ = index_ = pendingCount_ = 0;
    filtered_ = reference_ = rssi;
    trend_ = pending_ = ProximityState::Stable;
    quality_ = SignalQuality::Unavailable;
    level_ = 1;
  }
  raw_ = rssi;
  history_[index_] = rssi;
  index_ = (index_ + 1) % 5;
  if (count_ < 5) ++count_;
  int8_t sorted[5];
  std::copy_n(history_, count_, sorted);
  std::sort(sorted, sorted + count_);
  const float median = 0.5f * (sorted[(count_ - 1) / 2] + sorted[count_ / 2]);
  filtered_ += cfg_.smoothing * (median - filtered_);
  const float delta = filtered_ - reference_;
  reference_ += cfg_.reference_smoothing * delta;
  updateQuality();
  updateLevel();
  updateTrend(delta);
  lastSample_ = received;
  seen_ = valid_ = true;
}

void ProximityEngine::updateTrend(float delta) {
  using State = ProximityState;
  State wanted = State::Stable;
  if (delta >= cfg_.trend_db || (trend_ == State::Approaching && delta >= cfg_.trend_exit_db))
    wanted = State::Approaching;
  else if (delta <= -cfg_.trend_db || (trend_ == State::MovingAway && delta <= -cfg_.trend_exit_db))
    wanted = State::MovingAway;

  if (wanted == trend_) {
    pendingCount_ = 0;
    return;
  }
  if (pending_ != wanted) {
    pending_ = wanted;
    pendingCount_ = 0;
  }
  if (++pendingCount_ >= cfg_.trend_samples) {
    trend_ = wanted;
    pendingCount_ = 0;
  }
}

void ProximityEngine::updateQuality() {
  const float weakExit = cfg_.weak + cfg_.hysteresis_db;
  const float nearExit = cfg_.near - cfg_.hysteresis_db;
  if (filtered_ >= cfg_.near || (quality_ == SignalQuality::Strong && filtered_ >= nearExit))
    quality_ = SignalQuality::Strong;
  else if (filtered_ < cfg_.weak || (quality_ == SignalQuality::Weak && filtered_ < weakExit))
    quality_ = SignalQuality::Weak;
  else
    quality_ = SignalQuality::Fair;
}

void ProximityEngine::updateLevel() {
  while (level_ < kProximityMaxLevel && filtered_ >= kLevelThreshold[level_ - 1])
    ++level_;
  while (level_ > 1 && filtered_ < kLevelThreshold[level_ - 2] - kLevelHysteresisDb)
    --level_;
}

ProximitySnapshot ProximityEngine::snapshot(uint32_t now) const {
  ProximitySnapshot s;
  s.seen = seen_;
  s.raw = raw_;
  s.filtered = filtered_;
  s.age_ms = seen_ ? now - lastSample_ : 0;
  s.fresh = valid_ && s.age_ms < cfg_.stale_ms;
  s.quality = s.fresh ? quality_ : SignalQuality::Unavailable;
  s.level = s.fresh ? level_ : 0;
  if (found_) s.state = ProximityState::Found;
  else if (!s.fresh) s.state = seen_ ? ProximityState::Lost : ProximityState::Searching;
  else if (trend_ != ProximityState::Stable) s.state = trend_;
  else if (quality_ == SignalQuality::Strong) s.state = ProximityState::Near;
  else if (quality_ == SignalQuality::Weak) s.state = ProximityState::Weak;
  else s.state = ProximityState::Stable;
  return s;
}

bool ProximityEngine::confirmFound(uint32_t now) {
  if (found_ || !snapshot(now).fresh) return false;
  found_ = true;
  return true;
}

SearchEvent ProximityEngine::confirmOrRestartSearch(uint32_t now, bool paired) {
  if (found_) {
    reset();
    return SearchEvent::None;
  }
  return paired && confirmFound(now) ? SearchEvent::SphereFound : SearchEvent::None;
}

void ProximityEngine::reset() {
  *this = ProximityEngine(cfg_);
}

void ProximityEngine::setSuspended(bool suspended, uint32_t now) {
  reset();
  suspended_ = suspended;
  resumedAt_ = now;
  resumePending_ = !suspended;
}
