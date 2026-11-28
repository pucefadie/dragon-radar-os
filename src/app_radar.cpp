#include "app_radar.h"
#include "beacon_service.h"
#include "gps_module.h"
#include <Preferences.h>
#include "radar_feedback.h"
#include "location_provider.h"
#include "settings_manager.h"
#include "peripherals_module.h"
#include <math.h>
static constexpr uint16_t scales[] = {25,50,100,250,500,1000,2000};
namespace {
DemoProvider demo;
DirectionHapticGate directionalHaptic;
RadarPosition locations[SphereSearch::count]{};
uint8_t savedPositions=0, lastSavedId=0;
uint32_t lastSaveMs=0;
GpsSaveStatus lastSave=GpsSaveStatus::Idle;

void locationKey(uint8_t id, char (&key)[3]) {
  key[0]='s';
  key[1]=char('0'+id);
  key[2]='\0';
}

bool canSaveSphere(const SphereGameView &game, const RadarFix &fix) {
  return game.selected && !(game.found & (1u<<(game.selected-1))) && radarUsableFix(fix);
}

void addProjectedBall(RadarView &view, uint8_t id, RadarPosition here, RadarPosition position,
                      double up, uint8_t selected) {
  const double distance=radarDistanceMeters(here,position);
  const double bearing=radarBearingDegrees(here,position);
  if (view.demo && (view.nearest_m < 0 || distance < view.nearest_m)) view.nearest_m=float(distance);
  if (id==selected) {
    view.gps_distance_m=distance;
    view.gps_bearing_deg=bearing;
  }
  const auto p=radarProject(distance,bearing,up,view.range_m,185);
  view.balls[view.count++]={(int16_t)lround(p.x),(int16_t)lround(p.y),p.offscreen,id};
}
}

void appRadarLocationsInit() {
  Preferences p;
  if (!p.begin("sphere_geo",true)) return;
  for (uint8_t id=1; id<=SphereSearch::count; ++id) {
    char key[3];
    locationKey(id,key);
    RadarPosition value{};
    if (p.isKey(key) && p.getBytesLength(key)==sizeof(value) && p.getBytes(key,&value,sizeof(value))==sizeof(value) &&
        radarValidPosition(value)) {
      locations[id-1]=value;
      savedPositions|=uint8_t(1u<<(id-1));
    }
  }
  p.end();
}

void appRadarSaveSpherePosition() {
  if (settingsGet().radar_mode!=RadarMode::Open) return;
  const auto game=beaconServiceSphereGame();
  const auto &g=gpsGetStatus();
  const RadarFix fix{{g.latitude_deg,g.longitude_deg},g.fix_age_ms,g.fix_quality,
                     g.fix_valid && g.status==GPS_STATUS_VALID_FIX};
  if (!canSaveSphere(game,fix)) return;

  lastSavedId=game.selected;
  lastSaveMs=millis();
  lastSave=GpsSaveStatus::Failed;
  Preferences p;
  if (!p.begin("sphere_geo",false)) return;
  char key[3];
  locationKey(game.selected,key);
  if (p.putBytes(key,&fix.position,sizeof(fix.position))==sizeof(fix.position)) {
    locations[game.selected-1]=fix.position;
    savedPositions|=uint8_t(1u<<(game.selected-1));
    lastSave=GpsSaveStatus::Saved;
  }
  p.end();
}

RadarFix appRadarFix(const RadarSystemSnapshot &snap) {
  return {{snap.gps_lat,snap.gps_lon},snap.gps_fix_age_ms,snap.gps_fix_quality,
           snap.gps_fix_valid && snap.gps_status==RADAR_GPS_VALID_FIX};
}
void appRadarZoom() { OsSettings &s = settingsGet(); s.zoom = (s.zoom + 1) % 7; settingsChanged(); }
void appRadarApproach() { demo.approach(); }
RadarView appRadarUpdate(const RadarSystemSnapshot &snap) {
  const OsSettings &s = settingsGet(); RadarView v;
  v.range_m = scales[s.zoom < 7 ? s.zoom : 2];
  v.demo = s.radar_mode==RadarMode::Demo;
  v.open = s.radar_mode==RadarMode::Open;
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
  if (!v.demo) {
    v.proximity=beaconServiceProximity();
    if (!v.open) return v;

    const auto game=beaconServiceSphereGame();
    const auto fix=appRadarFix(snap);
    v.gps_valid=radarUsableFix(fix);
    v.gps_can_save=canSaveSphere(game,fix);
    v.heading_up=s.heading_up && v.compass_online && v.compass_calibrated &&
                 have_heading && isfinite(snap.heading_deg);
    if (lastSavedId==game.selected && millis()-lastSaveMs<2000) v.gps_save=lastSave;

    const uint8_t onMap=game.associated & savedPositions & ~game.found;
    v.selected_position=game.selected && (savedPositions & (1u<<(game.selected-1)));
    if (v.gps_valid)
      for (uint8_t id=1; id<=SphereSearch::count; ++id)
        if (onMap & (1u<<(id-1)))
          addProjectedBall(v,id,fix.position,locations[id-1],v.heading_up ? heading : 0,game.selected);
    return v;
  }
  RadarPosition here;
  demo.position(here);
  for (unsigned i=0; i<7; ++i) {
    RadarBall ball;
    if (!demo.ball(i,ball) || !ball.valid || ball.found) continue;
    addProjectedBall(v,ball.id,here,ball.position,s.heading_up && have_heading ? heading : 0,0);
  }
  return v;
}
void appRadarResetDirectionalHaptic() { directionalHaptic.reset(); }
void appRadarHaptic(const RadarView &v, uint32_t now) {
  if (!v.demo && !v.open) {
    const auto game=beaconServiceSphereGame();
    const auto &signal=v.proximity;
    const bool valid=settingsGet().vibration && signal.fresh &&
        signal.state!=ProximityState::Found &&
        beaconServiceSecureLinkStatus()==SecureLinkStatus::Paired;
    const auto event=directionalHaptic.update(valid,game.selected,
        beaconServiceProximityEpoch(),signal.level,now,
        motorGetStatus().pattern_running || !motorGetStatus().initialized);
    if (event==DirectionHaptic::Approach) motorTriggerShortPulse();
    else if (event==DirectionHaptic::Retreat) motorTriggerDoublePulse();
  } else appRadarResetDirectionalHaptic();
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
