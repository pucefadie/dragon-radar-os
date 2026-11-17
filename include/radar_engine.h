#pragma once
#include <stdint.h>
struct RadarPosition { double latitude, longitude; };
struct RadarBall {
  uint8_t id;
  RadarPosition position;
  bool valid, found;
  uint32_t updated_ms;
};
struct RadarProjection { double x, y; bool offscreen; };
double radarWrapDegrees(double degrees);
double radarDistanceMeters(RadarPosition from, RadarPosition to);
double radarBearingDegrees(RadarPosition from, RadarPosition to);
RadarProjection radarProject(double distance, double bearing, double up, double range, double radius);
