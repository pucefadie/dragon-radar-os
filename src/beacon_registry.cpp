#include "beacon_registry.h"
#include <string.h>
bool BeaconRegistry::fresh(const BeaconEntry &e,uint32_t now) { return e.seen && uint32_t(now-e.received)<BEACON_STALE_MS; }
const BeaconEntry *BeaconRegistry::entry(unsigned index) const { return index<7 ? &entries[index] : nullptr; }
BeaconAccept BeaconRegistry::accept(const BeaconFrame &f,const uint8_t mac[6],int8_t rssi,uint32_t now) {
  if (!mac || f.id<1 || f.id>7) return BeaconAccept::Invalid;
  BeaconEntry &e=entries[f.id-1]; const bool active=fresh(e,now);
  if (active && memcmp(e.mac,mac,6)!=0) return BeaconAccept::Conflict;
  if (active && f.boot_id==e.frame.boot_id && !beaconSequenceNewer(f.sequence,e.frame.sequence)) return BeaconAccept::Duplicate;
  if (!active) e.packets=0;
  e.seen=true; memcpy(e.mac,mac,6); e.frame=f; e.rssi=rssi; e.received=now; ++e.packets;
  return BeaconAccept::Accepted;
}
