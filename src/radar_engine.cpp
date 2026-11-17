#include "radar_engine.h"
#include <math.h>
namespace { constexpr double pi = 3.14159265358979323846; constexpr double earth = 6371008.8; double radians(double d) { return d * pi / 180.0; } }
double radarWrapDegrees(double degrees) { double x = fmod(degrees, 360.0); return x < 0 ? x + 360.0 : x; }
double radarDistanceMeters(RadarPosition a, RadarPosition b) {
  double p1 = radians(a.latitude), p2 = radians(b.latitude);
  double dp = p2-p1, dl = radians(b.longitude-a.longitude);
  double h = sin(dp/2)*sin(dp/2) + cos(p1)*cos(p2)*sin(dl/2)*sin(dl/2);
  if (h < 0) h = 0;
  if (h > 1) h = 1;
  return earth * 2 * atan2(sqrt(h), sqrt(1-h));
}
double radarBearingDegrees(RadarPosition a, RadarPosition b) {
  double p1 = radians(a.latitude), p2 = radians(b.latitude), dl = radians(b.longitude-a.longitude);
  return radarWrapDegrees(atan2(sin(dl)*cos(p2), cos(p1)*sin(p2)-sin(p1)*cos(p2)*cos(dl))*180/pi);
}
RadarProjection radarProject(double distance, double bearing, double up, double range, double radius) {
  if (!isfinite(range) || !isfinite(radius) || range <= 0 || radius <= 0 || distance < 0 || !isfinite(distance) || !isfinite(bearing) || !isfinite(up)) return {0, 0, true};
  double r = distance / range * radius; bool off = r > radius; if (off) r = radius;
  double angle = radians(radarWrapDegrees(bearing-up)); return {r*sin(angle), -r*cos(angle), off};
}
