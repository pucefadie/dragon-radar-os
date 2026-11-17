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
}
