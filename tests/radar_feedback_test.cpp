#include "radar_feedback.h"
#include <cassert>
int main() {
  assert(radarProximityZone(0, 101) == 0);
  assert(radarProximityZone(0, 100) == 1);
  assert(radarProximityZone(0, 50) == 2);
  assert(radarProximityZone(0, 25) == 3);
  assert(radarProximityZone(0, 12.5f) == 4);
  assert(radarProximityZone(0, 6.5f) == 5);
  assert(radarProximityZone(0, 3) == 6);
  assert(radarProximityZone(2, 54) == 2);
  assert(radarProximityZone(2, 56) == 1);
  assert(radarProximityZone(6, 120) == 0);
  assert(radarProximityIntervalMs(5) < radarProximityIntervalMs(1));

  using E=DirectionHaptic;
  DirectionHapticGate g;
  assert(g.update(true,1,1,2,100,false)==E::None);
  assert(g.update(true,1,1,3,200,false)==E::Approach);
  assert(g.update(true,1,1,2,300,false)==E::None); // cooldown
  assert(g.update(true,1,1,2,2000,false)==E::None); // no queued event
  assert(g.update(true,1,1,9,2100,false)==E::Approach); // one event for multi-band jump
  assert(g.update(true,1,1,6,3700,false)==E::Retreat);
  assert(g.update(true,1,1,12,5300,true)==E::None); // motor busy including OFF gap
  assert(g.update(true,1,1,12,5400,false)==E::None);
  assert(g.update(false,1,1,0,5500,false)==E::None);
  assert(g.update(true,1,1,15,5600,false)==E::None); // reacquisition
  assert(g.update(true,2,2,1,5700,false)==E::None); // different sphere
  assert(g.update(true,2,3,15,5800,false)==E::None); // new session
  g.reset();
  assert(g.update(true,2,3,3,5900,false)==E::None);
  assert(g.update(true,2,3,6,6000,false)==E::Approach);
  assert(g.update(true,0,3,6,8000,false)==E::None);
  assert(g.update(true,2,3,16,8100,false)==E::None);
  assert(g.update(true,2,3,5,8200,false)==E::None);
  DirectionHapticGate wrap;
  assert(wrap.update(true,1,0,3,UINT32_MAX-1000,false)==E::None);
  assert(wrap.update(true,1,0,6,UINT32_MAX-500,false)==E::Approach);
  assert(wrap.update(true,1,0,3,100,false)==E::None);
  assert(wrap.update(true,1,0,6,999,false)==E::Approach);
  // Same 10% band: 6 and 7 both map to 40%.
  assert(wrap.update(true,1,0,7,3000,false)==E::None);
}
