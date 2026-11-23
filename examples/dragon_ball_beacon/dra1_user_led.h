#pragma once
#include "secure_radio.h"

class Dra1UserLed {
 public:
  bool update(SecureLinkStatus link, uint32_t now) {
    const bool previous = lit_;
    if (link == SecureLinkStatus::Paired || link == SecureLinkStatus::Lost) hadSession_ = true;
    const Mode next = modeFor(link);
    if (next != mode_) {
      mode_ = next;
      edge_ = now;
      lit_ = next != Mode::Off;
    } else if (mode_ == Mode::Slow || mode_ == Mode::Fast) {
      const uint32_t interval = mode_ == Mode::Slow ? 500 : 125;
      const uint32_t elapsed = now - edge_;
      if (elapsed >= interval) {
        const uint32_t totalEdges = elapsed / interval;
        edge_ += totalEdges * interval;
        if (totalEdges & 1) lit_ = !lit_;
      }
    }
    return lit_ != previous;
  }

  bool lit() const { return lit_; }

 private:
  enum class Mode : uint8_t { Off, Slow, Solid, Fast };
  Mode modeFor(SecureLinkStatus link) const {
    switch (link) {
      case SecureLinkStatus::Unavailable: return Mode::Off;
      case SecureLinkStatus::Paired: return Mode::Solid;
      case SecureLinkStatus::Lost: return Mode::Fast;
      case SecureLinkStatus::ChannelMismatch: return hadSession_ ? Mode::Fast : Mode::Slow;
      case SecureLinkStatus::AwaitingHandshake: return Mode::Slow;
    }
    return Mode::Off;
  }

  uint32_t edge_ = 0;
  Mode mode_ = Mode::Off;
  bool lit_ = false;
  bool hadSession_ = false;
};
