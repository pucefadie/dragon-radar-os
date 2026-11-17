#pragma once
#include <stdint.h>
#include "display_module.h"
constexpr uint32_t RADAR_SCAN_PERIOD_MS = 6000;
struct RadarBlip { int16_t x, y; bool offscreen; uint8_t id; };
struct RadarView {
  RadarBlip balls[7]; uint8_t count = 0; uint16_t range_m = 100;
  bool demo = false, gps_valid = false, compass_online = false, compass_calibrated = false;
  bool heading_up = true, heading_held = false;
  float heading = 0, nearest_m = -1;
};
RadarView appRadarUpdate(const RadarSystemSnapshot &snapshot);
void appRadarZoom();
void appRadarApproach();
void appRadarHaptic(const RadarView &view, uint32_t now);
