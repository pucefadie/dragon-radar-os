#include "proximity_engine.h"
#include <cassert>
#include <cmath>

using State = ProximityState;
using Quality = SignalQuality;

int main() {
  ProximityEngine engine;
  uint32_t now = 100;
  const auto sample = [&](int8_t rssi) {
    engine.sample(rssi, now, now);
    now += 3000;
    return engine.snapshot(now - 3000);
  };

  assert(engine.snapshot(now).state == State::Searching && engine.snapshot(now).level == 0);
  const auto weak = sample(-90);
  assert(weak.fresh && weak.raw == -90 && weak.filtered == -90 && weak.quality == Quality::Weak);
  assert(weak.state == State::Weak);
  assert(engine.snapshot(now).age_ms == 3000);
  engine.sample(0, now, now);
  engine.sample(INT8_MIN, now, now);
  assert(engine.snapshot(now).raw == -90 && engine.snapshot(now).age_ms == 3000);

  bool approaching = false;
  for (int rssi = -87; rssi <= -45; rssi += 3)
    approaching |= sample(int8_t(rssi)).state == State::Approaching;
  assert(approaching && engine.snapshot(now).quality == Quality::Strong);
  for (unsigned i=0; i<28; ++i) sample(-45);
  assert(engine.snapshot(now).state == State::Near);
  assert(engine.confirmFound(now) && engine.snapshot(now).state == State::Found);
  assert(engine.snapshot(now + 30000).state == State::Found);

  engine.reset();
  assert(!engine.snapshot(now).seen && !engine.confirmFound(now));
  sample(-48);
  bool movingAway = false;
  for (int rssi = -51; rssi >= -90; rssi -= 3)
    movingAway |= sample(int8_t(rssi)).state == State::MovingAway;
  assert(movingAway);
  const auto stale = engine.snapshot(now + 7000);
  assert(stale.seen && !stale.fresh && stale.age_ms == 10000 && stale.state == State::Lost);
  assert(stale.quality == Quality::Unavailable && stale.level == 0 && !engine.confirmFound(now + 7000));
  now += 10000;
  assert(sample(-58).filtered == -58);  // First sample after a gap starts a new filter.
  engine.invalidate();                // Channel loss or a new authenticated session.
  assert(!engine.snapshot(now).fresh && engine.snapshot(now).level == 0 && !engine.confirmFound(now));
  assert(sample(-93).filtered == -93);
  assert(engine.confirmFound(now));
  engine.setSuspended(true, now);     // Standby discards both history and manual confirmation.
  engine.sample(-50, now + 100, now + 100);
  assert(!engine.snapshot(now + 100).seen);
  now += 200;
  engine.setSuspended(false, now);
  engine.sample(-50, now - 1, now);  // An ACK queued before wake cannot seed the display.
  assert(!engine.snapshot(now).seen);
  assert(sample(-67).filtered == -67);

  ProximityEngine oscillating;
  Quality lastQuality = Quality::Unavailable;
  for (uint32_t t=0; t<60000; t+=3000) {
    oscillating.sample(t % 6000 ? -59 : -61, t, t);
    const auto s = oscillating.snapshot(t);
    assert(s.state != State::Approaching && s.state != State::MovingAway);
    if (t >= 30000) assert(s.quality == lastQuality);
    lastQuality = s.quality;
  }
  const auto steady = oscillating.snapshot(57000);
  assert(std::fabs(steady.filtered + 60) < 1);
  ProximityEngine near;
  for (uint32_t t=0; t<60000; t+=3000)
    near.sample(t % 6000 ? -62 : -59, t, t);
  assert(near.snapshot(57000).quality == Quality::Strong);
  assert(near.snapshot(57000).state == State::Near);

  const struct { int8_t rssi; uint8_t level; } scale[] = {
    {-110, 1}, {-90, 1}, {-85, 1}, {-65, 4}, {-60, 5}, {-45, 8},
    {-30, 11}, {-24, 13}, {-19, 14}, {-15, 15}, {-10, 15}
  };
  for (const auto &point : scale) {
    ProximityEngine steadySignal;
    uint32_t t = 0;
    for (; t < 30000; t += 3000) {
      steadySignal.sample(point.rssi, t, t);
      const auto measured = steadySignal.snapshot(t);
      assert(measured.fresh && measured.level == point.level);
      assert(measured.state != State::Found);
      if (point.rssi >= -60) assert(measured.quality == Quality::Strong);
    }
    assert(steadySignal.snapshot(t + 7000).level == 0);
  }

  ProximityEngine closeSignal;
  uint32_t t = 0;
  const auto hold = [&](int8_t rssi) {
    for (unsigned i = 0; i < 12; ++i, t += 3000) closeSignal.sample(rssi, t, t);
    return closeSignal.snapshot(t);
  };
  assert(hold(-26).level == 12);
  assert(hold(-24).level == 13);
  // Two-dB excursions that repeatedly cross the rising threshold stay in the same band.
  for (int i = 0; i < 3; ++i) {
    assert(hold(-26).level == 13);
    assert(hold(-24).level == 13);
  }
  assert(hold(-29).level == 12);
  assert(hold(-19).level == 14);
  closeSignal.invalidate();
  assert(closeSignal.snapshot(t).level == 0);
  assert(hold(-65).level == 4);  // New session starts at its own measured level.

  SphereSearch search;
  search.setAssociated(0x7f);
  assert(search.selected()==1 && search.foundCount()==0);
  assert(search.confirm(0,true)==SearchEvent::None);
  for (uint8_t id=1; id<=SphereSearch::count; ++id) search.sample(id,int8_t(-20-id),10+id,17);
  assert(search.snapshot(1,17).raw==-21 && search.snapshot(7,17).raw==-27);
  assert(search.snapshot(1,17).age_ms==6 && search.snapshot(7,17).age_ms==0);
  assert(search.confirm(17,false)==SearchEvent::None);
  search.invalidate(2);
  assert(!search.snapshot(2,17).fresh && search.snapshot(1,17).fresh);
  assert(search.confirm(17,true)==SearchEvent::SphereFound);
  assert(search.confirm(18,true)==SearchEvent::None && !search.select(1));
  assert(search.select(2) && search.confirm(18,true)==SearchEvent::None);
  search.sample(2,-22,19,19);
  assert(search.confirm(10019,true)==SearchEvent::None);
  search.sample(2,-22,10020,10020);
  assert(search.confirm(10020,true)==SearchEvent::SphereFound);
  search.setSuspended(true,10021);
  assert(search.snapshot(1,10021).state==State::Found && search.foundCount()==2);
  search.setSuspended(false,10030);
  for (uint8_t id=3; id<=SphereSearch::count; ++id) {
    assert(search.select(id));
    assert(search.confirm(10031,true)==SearchEvent::None);
    search.sample(id,-45,10032,10032);
    assert(search.confirm(10033,true)==SearchEvent::SphereFound);
  }
  assert(search.foundCount()==7 && !search.select(7));
  search.newGame();
  assert(search.foundCount()==0 && search.associated()==0x7f && search.select(1));
  assert(!search.snapshot(1,10034).fresh && search.confirm(10034,true)==SearchEvent::None);

  // Delayed ACKs must not rewind the filter or artificially refresh its history.
  ProximityEngine reordered;
  reordered.sample(-65, 1000, 1000);
  reordered.sample(-55, 4000, 4000);
  const auto newest = reordered.snapshot(4100);
  reordered.sample(-100, 3000, 4100);
  const auto afterOld = reordered.snapshot(4100);
  assert(afterOld.raw == newest.raw && afterOld.filtered == newest.filtered);
  assert(afterOld.age_ms == newest.age_ms && afterOld.level == newest.level);
  reordered.sample(-100, 4000, 4200);
  assert(reordered.snapshot(4200).raw == newest.raw);
  assert(reordered.snapshot(14000).state == State::Lost);

  // Interleave seven peers; delayed samples for one peer cannot disturb others.
  SphereSearch interleaved;
  interleaved.setAssociated(0x7f);
  ProximityEngine ordered[SphereSearch::count];
  for (uint32_t t=1000; t<=61000; t+=3000) {
    for (uint8_t id=1; id<=SphereSearch::count; ++id) {
      const auto signal = int8_t(-90 + id*5 + int((t/3000)%5));
      const uint32_t rx = t + id;
      ordered[id-1].sample(signal, rx, t+10);
      interleaved.sample(id, signal, rx, t+10);
      interleaved.sample(id, -110, rx-100, t+10);
      interleaved.sample(id, -10, rx, t+10);
    }
    for (uint8_t id=1; id<=SphereSearch::count; ++id) {
      const auto expected = ordered[id-1].snapshot(t+10);
      const auto actual = interleaved.snapshot(id, t+10);
      assert(actual.raw == expected.raw && actual.filtered == expected.filtered);
      assert(actual.age_ms == expected.age_ms && actual.level == expected.level);
      assert(actual.state == expected.state && actual.fresh == expected.fresh);
    }
  }

  ProximityEngine wrap;
  wrap.sample(-74, UINT32_MAX - 1000, UINT32_MAX - 1000);
  assert(wrap.snapshot(999).age_ms == 2000 && wrap.snapshot(999).fresh);
  wrap.sample(-68, 999, 999);
  wrap.sample(-100, UINT32_MAX - 500, 1000);
  assert(wrap.snapshot(1000).raw == -68 && wrap.snapshot(1000).age_ms == 1);
  wrap.sample(-60, 12000, 12000);
  assert(wrap.snapshot(12000).filtered == -60);
}
