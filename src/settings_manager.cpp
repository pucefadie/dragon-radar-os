#include "settings_manager.h"
#include <Arduino.h>
#include <Preferences.h>
namespace { OsSettings s; bool dirty = false; uint32_t changed = 0; }
void settingsInit() {
  Preferences p;
  if (!p.begin("radar_os", true)) return;
  if (p.getUInt("schema", 0) == 1) {
    s.brightness = p.getUChar("brightness", 150);
    s.volume = p.getUChar("volume", 50); if (s.volume > 100) s.volume = 50;
    s.zoom = p.getUChar("zoom", 2); if (s.zoom > 6) s.zoom = 2;
    s.sound = p.getBool("sound", true); s.vibration = p.getBool("vibration", true);
    s.radar_mode = radarModeFromStorage(p.getUChar("radar_mode", 255), p.getBool("demo", true));
    s.heading_up = p.getBool("heading", true);
    s.mount_offset = p.getFloat("mount", 0); if (!isfinite(s.mount_offset) || s.mount_offset < -180 || s.mount_offset > 180) s.mount_offset = 0;
    s.declination = p.getFloat("decl", 0); if (!isfinite(s.declination) || s.declination < -180 || s.declination > 180) s.declination = 0;
  }
  p.end();
}
OsSettings &settingsGet() { return s; }
void settingsChanged() { dirty = true; changed = millis(); }
void settingsUpdate() {
  if (!dirty || millis() - changed < 1500) return;
  Preferences p; if (!p.begin("radar_os", false)) return;
  p.putUInt("schema", 1); p.putUChar("brightness", s.brightness); p.putUChar("volume", s.volume);
  p.putUChar("zoom", s.zoom); p.putBool("sound", s.sound); p.putBool("vibration", s.vibration);
  p.putUChar("radar_mode", uint8_t(s.radar_mode));
  p.putBool("demo", s.radar_mode == RadarMode::Demo); p.putBool("heading", s.heading_up); p.putFloat("decl", s.declination); p.putFloat("mount", s.mount_offset);
  p.end(); dirty = false;
}
