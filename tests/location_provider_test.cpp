#include "location_provider.h"
#include <cassert>
int main() {
  DemoProvider demo; RadarPosition p; RadarBall b;
  assert(demo.position(p) && demo.ball(0,b));
  double before=radarDistanceMeters(p,b.position);
  demo.approach(); assert(demo.position(p));
  assert(radarDistanceMeters(p,b.position) < before);
  for (unsigned i=0;i<7;i++) { assert(demo.ball(i,b)); assert(b.id == i+1); }
  assert(!demo.ball(7,b));
  EspNowProvider radio; UwbProvider uwb;
  assert(!radio.ball(0,b) && !uwb.position(p));
}
