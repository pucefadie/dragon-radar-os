#pragma once
#include <stdint.h>
#include "display_module.h"
#include "proximity_engine.h"
#include "radar_engine.h"
constexpr uint32_t RADAR_SCAN_PERIOD_MS = 6000;
constexpr int16_t RADAR_FOUND_X = 92, RADAR_FOUND_Y = 356;
constexpr int16_t RADAR_FOUND_WIDTH = 282, RADAR_FOUND_HEIGHT = 36;
constexpr int16_t RADAR_GAME_X = 146, RADAR_GAME_Y = 397;
constexpr int16_t RADAR_GAME_WIDTH = 174, RADAR_GAME_HEIGHT = 26;
constexpr int16_t RADAR_GPS_X = 104, RADAR_OPEN_GAME_X = 238, RADAR_OPEN_ACTION_WIDTH = 124;
enum class GpsSaveStatus : uint8_t { Idle, Saved, Failed };
constexpr int16_t RADAR_SPHERE_X = 81, RADAR_SPHERE_Y = 122;
constexpr int16_t RADAR_SPHERE_PITCH = 44, RADAR_SPHERE_WIDTH = 40, RADAR_SPHERE_HEIGHT = 27;
struct RadarBlip { int16_t x, y; bool offscreen; uint8_t id; };
struct RadarView {
  RadarBlip balls[7]; uint8_t count = 0; uint16_t range_m = 100;
  bool demo = false, compass_online = false, compass_calibrated = false;
  bool heading_up = true, heading_held = false;
  bool open = false, gps_valid = false, selected_position = false, gps_can_save = false;
  double gps_distance_m = -1, gps_bearing_deg = 0;
  GpsSaveStatus gps_save = GpsSaveStatus::Idle;
  float heading = 0, nearest_m = -1;
  ProximitySnapshot proximity;
};
void appRadarLocationsInit();
void appRadarSaveSpherePosition();
RadarFix appRadarFix(const RadarSystemSnapshot &snapshot);
RadarView appRadarUpdate(const RadarSystemSnapshot &snapshot);
void appRadarZoom();
void appRadarApproach();
void appRadarHaptic(const RadarView &view, uint32_t now);
void appRadarResetDirectionalHaptic();
