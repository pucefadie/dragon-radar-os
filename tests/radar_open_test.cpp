#include "app_radar.h"
#include "beacon_service.h"
#include "gps_module.h"
#include "settings_manager.h"
#include <Preferences.h>
#include <cassert>
#include <cmath>
#include <limits>

namespace openTest {
std::map<std::string,std::vector<uint8_t>> storage;
unsigned writes=0;
bool failWrite=false;
}
namespace {
OsSettings settings;
SphereGameView game;
GpsStatusSnapshot gps{};
ProximitySnapshot proximity;
}
OsSettings &settingsGet() { return settings; }
void settingsChanged() {}
SphereGameView beaconServiceSphereGame() { return game; }
ProximitySnapshot beaconServiceProximity() { return proximity; }
const GpsStatusSnapshot &gpsGetStatus() { return gps; }
bool motorTriggerShortPulse() { assert(false); return false; }
bool motorTriggerTriplePulse() { assert(false); return false; }

int main() {
  assert(radarModeFromStorage(255,true)==RadarMode::Demo);
  assert(radarModeFromStorage(255,false)==RadarMode::Closed);
  assert(radarModeFromStorage(2,false)==RadarMode::Open);
  assert(nextRadarMode(RadarMode::Demo)==RadarMode::Closed);
  assert(nextRadarMode(RadarMode::Closed)==RadarMode::Open);
  assert(nextRadarMode(RadarMode::Open)==RadarMode::Demo);

  RadarFix fix{{45,9},10000,1,true};
  assert(radarUsableFix(fix));
  fix.age_ms++;
  assert(!radarUsableFix(fix));
  fix.age_ms=0;
  for (uint8_t q : {0,6,7,8}) {
    fix.quality=q;
    assert(!radarUsableFix(fix));
  }
  assert(!radarValidPosition({0,0}));
  assert(!radarValidPosition({91,0}) && !radarValidPosition({45,181}));
  assert(!radarValidPosition({45,std::numeric_limits<double>::quiet_NaN()}));

  game.associated=3;
  game.selected=2;
  proximity.fresh=true;
  proximity.raw=-45;
  RadarSystemSnapshot near;
  near.gps_fix_valid=true;
  near.gps_status=RADAR_GPS_VALID_FIX;
  near.gps_fix_quality=1;
  near.gps_lat=45;
  near.gps_lon=9;
  near.gps_fix_age_ms=10;

  const RadarPosition corrupted{91,9};
  const auto *badBytes=reinterpret_cast<const uint8_t *>(&corrupted);
  openTest::storage["s1"]=std::vector<uint8_t>(badBytes,badBytes+sizeof(corrupted));
  openTest::storage["s2"]={1,2,3};  // Wrong record size.
  appRadarLocationsInit();
  assert(openTest::writes==0 && openTest::storage["s2"].size()==3);
  const auto demo=appRadarUpdate(near);
  assert(demo.demo && demo.count==7);

  settings.radar_mode=RadarMode::Open;
  auto view=appRadarUpdate(near);
  assert(view.open && view.gps_valid && view.gps_can_save);
  assert(!view.selected_position && view.count==0 && view.proximity.fresh && view.gps_distance_m<0);

  gps.status=GPS_STATUS_VALID_FIX;
  gps.fix_valid=true;
  gps.fix_quality=1;
  gps.latitude_deg=45;
  gps.longitude_deg=9.001;
  appRadarSaveSpherePosition();
  assert(openTest::writes==1 && openTest::storage.count("s2")==1);
  view=appRadarUpdate(near);
  assert(view.count==1 && view.balls[0].id==2 && view.selected_position);
  assert(std::fabs(view.gps_distance_m-78.63)<1 && std::fabs(view.gps_bearing_deg-90)<1);
  assert(!view.heading_up && view.balls[0].x>0 && std::abs(view.balls[0].y)<2);
  assert(view.gps_save==GpsSaveStatus::Saved);
  appRadarZoom(); // From the shared 100 m scale to 250 m.
  assert(appRadarUpdate(near).range_m==250);
  settings.zoom=1;
  assert(appRadarUpdate(near).balls[0].offscreen);
  settings.zoom=2;

  near.bmm_online=true;
  near.mag_calibrated=true;
  near.heading_deg=90;
  view=appRadarUpdate(near);
  assert(view.heading_up && std::abs(view.balls[0].x)<2 && view.balls[0].y<0);

  openTest::failWrite=true;
  gps.longitude_deg=10;
  appRadarSaveSpherePosition();
  assert(openTest::writes==2);
  view=appRadarUpdate(near);
  assert(view.gps_save==GpsSaveStatus::Failed && std::fabs(view.gps_distance_m-78.63)<1);
  openTest::failWrite=false;

  game.selected=1;
  gps.latitude_deg=45.001;
  gps.longitude_deg=9;
  appRadarSaveSpherePosition();
  assert(openTest::writes==3 && openTest::storage.count("s1")==1);
  near.gps_fix_valid=false;
  gps.fix_valid=false;
  appRadarSaveSpherePosition();
  view=appRadarUpdate(near);
  assert(openTest::writes==3 && view.count==0 && !view.gps_valid && !view.gps_can_save);
  assert(view.proximity.fresh && view.proximity.raw==-45);
  near.gps_fix_valid=true;
  view=appRadarUpdate(near);
  assert(view.count==2 && view.balls[0].id==1 && view.balls[1].id==2);
  game.found=1;
  view=appRadarUpdate(near);
  assert(view.count==1 && view.balls[0].id==2 && !view.gps_can_save);
  assert(view.gps_distance_m<0);

  settings.radar_mode=RadarMode::Closed;
  view=appRadarUpdate(near);
  assert(!view.open && !view.demo && view.count==0 && view.proximity.fresh);
  appRadarSaveSpherePosition();
  assert(openTest::writes==3);

  const RadarPosition persisted{44.999,9};
  const auto *storedBytes=reinterpret_cast<const uint8_t *>(&persisted);
  openTest::storage["s7"]=std::vector<uint8_t>(storedBytes,storedBytes+sizeof(persisted));
  appRadarLocationsInit();
  settings.radar_mode=RadarMode::Open;
  game.associated=1u<<6;
  game.selected=7;
  game.found=0;
  view=appRadarUpdate(near);
  assert(openTest::writes==3 && view.count==1 && view.balls[0].id==7);
  assert(std::fabs(view.gps_distance_m-111.2)<1);
}
