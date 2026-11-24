#pragma once
#include <stdint.h>
#include "display_module.h"
#include "proximity_engine.h"
constexpr uint32_t RADAR_SCAN_PERIOD_MS = 6000;
constexpr int16_t RADAR_FOUND_X = 92, RADAR_FOUND_Y = 356;
constexpr int16_t RADAR_FOUND_WIDTH = 282, RADAR_FOUND_HEIGHT = 36;
constexpr int16_t RADAR_GAME_X = 146, RADAR_GAME_Y = 397;
constexpr int16_t RADAR_GAME_WIDTH = 174, RADAR_GAME_HEIGHT = 26;
constexpr int16_t RADAR_SPHERE_X = 81, RADAR_SPHERE_Y = 122;
constexpr int16_t RADAR_SPHERE_PITCH = 44, RADAR_SPHERE_WIDTH = 40, RADAR_SPHERE_HEIGHT = 27;
struct RadarBlip { int16_t x, y; bool offscreen; uint8_t id; };
struct RadarView {
  RadarBlip balls[7]; uint8_t count = 0; uint16_t range_m = 100;
  bool demo = false, compass_online = false, compass_calibrated = false;
  bool heading_up = true, heading_held = false;
  float heading = 0, nearest_m = -1;
  ProximitySnapshot proximity;
};
RadarView appRadarUpdate(const RadarSystemSnapshot &snapshot);
void appRadarZoom();
void appRadarApproach();
void appRadarHaptic(const RadarView &view, uint32_t now);
