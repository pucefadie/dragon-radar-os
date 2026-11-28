#pragma once
#include <stdint.h>
#include "proximity_engine.h"

// Pure rendering model. Relative filtered signal, never physical distance.
enum class ClosedBallState : uint8_t { None, Searching, Lost, Signal, Found };
struct ClosedBallVisual {
  ClosedBallState state;
  uint8_t radius;
};
constexpr uint8_t kClosedGridRadius=190;
constexpr uint8_t kClosedMinRadius=12;
constexpr uint8_t closedSphereRadius(uint8_t level) {
  return level == 0 ? 0 : uint8_t(kClosedMinRadius +
      ((unsigned(level > kProximityMaxLevel ? kProximityMaxLevel : level)-1) *
       (kClosedGridRadius-kClosedMinRadius) + (kProximityMaxLevel-1)/2) /
      (kProximityMaxLevel-1));
}
inline ClosedBallVisual closedBallVisual(uint8_t selected, bool paired,
                                        bool linkLost, const ProximitySnapshot &p) {
  if (selected < 1 || selected > 7) return {ClosedBallState::None,0};
  if (p.state==ProximityState::Found) return {ClosedBallState::Found,40};
  if (linkLost || p.state==ProximityState::Lost || (p.seen && !p.fresh))
    return {ClosedBallState::Lost,12};
  if (!paired || !p.fresh || !p.level) return {ClosedBallState::Searching,12};
  return {ClosedBallState::Signal,closedSphereRadius(p.level)};
}

struct ClosedStar { int16_t x,y,r; };
// Six outer positions at 0.53R, plus optional center for odd counts.
inline ClosedStar closedStar(uint8_t id, uint8_t index, uint8_t radius) {
  if (id<1 || id>7 || index>=id || radius<24) return {0,0,0};
  // Keep the entire star constellation clear of the HUD over the large ball.
  const int16_t layoutRadius = radius > 92 ? 92 : radius;
  const int16_t r = layoutRadius/5;
  if ((id&1) && index==0) return {0,0,r};
  const uint8_t n=id-(id&1);
  const uint8_t i=index-(id&1);
  static const int8_t x[6]={0,87,87,0,-87,-87};
  static const int8_t y[6]={-100,-50,50,100,50,-50};
  // Two opposite or four symmetric positions; six evenly spaced.
  static const uint8_t two[2]={0,3};
  static const uint8_t four[4]={1,2,4,5};
  const uint8_t slot=n==2 ? two[i] : n==4 ? four[i] : i;
  const int16_t orbit=layoutRadius*53/100;
  return {int16_t(x[slot]*orbit/100),int16_t(y[slot]*orbit/100),r};
}
