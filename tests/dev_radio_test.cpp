#include "dev_radio.h"
#include <cassert>
#include <cstring>
#include <deque>
struct Msg { bool toSphere; uint8_t bytes[RADIO_FRAME_SIZE]; };
struct Bus;
struct Adapter : DevRadioTransport { Bus &bus; bool sphere; bool busyTimeout=false; Adapter(Bus &b,bool s):bus(b),sphere(s){} bool send(const uint8_t*,const uint8_t*) override; void event(DevEvent e,uint8_t,uint32_t,uint32_t) override {if(e==DevEvent::TxBusyTimeout)busyTimeout=true;} };
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
 bad.id=2;radioEncode(bad,altered);radar.receive(smac,altered,sizeof(altered),now);assert(radar.pending());bad.id=1;bad.session=88;radioEncode(bad,altered);radar.receive(smac,altered,sizeof(altered),now);assert(radar.pending());bad.session=77;bad.destination=2;radioEncode(bad,altered);radar.receive(smac,altered,sizeof(altered),now);assert(radar.pending());bad.destination=0;bad.ack_sequence++;radioEncode(bad,altered);radar.receive(smac,altered,sizeof(altered),now);assert(radar.pending());
 now+=500;radar.tick(now);assert(!bus.q.empty());pump();assert(!radar.pending());radar.receive(smac,response.bytes,sizeof(response.bytes),now);assert(!radar.pending());
 now+=10001;sphere.tick(now);assert(!sphere.online());radar.tick(now);assert(!radar.linked());radar.beacon(smac,1,77,now);radar.tick(now);pump();assert(radar.linked()&&sphere.online());
 // Reboot changes the sphere session and restarts discovery.
 DevSphere rebooted(c6,1,78);radar.beacon(smac,1,78,now);radar.tick(now);unsigned limit=20;while(!bus.q.empty()&&limit--){Msg m=bus.q.front();bus.q.pop_front();if(m.toSphere)rebooted.receive(rmac,m.bytes,sizeof(m.bytes),now);else radar.receive(smac,m.bytes,sizeof(m.bytes),now);radar.tick(now);}assert(limit&&radar.linked()&&rebooted.online());
 // No responses: eventually timeout, followed by rediscovery on a later beacon.
 now+=3001;radar.beacon(smac,1,78,now);for(int i=0;i<4;i++){radar.tick(now);bus.q.clear();now+=500;}assert(!radar.pending()&&!radar.linked());
 RadioFrame unauthorized{};unauthorized.type=RadioMessage::Command;unauthorized.id=0;unauthorized.destination=1;unauthorized.flags=RADIO_FLAG_DIRECTED;unauthorized.payload_length=1;unauthorized.payload[0]=1;uint8_t forbidden[RADIO_FRAME_SIZE];radioEncode(unauthorized,forbidden);rebooted.receive(rmac,forbidden,sizeof(forbidden),now);assert(bus.q.empty());
 // PONG from an unexpected MAC or with a different session cannot complete PING.
 Bus pongbus;Adapter pa(pongbus,false),pb(pongbus,true);DevRadar pr(pa,8);DevSphere ps(pb,1,11);pr.beacon(smac,1,11,0);pr.tick(0);Msg d=pongbus.q.front();pongbus.q.pop_front();ps.receive(rmac,d.bytes,sizeof(d.bytes),0);Msg status=pongbus.q.front();pongbus.q.pop_front();pr.receive(smac,status.bytes,sizeof(status.bytes),0);pr.tick(0);Msg ping=pongbus.q.front();pongbus.q.pop_front();ps.receive(rmac,ping.bytes,sizeof(ping.bytes),0);Msg pong=pongbus.q.front();pongbus.q.pop_front();pr.receive(other,pong.bytes,sizeof(pong.bytes),0);assert(pr.pending());RadioFrame pf{};assert(radioDecode(pong.bytes,sizeof(pong.bytes),pf));pf.session=12;uint8_t forged[RADIO_FRAME_SIZE];radioEncode(pf,forged);pr.receive(smac,forged,sizeof(forged),0);assert(pr.pending());pr.receive(smac,pong.bytes,sizeof(pong.bytes),0);assert(pr.pending());
 // Drop a diagnostic COMMAND_ACK and verify the duplicate command is acknowledged again.
 Bus ackbus;Adapter aa(ackbus,false),ab(ackbus,true);DevRadar ar(aa,5);DevSphere as(ab,1,10);ar.beacon(smac,1,10,0);ar.tick(0);bool dropped=false;unsigned steps=30;while(!ackbus.q.empty()&&steps--){Msg m=ackbus.q.front();ackbus.q.pop_front();RadioFrame f{};assert(radioDecode(m.bytes,sizeof(m.bytes),f));if(!m.toSphere&&f.type==RadioMessage::CommandAck&&!dropped){dropped=true;continue;}if(m.toSphere)as.receive(rmac,m.bytes,sizeof(m.bytes),0);else ar.receive(smac,m.bytes,sizeof(m.bytes),0);ar.tick(0);}assert(dropped&&ar.pending());ar.tick(500);while(!ackbus.q.empty()&&steps--){Msg m=ackbus.q.front();ackbus.q.pop_front();if(m.toSphere)as.receive(rmac,m.bytes,sizeof(m.bytes),500);else ar.receive(smac,m.bytes,sizeof(m.bytes),500);ar.tick(500);}assert(steps&&!ar.pending()&&ar.linked());
 // A delayed response beyond the application window must not clear a pending request.
 Bus delayed;Adapter da(delayed,false),db(delayed,true);DevRadar dr(da,4);DevSphere ds(db,1,9);dr.beacon(smac,1,9,0);dr.tick(0);Msg rq=delayed.q.front();delayed.q.pop_front();ds.receive(rmac,rq.bytes,sizeof(rq.bytes),0);Msg late=delayed.q.front();delayed.q.clear();dr.receive(smac,late.bytes,sizeof(late.bytes),1500);assert(dr.pending()&&!dr.linked());
 Bus stuck;Adapter stuckRadar(stuck,false),stuckSphere(stuck,true);DevRadar sr(stuckRadar,1);DevSphere ss(stuckSphere,1,2);uint32_t clock=0xfffffff0u;stuck.busy=true;sr.beacon(smac,1,2,clock);for(int i=0;i<42;i++){sr.tick(clock);clock+=100;}assert(!sr.pending()&&!sr.linked()&&stuckRadar.busyTimeout&&sr.outcome()==DevOutcome::TxBusyTimeout);stuck.busy=false;clock+=2000;sr.beacon(smac,1,2,clock);sr.tick(clock);assert(!stuck.q.empty());unsigned budget=20;while(!stuck.q.empty()&&budget--){Msg m=stuck.q.front();stuck.q.pop_front();if(m.toSphere)ss.receive(rmac,m.bytes,sizeof(m.bytes),clock);else sr.receive(smac,m.bytes,sizeof(m.bytes),clock);sr.tick(clock);}assert(budget&&sr.linked());
 uint8_t malformed[RADIO_FRAME_SIZE]={};rebooted.receive(rmac,malformed,sizeof(malformed),now);assert(rebooted.online());
}
