#include "proximity_engine.h"
#include <cassert>
int main(){
 ProximityConfig c;c.state_hold_ms=0;c.found_enabled=true;c.found_hold_ms=1000;c.lost_ms=5000;ProximityEngine e(c);
 assert(e.snapshot(0).state==ProximityState::Searching);
 e.sample(-80,10);e.sample(-79,200);e.sample(-78,400);e.update(400);assert(e.snapshot(400).seen);
 e.sample(-60,3000);e.update(3000);e.update(3001);assert(e.snapshot(3001).state==ProximityState::Approaching||e.snapshot(3001).state==ProximityState::Stable);
 for(uint32_t t=6000;t<=9000;t+=200){e.sample(-42,t);e.update(t);e.update(t+1);}assert(e.snapshot(9001).state==ProximityState::Found||e.snapshot(9001).state==ProximityState::Near);
 e.update(15001);assert(e.snapshot(15001).state==ProximityState::Lost);
 e.sample(-70,15100);e.update(15100);assert(e.snapshot(15100).state==ProximityState::Lost||e.snapshot(15100).state==ProximityState::Searching);
 e.reset();assert(!e.snapshot(0).seen);
}
