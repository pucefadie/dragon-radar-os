#pragma once
#include "radar_engine.h"
class LocationProvider {
 public:
  virtual ~LocationProvider() = default;
  virtual bool position(RadarPosition &out) const = 0;
  virtual bool ball(unsigned index, RadarBall &out) const = 0;
};
// Real radio providers deliberately report unavailable until their hardware exists.
class EspNowProvider : public LocationProvider {
 public:
  bool position(RadarPosition &) const override { return false; }
  bool ball(unsigned, RadarBall &) const override { return false; }
};
class UwbProvider : public LocationProvider {
 public:
  bool position(RadarPosition &) const override { return false; }
  bool ball(unsigned, RadarBall &) const override { return false; }
};
class DemoProvider : public LocationProvider {
 public:
  bool position(RadarPosition &out) const override;
  bool ball(unsigned index, RadarBall &out) const override;
  void approach();
 private:
  unsigned steps = 0;
};
