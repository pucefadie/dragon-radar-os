#include "beacon_registry.h"
#include <cassert>
int main() {
  BeaconRegistry r; uint8_t a[6]={1}, b[6]={2}; BeaconFrame f={1,false,42,1,0};
  assert(r.accept(f,a,-60,100)==BeaconAccept::Accepted);
  assert(r.entry(0)->rssi==-60 && BeaconRegistry::fresh(*r.entry(0),100));
  assert(r.accept(f,a,-50,200)==BeaconAccept::Duplicate);
  f.sequence=2; assert(r.accept(f,b,-55,300)==BeaconAccept::Conflict);
  assert(r.accept(f,a,-55,400)==BeaconAccept::Accepted);
  f.boot_id=43; f.sequence=0; assert(r.accept(f,a,-55,500)==BeaconAccept::Accepted);
  assert(r.accept(f,b,-70,15500)==BeaconAccept::Accepted);
  assert(!BeaconRegistry::fresh(*r.entry(0),30500));
  f.id=0; assert(r.accept(f,a,-1,0)==BeaconAccept::Invalid);
  assert(!r.entry(7));
}
