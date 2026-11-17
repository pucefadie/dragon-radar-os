#include "radar_engine.h"
#include <cassert>
#include <cmath>
int main() {
  RadarPosition o{0,0};
  assert(std::abs(radarBearingDegrees(o,{1,0})) < 0.01);
  assert(std::abs(radarBearingDegrees(o,{0,1})-90) < 0.01);
  assert(std::abs(radarBearingDegrees(o,{-1,0})-180) < 0.01);
  assert(std::abs(radarBearingDegrees(o,{0,-1})-270) < 0.01);
  assert(std::abs(radarDistanceMeters(o,{0,1})-111195) < 3);
  assert(radarWrapDegrees(-10)==350 && radarWrapDegrees(370)==10);
  auto p=radarProject(50,90,90,100,200);
  assert(std::abs(p.x)<0.001 && std::abs(p.y+100)<0.001 && !p.offscreen);
  p=radarProject(200,90,0,100,200);
  assert(std::abs(p.x-200)<0.001 && p.offscreen);
}
