#include "secure_radio.h"
#include <array>
#include <cassert>
#include <cstring>
#include <deque>
#include <openssl/evp.h>
#include <openssl/hmac.h>

using Frame = std::array<uint8_t,SECURE_RADIO_SIZE>;
struct Wire : SecureRadioIo {
  std::deque<Frame> queue;
  uint8_t counter;
  explicit Wire(uint8_t seed):counter(seed) {}
  bool sign(const uint8_t *key,const uint8_t *bytes,size_t length,uint8_t *tag) override {
    unsigned size=0;
    return HMAC(EVP_sha256(),key,32,bytes,length,tag,&size) && size==32;
  }
  void random(uint8_t *b,size_t n) override {
    memset(b,0,n);
    b[0]=++counter;
  }
  bool send(const uint8_t *,const uint8_t *b,size_t n) override {
    assert(n==SECURE_RADIO_SIZE);
    Frame f; memcpy(f.data(),b,n); queue.push_back(f);
    return true;
  }
  Frame take() { assert(!queue.empty()); Frame frame=queue.front(); queue.pop_front(); return frame; }
};

static SecureRadioConfig make(uint8_t peer) {
  uint8_t record[SECURE_CONFIG_SIZE]{'D','R','C','1',1,6,peer};
  record[12]=5; record[28]=8; record[60]=9;
  SecureRadioConfig cfg{};
  assert(!secureRadioDecodeConfig(record,sizeof(record)-1,cfg));
  record[4]=2;
  assert(!secureRadioDecodeConfig(record,sizeof(record),cfg));
  record[4]=1;
  record[5]=0;
  assert(!secureRadioDecodeConfig(record,sizeof(record),cfg));
  record[5]=6;
  assert(secureRadioDecodeConfig(record,sizeof(record),cfg));
  return cfg;
}

