#pragma once
#include "beacon_protocol.h"
struct BeaconEntry { bool seen=false; uint8_t mac[6]={}; BeaconFrame frame={}; int8_t rssi=0; uint32_t received=0; uint32_t packets=0; };
enum class BeaconAccept { Accepted, Conflict, Duplicate, Invalid };
class BeaconRegistry {
 public:
  BeaconAccept accept(const BeaconFrame &frame, const uint8_t mac[6], int8_t rssi, uint32_t now);
  const BeaconEntry *entry(unsigned index) const;
  static bool fresh(const BeaconEntry &entry,uint32_t now);
 private:
  BeaconEntry entries[7];
};
