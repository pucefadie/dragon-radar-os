#include "app_radar.h"
#include "radar_feedback.h"
#include "location_provider.h"
#include "settings_manager.h"
#include "peripherals_module.h"
#include <math.h>
static constexpr uint16_t scales[] = {25,50,100,250,500,1000,2000};
static DemoProvider demo;
void appRadarZoom() { OsSettings &s = settingsGet(); s.zoom = (s.zoom + 1) % 7; settingsChanged(); }
void appRadarApproach() { demo.approach(); }
RadarView appRadarUpdate(const RadarSystemSnapshot &snap) {
  const OsSettings &s = settingsGet(); RadarView v;
  v.range_m = scales[s.zoom < 7 ? s.zoom : 2]; v.demo = s.demo; v.gps_valid = snap.gps_fix_valid && snap.gps_fix_age_ms < 5000;
  v.compass_online = snap.bmm_online; v.compass_calibrated = snap.mag_calibrated; v.heading_up = s.heading_up;
  static float heading = 0; static uint32_t last_motor = 0; static bool have_heading = false;
  if (snap.motor_active) last_motor = snap.uptime_ms;
  bool settling = snap.motor_active || (last_motor && snap.uptime_ms - last_motor < 500);
  if (!settling && snap.bmm_online && isfinite(snap.heading_deg)) {
    float target = (float)radarWrapDegrees(snap.heading_deg + s.mount_offset + s.declination);
    if (!have_heading) heading = target;
    else { float delta = fmodf(target - heading + 540.0f, 360.0f) - 180.0f; heading = (float)radarWrapDegrees(heading + delta * 0.2f); }
    have_heading = true;
  }
  v.heading_held = settling; v.heading = heading;
  if (!s.demo) return v;
  RadarPosition here; demo.position(here);
  for (unsigned i=0; i<7; ++i) {
    RadarBall ball; if (!demo.ball(i, ball) || !ball.valid || ball.found) continue;
    double distance = radarDistanceMeters(here, ball.position), bearing = radarBearingDegrees(here, ball.position);
    if (v.nearest_m < 0 || distance < v.nearest_m) v.nearest_m = (float)distance;
    RadarProjection p = radarProject(distance, bearing, s.heading_up && have_heading ? heading : 0, v.range_m, 185);
    v.balls[v.count++] = {(int16_t)lround(p.x), (int16_t)lround(p.y), p.offscreen, ball.id};
  }
  return v;
}
void appRadarHaptic(const RadarView &v, uint32_t now) {
  static uint8_t zone = 0; static uint32_t last = 0; static bool reached = false;
  if (!settingsGet().vibration || !v.demo || v.nearest_m < 0 || !isfinite(v.nearest_m)) { zone = 0; reached = false; return; }
  zone = radarProximityZone(zone, v.nearest_m);
  if (zone != 6) reached = false;
  if (zone == 6 && !reached) {
    if (motorTriggerTriplePulse()) { reached = true; last = now; }
  } else if (zone > 0 && zone < 6 && now - last >= radarProximityIntervalMs(zone)) {
    if (motorTriggerShortPulse()) last = now;
  }
}