int main() {
  const uint8_t radarMac[6]{2}, sphereMac[6]{4}, wrongMac[6]{6};
  Wire radarWire(20), sphereWire(50);
  auto rc=make(4), sc=make(2);
  SecureRadio radar(radarWire,true,rc), sphere(sphereWire,false,sc);
  auto toSphere=[&](const Frame &f,uint32_t t){sphere.receive(radarMac,f.data(),f.size(),t);};
  auto toRadar=[&](const Frame &f,uint32_t t){radar.receive(sphereMac,f.data(),f.size(),t);};
  assert(radar.state(0)==LinkState::AwaitingAuthenticatedSession);
  radar.tick(0);
  Frame hello=radarWire.take(), forged=hello;
  forged[79]^=1;
  toSphere(forged,0);
  sphere.receive(wrongMac,hello.data(),hello.size(),0);
  assert(sphereWire.queue.empty());
  assert(sphere.diagnostics().auth_rejected==1);
  toSphere(hello,1);
  Frame challenge=sphereWire.take();
  toRadar(challenge,2);
  assert(radar.state(2)==LinkState::AwaitingAuthenticatedSession);
  radar.tick(2);
  Frame finish=radarWire.take();
  toSphere(finish,3);
  Frame ready=sphereWire.take();
  assert(sphere.state(3)==LinkState::Paired);
  assert(radar.state(3)==LinkState::AwaitingAuthenticatedSession);
  radar.tick(452); // retry FINISH if READY was lost
  assert(radarWire.take()==finish);
  toSphere(finish,453);
  assert(sphereWire.take()==ready);
  toRadar(ready,454);
  assert(radar.state(454)==LinkState::Paired);
  radar.tick(454);
  Frame probe=radarWire.take();
  toSphere(probe,455);
  Frame ack=sphereWire.take();
  assert(sphere.confirmations()==1);
  assert(radar.diagnostics().probe_retry_queued==0);
  assert(sphere.diagnostics().duplicate_probe==0);
  radar.tick(904);
  assert(radarWire.take()==probe);
  toSphere(probe,905);
  assert(sphereWire.take()==ack && sphere.confirmations()==1);
  assert(radar.diagnostics().probe_retry_queued==1);
  assert(sphere.diagnostics().duplicate_probe==1);
  forged=ack; forged[12]^=1;
  toRadar(forged,906);
  assert(radar.confirmations()==0);
  assert(radar.diagnostics().auth_rejected==0); // Malformed ACK is rejected before HMAC.
  forged=ack; forged[79]^=1;
  toRadar(forged,906);
  radar.receive(wrongMac,ack.data(),ack.size(),906);
  assert(radar.confirmations()==0);
  assert(radar.diagnostics().auth_rejected==1);
  toRadar(ack,907);
  toRadar(ack,908);
  assert(radar.confirmations()==1);

  Wire rebootWire(90);
  SecureRadio rebooted(rebootWire,false,sc);
  rebooted.receive(radarMac,hello.data(),hello.size(),1000);
  assert(rebootWire.take()!=challenge);
  rebooted.receive(radarMac,finish.data(),finish.size(),1001);
  rebooted.receive(radarMac,probe.data(),probe.size(),1002);
  assert(rebooted.state(1002)==LinkState::AwaitingAuthenticatedSession && rebootWire.queue.empty());
  assert(radar.state(16000)==LinkState::Lost);
  radar.tick(16000);
  Frame nextHello=radarWire.take();
  toRadar(challenge,16001);
  toRadar(ready,16002);
  assert(radar.state(16002)==LinkState::Lost);
  rebooted.receive(radarMac,nextHello.data(),nextHello.size(),16003);
  toRadar(rebootWire.take(),16004);
  radar.tick(16004);
  Frame nextFinish=radarWire.take();
  rebooted.receive(radarMac,nextFinish.data(),nextFinish.size(),16005);
  toRadar(rebootWire.take(),16006);
  assert(radar.state(16006)==LinkState::Paired);
  toRadar(ack,16006);
  assert(radar.confirmations()==1);
  assert(radar.diagnostics().stale_session==1);
  rebooted.receive(radarMac,probe.data(),probe.size(),16007);
  assert(rebooted.diagnostics().stale_session==1);
  assert(rebooted.confirmations()==0 && rebootWire.queue.empty());

  // Restart the radar while the sphere is still paired, then exchange a new probe.
  Wire restartedRadarWire(110);
  SecureRadio restartedRadar(restartedRadarWire,true,rc);
  restartedRadar.tick(16010);
  Frame restartHello=restartedRadarWire.take();
  rebooted.receive(radarMac,restartHello.data(),restartHello.size(),16011);
  Frame restartChallenge=rebootWire.take();
  restartedRadar.receive(sphereMac,restartChallenge.data(),restartChallenge.size(),16012);
  restartedRadar.tick(16012);
  Frame restartFinish=restartedRadarWire.take();
  rebooted.receive(radarMac,restartFinish.data(),restartFinish.size(),16013);
  Frame restartReady=rebootWire.take();
  restartedRadar.receive(sphereMac,restartReady.data(),restartReady.size(),16014);
  assert(restartedRadar.state(16014)==LinkState::Paired);
  restartedRadar.tick(16014);
  Frame restartProbe=restartedRadarWire.take();
  rebooted.receive(radarMac,restartProbe.data(),restartProbe.size(),16015);
  Frame restartAck=rebootWire.take();
  restartedRadar.receive(sphereMac,restartAck.data(),restartAck.size(),16016);
  assert(rebooted.confirmations()==1 && restartedRadar.confirmations()==1);

  // Repeating the last probe may recover a lost ACK but cannot extend freshness.
  rebooted.receive(radarMac,restartProbe.data(),restartProbe.size(),31000);
  assert(rebootWire.take()==restartAck && rebooted.confirmations()==1);
  assert(rebooted.state(31016)==LinkState::Lost);
}
