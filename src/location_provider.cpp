#include "location_provider.h"
// Coordinates are deliberately synthetic; movement advances north in 5 m steps.
bool DemoProvider::position(RadarPosition &out) const { out = {41.900000 + steps * 0.000045, 12.500000}; return true; }
bool DemoProvider::ball(unsigned i, RadarBall &out) const {
  static const RadarPosition positions[] = {{41.900450,12.500000},{41.900350,12.500550},{41.899950,12.501000},{41.899000,12.500700},{41.898200,12.499200},{41.900000,12.494000},{41.906000,12.494000}};
  if (i >= 7) return false;
  out = {(uint8_t)(i + 1), positions[i], true, false, 0}; return true;
}
void DemoProvider::approach() { if (steps < 10) ++steps; else steps = 0; }
