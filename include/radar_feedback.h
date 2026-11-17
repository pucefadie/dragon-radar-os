#pragma once
#include <stdint.h>
// Zones 1..6 begin at the corresponding distance; a 10% exit margin
// prevents rapid switching when a measurement fluctuates at a boundary.
inline uint8_t radarProximityZone(uint8_t zone, float metres) {
  static const float thresholds[] = {100.0f, 50.0f, 25.0f, 12.5f, 6.5f, 3.0f};
  if (zone > 6) zone = 0;
  while (zone < 6 && metres <= thresholds[zone]) ++zone;
  while (zone > 0 && metres > thresholds[zone-1] * 1.10f) --zone;
  return zone;
}
inline uint32_t radarProximityIntervalMs(uint8_t zone) {
  static const uint32_t periods[] = {0, 6000, 4000, 2500, 1600, 1000, 0};
  return zone <= 6 ? periods[zone] : 0;
}
