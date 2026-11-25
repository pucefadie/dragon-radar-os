#pragma once
#include <stdint.h>

namespace sphereRgb {
constexpr struct ChainConfig { uint8_t dataPin, pixelCount; bool enabled; } kChain{23,2,true};
struct Color { uint8_t r,g,b; };
constexpr Color palette[]={{255,0,0},{255,100,0},{255,170,0},{0,255,0},{0,0,255},{150,0,255},{255,255,255}};
enum class Effect : uint8_t { Quiet, Wait, Green, Found, Manual, White };

class Animation {
 public:
  void begin(uint32_t now,bool white) {
    boot_=now;
    effect_=white ? Effect::White : Effect::Wait;
    since_=now;
  }
  void paired(uint32_t now) {
    if (paired_) return;
    paired_=true;
    if (effect_==Effect::White) return;
    if (effect_==Effect::Wait || effect_==Effect::Quiet) set(Effect::Green,now);
  }
  void manual(bool on,uint8_t color,uint8_t brightness,uint32_t now) {
    brightness_=brightness;
    color_=color;
    set(on?Effect::Manual:Effect::Quiet,now);
  }
  uint8_t brightness() const { return brightness_; }
  void setBrightness(uint8_t value) { brightness_=value<=100 ? value : 50; }
  void found(uint32_t now) { set(Effect::Found,now); }
  Color update(uint32_t now) {
    const uint32_t age=now-since_;
    switch (effect_) {
      case Effect::White:
        if (now-boot_<500) return palette[6];
        set(paired_?Effect::Green:Effect::Wait,now);
        return {};
      case Effect::Wait:
        if (age<8000) return scale(palette[0],uint8_t(brightness_*wave(age,1600)/100));
        break;
      case Effect::Green:
        if (age<500) return scale(palette[3],brightness_);
        break;
      case Effect::Found:
        if (age<3000) return scale(palette[2],uint8_t(brightness_*wave(age,750)/100));
        break;
      case Effect::Manual:
        if (age<10000) return scale(palette[color_],brightness_);
        break;
      case Effect::Quiet: return {};
    }
    set(Effect::Quiet,now);
    return {};
  }
 private:
  static uint8_t wave(uint32_t t,uint32_t period) {
    const uint32_t half=period/2, phase=t%period;
    return 20+80*(phase<half ? phase : period-phase)/half;
  }
  static Color scale(Color color,uint8_t b) {
    return {uint8_t(color.r*b/100),uint8_t(color.g*b/100),uint8_t(color.b*b/100)};
  }
  void set(Effect effect,uint32_t now) { effect_=effect; since_=now; }
  Effect effect_=Effect::Quiet;
  uint32_t boot_=0,since_=0;
  uint8_t brightness_=50,color_=0;
  bool paired_=false;
};
}
