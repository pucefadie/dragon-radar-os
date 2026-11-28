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

// Relative proximity bands, not physical distance. No queued events.
enum class DirectionHaptic : uint8_t { None, Approach, Retreat };
class DirectionHapticGate {
 public:
  static constexpr uint32_t cooldownMs = 1500;
  void reset() { seeded_ = false; }
  DirectionHaptic update(bool valid, uint8_t id, uint32_t epoch, uint8_t level,
                         uint32_t now, bool busy) {
    if (!valid || id < 1 || id > 7 || level < 1 || level > 15) {
      reset();
      return DirectionHaptic::None;
    }
    const uint8_t band = uint8_t(10u * level / 15u);
    if (!seeded_ || id != id_ || epoch != epoch_) {
      seeded_ = true; id_ = id; epoch_ = epoch; band_ = band;
      return DirectionHaptic::None;
    }
    if (band == band_) return DirectionHaptic::None;
    const auto event = band > band_ ? DirectionHaptic::Approach : DirectionHaptic::Retreat;
    band_ = band; // Suppressed events are dropped, not replayed later.
    if (busy || (emitted_ && uint32_t(now - last_) < cooldownMs)) return DirectionHaptic::None;
    emitted_ = true; last_ = now;
    return event;
  }
 private:
  bool seeded_ = false, emitted_ = false;
  uint8_t id_ = 0, band_ = 0;
  uint32_t epoch_ = 0, last_ = 0;
};
