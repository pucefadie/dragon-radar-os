#pragma once
#include <stdint.h>
enum class RadarMode : uint8_t { Demo, Closed, Open };

inline RadarMode radarModeFromStorage(uint8_t value, bool legacyDemo) {
  return value <= uint8_t(RadarMode::Open) ? RadarMode(value) :
         legacyDemo ? RadarMode::Demo : RadarMode::Closed;
}
inline RadarMode nextRadarMode(RadarMode mode) {
  return RadarMode((uint8_t(mode)+1)%3);
}

struct OsSettings {
  uint8_t brightness = 150;
  uint8_t volume = 50;
  uint8_t zoom = 2;
  bool sound = true;
  bool vibration = true;
  RadarMode radar_mode = RadarMode::Demo;
  bool heading_up = true;
  float declination = 0;
  float mount_offset = 0;
};
void settingsInit();
OsSettings &settingsGet();
void settingsChanged();
void settingsUpdate();
