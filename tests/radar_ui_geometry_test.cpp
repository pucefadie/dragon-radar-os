#include "radar_ui_geometry.h"
#include <cassert>
int main() {
  using S=ClosedBallState;
  assert(closedSphereRadius(0)==0);
  assert(closedSphereRadius(1)==12 && closedSphereRadius(15)==68);
  assert(closedSphereRadius(255)==68);
  for (uint8_t l=2;l<=15;++l)
    assert(closedSphereRadius(l)>closedSphereRadius(l-1));
  ProximitySnapshot p;
  assert(closedBallVisual(0,true,false,p).state==S::None);
  assert(closedBallVisual(8,true,false,p).state==S::None);
  assert(closedBallVisual(1,false,false,p).state==S::Searching);
  assert(closedBallVisual(1,false,true,p).state==S::Lost);
  p.seen=p.fresh=true; p.level=15; p.state=ProximityState::Near;
  assert(closedBallVisual(1,true,false,p).radius==68);
  assert(closedBallVisual(1,false,false,p).state==S::Searching);
  p.fresh=false;
  assert(closedBallVisual(1,true,false,p).state==S::Lost);
  p.state=ProximityState::Found;
  assert(closedBallVisual(1,false,true,p).state==S::Found);
  assert(closedBallVisual(1,false,true,p).radius==40);
  for (uint8_t id=1;id<=7;++id) {
    for (uint8_t radius=24;radius<=68;radius+=4) {
      for (uint8_t i=0;i<id;++i) {
        const auto a=closedStar(id,i,radius);
        assert(a.r>0 && a.x*a.x+a.y*a.y <= (radius-a.r-4)*(radius-a.r-4));
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
  assert(233-68>122+27 && 233+68<308);
}
