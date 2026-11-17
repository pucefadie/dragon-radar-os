#pragma once
#include <stdint.h>
enum class ProximityState : uint8_t { Searching, Detected, Approaching, Stable, MovingAway, Near, Found, Lost };
struct ProximityConfig { int8_t detected=-92, near=-62, found=-48; float trend_db=3.0f; uint32_t found_hold_ms=2500, state_hold_ms=1200, lost_ms=15000; bool found_enabled=false; };
struct ProximitySnapshot { bool seen=false; int8_t raw=0, median=0; float filtered=0; ProximityState state=ProximityState::Searching; uint32_t age_ms=0; };
class ProximityEngine {
 public:
  explicit ProximityEngine(ProximityConfig config={});
  void sample(int8_t rssi,uint32_t now);
  void update(uint32_t now);
  ProximitySnapshot snapshot(uint32_t now) const;
  void reset();
 private:
  ProximityConfig cfg_; int8_t history_[5]{}; uint8_t count_=0,index_=0;
  bool seen_=false,hasStateSince_=false;int8_t raw_=0,median_=0;float ema_=0,trendBase_=0;
  uint32_t lastSample_=0,trendSince_=0,stateSince_=0,foundSince_=0;
  ProximityState state_=ProximityState::Searching,pending_=ProximityState::Searching;
};
