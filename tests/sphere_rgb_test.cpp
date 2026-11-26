#include "../examples/dragon_ball_beacon/sphere_rgb.h"
#include <cassert>
using namespace sphereRgb;
static bool black(Color c) { return c.r==0 && c.g==0 && c.b==0; }

int main() {
  static_assert(kChain.pixelCount==2 && kChain.dataPin==23,"bench configuration");
  Animation gated;
  gated.begin(0,false);
  assert(gated.update(800).r==127 && gated.update(800).g==0);
  assert(black(gated.update(8000)));
  gated.paired(8001);
  assert(gated.update(8002).g==127);
  assert(black(gated.update(8501)));
  gated.paired(8502);
  assert(black(gated.update(8503)));
  gated.manual(true,4,35,9000);
  assert(gated.update(9000).b==89);
  assert(black(gated.update(19000)));
  gated.found(19001);
  assert(gated.update(19376).r==89 && gated.update(19376).g==59);
  assert(black(gated.update(22001)));
  gated.manual(true,5,0,22002);
  assert(black(gated.update(22003)));
  gated.manual(false,6,100,22004);
  assert(black(gated.update(22005)));

  Animation white;
  white.setBrightness(0);
  white.begin(UINT32_MAX-300,true);
  assert(white.update(198).b==255);
  assert(black(white.update(199)));
}
