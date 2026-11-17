#include "dev_radio.h"
#include <cassert>
#include <cstring>
#include <deque>
struct Msg { bool toSphere; uint8_t bytes[RADIO_FRAME_SIZE]; };
struct Bus;
struct Adapter : DevRadioTransport { Bus &bus; bool sphere; Adapter(Bus &b,bool s):bus(b),sphere(s){} bool send(const uint8_t*,const uint8_t*) override; };
struct Bus {std::deque<Msg> q; bool busy=false;};
bool Adapter::send(const uint8_t*,const uint8_t *p){if(bus.busy)return false;Msg m{};m.toSphere=!sphere;memcpy(m.bytes,p,RADIO_FRAME_SIZE);bus.q.push_back(m);return true;}
int main(){
 Bus bus;Adapter s3(bus,false),c6(bus,true);DevRadar radar(s3,42);DevSphere sphere(c6,1,77);uint8_t smac[6]={1},rmac[6]={2};uint32_t now=0xfffffff0u;
 auto pump=[&](){unsigned limit=20;while(!bus.q.empty()&&limit--){Msg m=bus.q.front();bus.q.pop_front();if(m.toSphere)sphere.receive(rmac,m.bytes,sizeof(m.bytes),now);else radar.receive(smac,m.bytes,sizeof(m.bytes),now);radar.tick(now);}assert(limit);};
 radar.beacon(smac,1,77,now);radar.tick(now);pump();assert(radar.linked()&&sphere.online()&&!radar.pending());
 now+=3001;radar.beacon(smac,1,77,now);radar.tick(now);assert(!bus.q.empty());pump();assert(radar.linked());
 // A busy transport must not consume the retry budget.
 now+=3001;radar.beacon(smac,1,77,now);bus.busy=true;for(int i=0;i<10;i++){radar.tick(now);now+=100;}assert(radar.pending());bus.busy=false;radar.tick(now);assert(!bus.q.empty());
 // Lose the first response; retry the same request and deliver the duplicate.
 Msg request=bus.q.front();bus.q.pop_front();sphere.receive(rmac,request.bytes,sizeof(request.bytes),now);assert(!bus.q.empty());Msg response=bus.q.front();bus.q.clear();uint8_t other[6]={9};radar.receive(other,response.bytes,sizeof(response.bytes),now);assert(radar.pending());
 RadioFrame bad{};assert(radioDecode(response.bytes,sizeof(response.bytes),bad));uint8_t altered[RADIO_FRAME_SIZE];
 bad.id=2;radioEncode(bad,altered);radar.receive(smac,altered,sizeof(altered),now);assert(radar.pending());bad.id=1;bad.session=88;radioEncode(bad,altered);radar.receive(smac,altered,sizeof(altered),now);assert(radar.pending());bad.session=77;bad.ack_sequence++;radioEncode(bad,altered);radar.receive(smac,altered,sizeof(altered),now);assert(radar.pending());
 now+=500;radar.tick(now);assert(!bus.q.empty());pump();assert(!radar.pending());radar.receive(smac,response.bytes,sizeof(response.bytes),now);assert(!radar.pending());
 now+=10001;sphere.tick(now);assert(!sphere.online());radar.tick(now);assert(!radar.linked());radar.beacon(smac,1,77,now);radar.tick(now);pump();assert(radar.linked()&&sphere.online());
 // Reboot changes the sphere session and restarts discovery.
 DevSphere rebooted(c6,1,78);radar.beacon(smac,1,78,now);radar.tick(now);unsigned limit=20;while(!bus.q.empty()&&limit--){Msg m=bus.q.front();bus.q.pop_front();if(m.toSphere)rebooted.receive(rmac,m.bytes,sizeof(m.bytes),now);else radar.receive(smac,m.bytes,sizeof(m.bytes),now);radar.tick(now);}assert(limit&&radar.linked()&&rebooted.online());
 // No responses: eventually timeout, followed by rediscovery on a later beacon.
 now+=3001;radar.beacon(smac,1,78,now);for(int i=0;i<4;i++){radar.tick(now);bus.q.clear();now+=500;}assert(!radar.pending()&&!radar.linked());
 RadioFrame unauthorized{};unauthorized.type=RadioMessage::Command;unauthorized.id=0;unauthorized.destination=1;unauthorized.flags=RADIO_FLAG_DIRECTED;unauthorized.payload_length=1;unauthorized.payload[0]=1;uint8_t forbidden[RADIO_FRAME_SIZE];radioEncode(unauthorized,forbidden);rebooted.receive(rmac,forbidden,sizeof(forbidden),now);assert(bus.q.empty());
 uint8_t malformed[RADIO_FRAME_SIZE]={};rebooted.receive(rmac,malformed,sizeof(malformed),now);assert(rebooted.online());
}
