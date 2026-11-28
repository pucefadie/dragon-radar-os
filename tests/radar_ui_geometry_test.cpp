#include "radar_ui_geometry.h"
#include <cassert>
int main() {
  using S=ClosedBallState;
  assert(closedSphereRadius(0)==0);
  assert(closedSphereRadius(1)==12 && closedSphereRadius(15)==kClosedGridRadius);
  assert(kClosedGridRadius==190 && closedSphereRadius(255)==190);
  assert(closedSphereRadius(8)==101);
  for (uint8_t l=2;l<=15;++l)
    assert(closedSphereRadius(l)>closedSphereRadius(l-1));
  ProximitySnapshot p;
  assert(closedBallVisual(0,true,false,p).state==S::None);
  assert(closedBallVisual(8,true,false,p).state==S::None);
  assert(closedBallVisual(1,false,false,p).state==S::Searching);
  assert(closedBallVisual(1,false,true,p).state==S::Lost);
  p.seen=p.fresh=true; p.level=15; p.state=ProximityState::Near;
  assert(closedBallVisual(1,true,false,p).radius==190);
  assert(closedBallVisual(1,false,false,p).state==S::Searching);
  p.fresh=false;
  assert(closedBallVisual(1,true,false,p).state==S::Lost);
  p.state=ProximityState::Found;
  assert(closedBallVisual(1,false,true,p).state==S::Found);
  assert(closedBallVisual(1,false,true,p).radius==40);
  for (uint8_t id=1;id<=7;++id) {
    for (unsigned radius=24;radius<=kClosedGridRadius;++radius) {
      for (uint8_t i=0;i<id;++i) {
        const auto a=closedStar(id,i,radius);
        const int clearance=int(radius)-a.r-4;
        assert(a.r>0 && a.x*a.x+a.y*a.y <= clearance*clearance);
        assert(233+a.y-a.r>149 && 233+a.y+a.r<304);
        for (uint8_t j=0;j<i;++j) {
          const auto b=closedStar(id,j,radius);
          const int dx=a.x-b.x,dy=a.y-b.y;
          assert(dx*dx+dy*dy>(a.r+b.r)*(a.r+b.r));
        }
      }
    }
    assert(closedStar(id,id,68).r==0);
    assert(closedStar(id,0,12).r==0);
  }
  assert(233-kClosedGridRadius==43 && 233+kClosedGridRadius==423);
  assert(233+kClosedGridRadius<466);
}
