#pragma once
#include <stdint.h>
struct OsSettings {
  uint8_t brightness = 150;
  uint8_t volume = 50;
  uint8_t zoom = 2;
  bool sound = true;
  bool vibration = true;
  bool demo = true;
  bool heading_up = true;
  float declination = 0;
  float mount_offset = 0;
};
void settingsInit();
OsSettings &settingsGet();
void settingsChanged();
void settingsUpdate();
