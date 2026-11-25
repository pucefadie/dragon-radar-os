#include "secure_radio.h"
#include <array>
#include <cassert>
#include <cstring>
#include <deque>
#include <memory>
#include <openssl/evp.h>
#include <openssl/hmac.h>

using Frame = std::array<uint8_t,SECURE_RADIO_SIZE>;
struct Wire : SecureRadioIo {
  std::deque<Frame> queue;
  std::deque<uint8_t> targets;
  uint8_t destination=0;
  uint8_t counter;
  bool accepting = true;
  bool signing = true;
  explicit Wire(uint8_t seed):counter(seed) {}
  bool sign(const uint8_t *key,const uint8_t *bytes,size_t length,uint8_t *tag) override {
    if (!signing) return false;
    unsigned size=0;
    return HMAC(EVP_sha256(),key,32,bytes,length,tag,&size) && size==32;
  }
  void random(uint8_t *b,size_t n) override {
    memset(b,0,n);
    b[0]=++counter;
  }
  bool send(const uint8_t *mac,const uint8_t *b,size_t n) override {
    assert(n==SECURE_RADIO_SIZE);
    if (!accepting) return false;
    Frame f; memcpy(f.data(),b,n); queue.push_back(f); targets.push_back(mac[0]);
    return true;
  }
  Frame take() { assert(!queue.empty()); Frame frame=queue.front(); queue.pop_front(); destination=targets.front(); targets.pop_front(); return frame; }
};

using Record = std::array<uint8_t,SECURE_CONFIG_SIZE>;
static Record makeRecord(uint8_t peer,uint8_t version='2') {
  Record record{{'D','R','C',version,1,6,peer}};
  record[12]=5; record[28]=8; record[60]=9;
  return record;
}

static SecureRadioConfig make(uint8_t peer) {
  Record record=makeRecord(peer);
  SecureRadioConfig cfg{};
  assert(!secureRadioDecodeConfig(record.data(),record.size()-1,cfg));
  record[4]=8;
  assert(!secureRadioDecodeConfig(record.data(),record.size(),cfg));
  record[4]=1;
  record[5]=0;
  assert(!secureRadioDecodeConfig(record.data(),record.size(),cfg));
  record[5]=6;
  assert(secureRadioDecodeConfig(record.data(),record.size(),cfg));
  assert(cfg.version==SecureRecordVersion::V2);
  return cfg;
}

struct StoredRecords {
  Record v1=makeRecord(2,'1'), v2=makeRecord(4);
  SecureRecordRead v2Status=SecureRecordRead::Loaded;
  size_t v2Length=SECURE_CONFIG_SIZE;
  unsigned v1Reads=0;
};

static SecureRecordRead readRecord(void *storage,const char *key,uint8_t *data,size_t &length) {
  auto &records=*static_cast<StoredRecords *>(storage);
  const bool v2=std::strncmp(key,SECURE_RECORD_V2_KEY,7)==0;
  if (v2 && records.v2Status!=SecureRecordRead::Loaded) return records.v2Status;
  if (!v2) ++records.v1Reads;
  const auto &record=v2?records.v2:records.v1;
  length=v2?records.v2Length:record.size();
  std::memcpy(data,record.data(),length);
  return SecureRecordRead::Loaded;
}

static void testRecords() {
  Wire io(2);
  StoredRecords records;
  SecureRadioConfig config{};
  assert(secureRadioLoadConfig(readRecord,&records,config));
  assert(config.peer[0]==4 && config.version==SecureRecordVersion::V2 && records.v1Reads==0);
  io.signing=false;
  assert(secureRadioResolvePmk(config,io) && config.pmk[0]==9);
  assert(!secureRadioDecodeConfig(nullptr,SECURE_CONFIG_SIZE,config));
  for (size_t offset : {size_t(12),size_t(28),size_t(60)}) {
    Record invalid=records.v2;
    invalid[offset]=0;
    assert(!secureRadioDecodeConfig(invalid.data(),invalid.size(),config));
  }
  Record invalid=records.v2;
  invalid[60]=invalid[12];
  assert(!secureRadioDecodeConfig(invalid.data(),invalid.size(),config));
  invalid=records.v2;
  invalid[60]=invalid[28];
  assert(!secureRadioDecodeConfig(invalid.data(),invalid.size(),config));
  records.v2Length=SECURE_CONFIG_SIZE-1;
  assert(!secureRadioLoadConfig(readRecord,&records,config) && records.v1Reads==0);
  records.v2Length=SECURE_CONFIG_SIZE;
  records.v2=records.v1; // A DRC1 blob in the DRC2 slot is invalid.
  assert(!secureRadioLoadConfig(readRecord,&records,config) && records.v1Reads==0);
  records.v2Status=SecureRecordRead::Error;
  assert(!secureRadioLoadConfig(readRecord,&records,config) && records.v1Reads==0);
  records.v2Status=SecureRecordRead::Missing;
  assert(secureRadioLoadConfig(readRecord,&records,config) && records.v1Reads==1);
  assert(config.version==SecureRecordVersion::V1 && config.peer[0]==2);
  assert(!secureRadioResolvePmk(config,io) && config.pmk[0]==0);
  io.signing=true;
  assert(secureRadioResolvePmk(config,io));
  const uint8_t benchPmk[16]{0xc8,0x57,0xdb,0x8b,0x7b,0x28,0xe7,0x76,0xe5,0xe7,0x90,0x05,0xac,0xaf,0x15,0x5e};
  assert(std::memcmp(config.pmk,benchPmk,16)==0);
  records.v1[60]=10; // The legacy stored PMK was local, not a shared value.
  assert(secureRadioLoadConfig(readRecord,&records,config));
  assert(secureRadioResolvePmk(config,io) && std::memcmp(config.pmk,benchPmk,16)==0);

  records.v2Status=SecureRecordRead::Loaded;
  records.v2=makeRecord(4);
  records.v2[4]=7;
  assert(std::strcmp(secureRadioRecordKey(7),"peer_v2_7")==0);
  assert(secureRadioLoadConfig(readRecord,&records,config,7) && config.id==7);
  assert(!secureRadioLoadConfig(readRecord,&records,config,6));
  records.v2Status=SecureRecordRead::Missing;
  const auto previousReads=records.v1Reads;
  assert(!secureRadioLoadConfig(readRecord,&records,config,7) && records.v1Reads==previousReads);
  records.v1[4]=7;
  assert(!secureRadioDecodeConfig(records.v1.data(),records.v1.size(),config));

  Wire senderWire(20), receiverWire(40);
  auto wrong=make(2);
  wrong.auth[0]^=1;
  SecureRadio sender(senderWire,true,make(4)), receiver(receiverWire,false,wrong);
  sender.tick(0);
  const auto hello=senderWire.take();
  const uint8_t source[6]{2};
  receiver.receive(source,hello.data(),hello.size(),1);
  assert(receiverWire.queue.empty() && receiver.diagnostics().auth_rejected==1);
  assert(receiver.state(1)==LinkState::AwaitingAuthenticatedSession);
  assert(receiver.linkStatus(1,true)==SecureLinkStatus::AwaitingHandshake);
}

#if M1C_BENCH_FIXTURE
static void testBenchFixture() {
  const uint8_t radarMac[6]{2}, sphereMac[6]{4};
  Wire radarWire(20), sphereWire(50);
  SecureRadio radar(radarWire,true,make(4)), sphere(sphereWire,false,make(2));
  auto toSphere=[&](const Frame &frame,uint32_t now) {
    sphere.receive(radarMac,frame.data(),frame.size(),now);
  };
  auto toRadar=[&](const Frame &frame,uint32_t now) {
    radar.receive(sphereMac,frame.data(),frame.size(),now);
  };
  auto handshake=[&](uint32_t start) {
    radar.tick(start);
    toSphere(radarWire.take(),start+1);
    toRadar(sphereWire.take(),start+2);
    radar.tick(start+2);
    toSphere(radarWire.take(),start+3);
    toRadar(sphereWire.take(),start+4);
    assert(radar.state(start+4)==LinkState::Paired && sphere.state(start+4)==LinkState::Paired);
  };
  assert(!radar.benchAction(M1cRadioAction::DropAck,0));
  handshake(0);
  assert(!sphere.benchAction(M1cRadioAction::DropAck,4));
  assert(radar.benchAction(M1cRadioAction::DropAck,4));
  assert(!radar.benchAction(M1cRadioAction::CaptureProbe,4));
  radar.tick(4);
  const Frame first=radarWire.take();
  toSphere(first,5);
  Frame signedAck=sphereWire.take(), badAck=signedAck;
  badAck.back()^=1;
  toRadar(badAck,6);
  assert(radar.benchResult().ack_dropped==0 && radar.diagnostics().auth_rejected==1);
  toRadar(signedAck,7);
  assert(radar.benchResult().ack_dropped==1 && radar.confirmations()==0);
  radar.tick(454);
  assert(radarWire.take()==first);
  toSphere(first,455);
  toRadar(sphereWire.take(),456);
  assert(radar.confirmations()==1 && sphere.confirmations()==1);
  assert(radar.benchResult().ack_dropped==1 && sphere.diagnostics().duplicate_probe==1);

  assert(radar.benchAction(M1cRadioAction::CorruptProbe,3004));
  radar.tick(3004);
  Frame badProbe=radarWire.take();
  toSphere(badProbe,3005);
  assert(radar.benchResult().corrupt_queued==1 && sphere.diagnostics().auth_rejected==1);
  assert(sphere.confirmations()==1 && sphereWire.queue.empty());
  radar.tick(3454);
  const Frame goodProbe=radarWire.take();
  badProbe.back()^=1;
  assert(badProbe==goodProbe);
  toSphere(goodProbe,3455);
  toRadar(sphereWire.take(),3456);
  assert(radar.confirmations()==2 && sphere.confirmations()==2);

  assert(radar.benchAction(M1cRadioAction::CaptureProbe,6004));
  radar.tick(6004);
  const Frame saved=radarWire.take();
  assert(radar.benchResult().captured);
  assert(!radar.benchAction(M1cRadioAction::Replay,6004));
  toSphere(saved,6005);
  toRadar(sphereWire.take(),6006);
  handshake(24000);
  assert(radar.benchAction(M1cRadioAction::Replay,24004));
  const Frame replay=radarWire.take();
  assert(replay==saved && !radar.benchResult().captured && radar.benchResult().replay_queued==1);
  toSphere(replay,24005);
  assert(sphere.confirmations()==3 && sphere.diagnostics().stale_session==1 && sphereWire.queue.empty());
  assert(!radar.benchAction(M1cRadioAction::Replay,24005));
}
#endif

static unsigned motorCalls;
static SecureMotorStatus motorResponse;

static SecureMotorStatus testMotorHandler(uint32_t) {
  ++motorCalls;
  return motorResponse;
}

static void testMotorCommand() {
  const uint8_t radarMac[6]{2}, sphereMac[6]{4}, unknownMac[6]{6};
  Wire radarWire(20), sphereWire(50);
  const auto config=make(4);
  SecureRadio radar(radarWire,true,config), sphere(sphereWire,false,make(2));
  sphere.setMotorHandler(testMotorHandler);
  motorCalls=0;
  motorResponse=SecureMotorStatus::Accepted;

  const auto toSphere=[&](const Frame &frame,uint32_t now) {
    return sphere.receive(radarMac,frame.data(),frame.size(),now);
  };
  const auto toRadar=[&](const Frame &frame,uint32_t now) {
    return radar.receive(sphereMac,frame.data(),frame.size(),now);
  };
  const auto handshake=[&](uint32_t now) {
    radar.tick(now);
    toSphere(radarWire.take(),now+1);
    toRadar(sphereWire.take(),now+2);
    radar.tick(now+2);
    toSphere(radarWire.take(),now+3);
    toRadar(sphereWire.take(),now+4);
  };
  assert(!radar.requestMotor(0) && !sphere.requestMotor(0));
  handshake(0);
  assert(radar.requestMotor(10) && !radar.requestMotor(11));
  radar.tick(12);
  const auto request=radarWire.take(), probe=radarWire.take();
  auto corrupt=request;
  corrupt.back()^=1;
  toSphere(corrupt,13);
  sphere.receive(unknownMac,request.data(),request.size(),14);
  sphere.receive(radarMac,request.data(),request.size(),15,1000);
  assert(motorCalls==0 && sphereWire.queue.empty() && sphere.diagnostics().auth_rejected==1);
  auto reflected=request;
  reflected[5]=1; reflected[6]=0;
  assert(radarWire.sign(config.auth,reflected.data(),48,reflected.data()+48));
  assert(toRadar(reflected,16)==SecureRxEvent::None && radar.motorStatus()==SecureMotorStatus::Pending);
  assert(toSphere(request,17)==SecureRxEvent::None && motorCalls==1);
  const auto lostResult=sphereWire.take();
  toSphere(probe,18);
  assert(toRadar(sphereWire.take(),19)==SecureRxEvent::ProbeAck);
  assert(radar.motorStatus()==SecureMotorStatus::Pending && radar.confirmations()==1);
  radar.tick(462);
  const auto retry=radarWire.take();
  assert(retry==request);
  toSphere(retry,463);
  assert(motorCalls==1);
  const auto result=sphereWire.take();
  assert(result==lostResult);
  auto falseResult=result;
  falseResult.back()^=1;
  toRadar(falseResult,464);
  assert(radar.motorStatus()==SecureMotorStatus::Pending);
  falseResult=result;
  ++falseResult[12];  // Correct HMAC but wrong command identifier.
  assert(sphereWire.sign(config.auth,falseResult.data(),48,falseResult.data()+48));
  toRadar(falseResult,465);
  assert(radar.motorStatus()==SecureMotorStatus::Pending);
  assert(toRadar(result,466)==SecureRxEvent::None && radar.motorStatus()==SecureMotorStatus::Accepted);
  assert(radar.confirmations()==1 && sphere.confirmations()==1);
  toRadar(result,467);
  assert(radar.motorStatus()==SecureMotorStatus::Accepted);

  motorResponse=SecureMotorStatus::CoolingDown;
  assert(radar.requestMotor(500));
  radar.tick(500);
  const auto next=radarWire.take();
  toSphere(next,501);
  toRadar(sphereWire.take(),502);
  assert(motorCalls==2 && radar.motorStatus()==SecureMotorStatus::CoolingDown);
  toSphere(request,503);
  assert(sphereWire.queue.empty() && motorCalls==2);

  sphere.setMotorHandler(nullptr);
  assert(radar.requestMotor(600));
  radar.tick(600);
  toSphere(radarWire.take(),601);
  toRadar(sphereWire.take(),602);
  assert(radar.motorStatus()==SecureMotorStatus::Unavailable && motorCalls==2);

  assert(radar.requestMotor(700));
  radar.tick(700);
  const auto unconfirmed=radarWire.take();
  radar.tick(2050);
  assert(radar.motorStatus()==SecureMotorStatus::Unconfirmed && radarWire.queue.empty());
  toSphere(unconfirmed,2051);
  toRadar(sphereWire.take(),2052);  // Late result cannot turn timeout into success.
  assert(radar.motorStatus()==SecureMotorStatus::Unconfirmed);
  assert(radar.requestMotor(2100));
  radar.cancelMotorRequest();
  radar.tick(2100);
  assert(radar.motorStatus()==SecureMotorStatus::Unconfirmed && radarWire.queue.empty());

  sphere.setMotorHandler(testMotorHandler);
  handshake(16000);
  toSphere(request,16005);
  assert(motorCalls==2 && sphereWire.queue.empty() && sphere.diagnostics().stale_session==1);
}

namespace {
std::array<unsigned,SECURE_SPHERE_COUNT> spherePulses{};
template<unsigned index>
SecureMotorStatus spherePulse(uint32_t) {
  ++spherePulses[index];
  return SecureMotorStatus::Accepted;
}
}

static void testSevenSpheres() {
  const uint8_t radarMac[6]{2};
  Wire radarWire(10), sphereWire(90);
  std::array<std::unique_ptr<SecureRadio>,SECURE_SPHERE_COUNT> radar, spheres;
  const SecureMotorHandler handler[]={
    spherePulse<0>,spherePulse<1>,spherePulse<2>,spherePulse<3>,
    spherePulse<4>,spherePulse<5>,spherePulse<6>
  };
  std::array<bool,SECURE_SPHERE_COUNT> enabled{};
  enabled.fill(true);
  for (uint8_t i=0; i<SECURE_SPHERE_COUNT; ++i) {
    auto atRadar=make(uint8_t(4+2*i));
    auto atSphere=make(2);
    atRadar.id=atSphere.id=i+1;
    atRadar.auth[0]=atSphere.auth[0]=uint8_t(20+i);
    radar[i].reset(new SecureRadio(radarWire,true,atRadar));
    spheres[i].reset(new SecureRadio(sphereWire,false,atSphere));
    spheres[i]->setMotorHandler(handler[i]);
  }
  Frame lastMotor{};
  auto pump=[&](uint32_t now) {
    for (auto &link : radar) link->tick(now);
    while (!radarWire.queue.empty()) {
      const auto f=radarWire.take();
      const unsigned ix=(radarWire.destination-4)/2;
      assert(ix<SECURE_SPHERE_COUNT && f[6]==ix+1);
      if (!enabled[ix]) continue;
      const unsigned type=f[4];
      if (type==7) {
        lastMotor=f;
        spheres[(ix+1)%SECURE_SPHERE_COUNT]->receive(radarMac,f.data(),f.size(),now);
      }
      spheres[ix]->receive(radarMac,f.data(),f.size(),now);
    }
    while (!sphereWire.queue.empty()) {
      const auto f=sphereWire.take();
      const unsigned ix=f[5]-1;
      assert(ix<SECURE_SPHERE_COUNT && sphereWire.destination==radarMac[0] && f[6]==0);
      const uint8_t sphereMac[6]{uint8_t(4+2*ix)};
      radar[ix]->receive(sphereMac,f.data(),f.size(),now);
    }
  };
  for (uint32_t t=0; t<800; t+=50) pump(t);
  for (const auto &link : radar) assert(link->state(800)==LinkState::Paired && link->confirmations()==1);
  assert(radar[6]->requestMotor(800));
  pump(800);
  assert(radar[6]->motorStatus()==SecureMotorStatus::Accepted);
  assert(spherePulses[6]==1);
  for (unsigned i=0; i<6; ++i) {
    assert(spherePulses[i]==0);
    assert(radar[i]->motorStatus()==SecureMotorStatus::Idle);
  }
  spheres[6]->receive(radarMac,lastMotor.data(),lastMotor.size(),801);
  assert(spherePulses[6]==1);
  const auto replayReply=sphereWire.take();
  assert(replayReply[4]==8 && replayReply[5]==7);

  enabled[2]=false;
  for (uint32_t t=850; t<22000; t+=50) pump(t);
  assert(radar[2]->state(22000)!=LinkState::Paired);
  assert(radar[2]->confirmations()==1);
  for (unsigned i=0; i<SECURE_SPHERE_COUNT; ++i) if (i!=2) {
    assert(radar[i]->state(22000)==LinkState::Paired && radar[i]->confirmations()>1);
  }
  enabled[2]=true;
  for (uint32_t t=22000; t<28500; t+=50) pump(t);
  assert(radar[2]->state(28500)==LinkState::Paired && radar[2]->confirmations()>1);
}

static unsigned rgbCalls;
static bool rgbTestHandler(uint32_t command,uint32_t) {
  assert(command==rgbCommand(RgbAction::Found));
  ++rgbCalls;
  return true;
}

static void testRgbCommand() {
  const uint8_t radarMac[6]{2}, sphereMac[6]{4}, otherMac[6]{6};
  Wire radarWire(20), sphereWire(50);
  SecureRadio radar(radarWire,true,make(4)), sphere(sphereWire,false,make(2));
  const uint32_t found=rgbCommand(RgbAction::Found);
  auto toSphere=[&](const Frame &f,uint32_t t){sphere.receive(radarMac,f.data(),f.size(),t);};
  auto toRadar=[&](const Frame &f,uint32_t t){radar.receive(sphereMac,f.data(),f.size(),t);};
  assert(!radar.requestRgb(0,found));
  radar.tick(0);
  toSphere(radarWire.take(),1);
  toRadar(sphereWire.take(),2);
  radar.tick(2);
  toSphere(radarWire.take(),3);
  toRadar(sphereWire.take(),4);
  assert(!radar.requestRgb(10,rgbCommand(RgbAction::On,101)));
  assert(radar.requestRgb(10,found));
  radar.tick(11);
  auto legacy=radarWire.take();
  assert(legacy[4]==9);
  auto probe=radarWire.take();
  toSphere(legacy,12);
  toRadar(sphereWire.take(),13);
  assert(radar.rgbStatus()==SecureMotorStatus::Unavailable);
  toSphere(probe,14);
  toRadar(sphereWire.take(),15);
  sphere.setRgbHandler(rgbTestHandler);
  assert(radar.requestRgb(20,found));
  radar.tick(21);
  const auto request=radarWire.take();
  auto forged=request;
  forged[79]^=1;
  toSphere(forged,22);
  sphere.receive(otherMac,request.data(),request.size(),23);
  assert(sphereWire.queue.empty() && rgbCalls==0);
  toSphere(request,24);
  const auto result=sphereWire.take();
  radar.tick(471);
  assert(radarWire.take()==request);
  toSphere(request,472);
  assert(sphereWire.take()==result && rgbCalls==1);
  toRadar(result,473);
  assert(radar.rgbStatus()==SecureMotorStatus::Accepted && radar.confirmations()==1);
  assert(radar.requestRgb(500,found));
  radar.tick(501);
  toSphere(radarWire.take(),502);
  toRadar(sphereWire.take(),503);
  toSphere(request,504);
  assert(sphereWire.queue.empty() && rgbCalls==2);
}

int main() {
  testSevenSpheres();
  testRgbCommand();
  testMotorCommand();
  testRecords();
#if M1C_BENCH_FIXTURE
  testBenchFixture();
#endif
  const uint8_t radarMac[6]{2}, sphereMac[6]{4}, wrongMac[6]{6};
  Wire radarWire(20), sphereWire(50);
  auto rc=make(4), sc=make(2);
  SecureRadio radar(radarWire,true,rc), sphere(sphereWire,false,sc);
  auto toSphere=[&](const Frame &f,uint32_t t){sphere.receive(radarMac,f.data(),f.size(),t);};
  auto toRadar=[&](const Frame &f,uint32_t t){return radar.receive(sphereMac,f.data(),f.size(),t);};
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
  assert(toRadar(challenge,2)==SecureRxEvent::None);
  assert(radar.state(2)==LinkState::AwaitingAuthenticatedSession);
  radar.tick(2);
  Frame finish=radarWire.take();
  toSphere(finish,3);
  Frame ready=sphereWire.take();
  assert(sphere.state(3)==LinkState::Paired);
  assert(radar.state(3)==LinkState::AwaitingAuthenticatedSession);
  assert(radar.linkStatus(3,true)==SecureLinkStatus::AwaitingHandshake);
  radar.tick(452); // retry FINISH if READY was lost
  assert(radarWire.take()==finish);
  toSphere(finish,453);
  assert(sphereWire.take()==ready);
  assert(toRadar(ready,454)==SecureRxEvent::SessionStarted);
  assert(radar.state(454)==LinkState::Paired);
  assert(radar.linkStatus(454,true)==SecureLinkStatus::Paired);
  assert(radar.linkStatus(454,false)==SecureLinkStatus::ChannelMismatch);
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
  assert(toRadar(forged,906)==SecureRxEvent::None);
  assert(radar.confirmations()==0);
  assert(radar.diagnostics().auth_rejected==0); // Malformed ACK is rejected before HMAC.
  forged=ack; forged[79]^=1;
  assert(toRadar(forged,906)==SecureRxEvent::None);
  assert(radar.receive(wrongMac,ack.data(),ack.size(),906)==SecureRxEvent::None);
  Frame wrongSequence=ack;
  wrongSequence[12]^=1;
  assert(radarWire.sign(rc.auth,wrongSequence.data(),48,wrongSequence.data()+48));
  assert(toRadar(wrongSequence,906)==SecureRxEvent::None);
  assert(radar.confirmations()==0);
  assert(radar.diagnostics().auth_rejected==1);
  assert(toRadar(ack,907)==SecureRxEvent::ProbeAck);
  assert(toRadar(ack,908)==SecureRxEvent::None);
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
  assert(toRadar(ready,16002)==SecureRxEvent::None);
  assert(radar.state(16002)==LinkState::Lost);
  assert(radar.linkStatus(16002,true)==SecureLinkStatus::Lost);
  assert(radar.linkStatus(16002,false)==SecureLinkStatus::ChannelMismatch);
  rebooted.receive(radarMac,nextHello.data(),nextHello.size(),16003);
  toRadar(rebootWire.take(),16004);
  radar.tick(16004);
  Frame nextFinish=radarWire.take();
  rebooted.receive(radarMac,nextFinish.data(),nextFinish.size(),16005);
  assert(toRadar(rebootWire.take(),16006)==SecureRxEvent::SessionStarted);
  assert(radar.state(16006)==LinkState::Paired);
  assert(radar.linkStatus(16006,true)==SecureLinkStatus::Paired);
  assert(toRadar(ack,16006)==SecureRxEvent::None);
  assert(radar.confirmations()==1);
  assert(radar.diagnostics().stale_session==1);
  rebooted.receive(radarMac,probe.data(),probe.size(),16007);
  assert(rebooted.diagnostics().stale_session==1);
  assert(rebooted.confirmations()==0 && rebootWire.queue.empty());

  // Restart the radar while the sphere is still paired, then exchange a new probe.
  Wire restartedRadarWire(110);
  SecureRadio restartedRadar(restartedRadarWire,true,rc);
  assert(restartedRadar.linkStatus(16010,true)==SecureLinkStatus::AwaitingHandshake);
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
  // A full local TX queue consumes an attempt without inflating the queued count.
  restartedRadarWire.accepting=false;
  restartedRadar.tick(16464);
  assert(restartedRadarWire.queue.empty());
  assert(restartedRadar.diagnostics().probe_retry_queued==0);
  restartedRadarWire.accepting=true;
  restartedRadar.tick(16914);
  assert(restartedRadarWire.take()==restartProbe);
  assert(restartedRadar.diagnostics().probe_retry_queued==1);
  rebooted.receive(radarMac,restartProbe.data(),restartProbe.size(),16915);
  assert(rebootWire.take()==restartAck);
  restartedRadar.receive(sphereMac,restartAck.data(),restartAck.size(),16916);
  assert(rebooted.confirmations()==1 && restartedRadar.confirmations()==1);

  // Repeating the last probe may recover a lost ACK but cannot extend freshness.
  rebooted.receive(radarMac,restartProbe.data(),restartProbe.size(),31000);
  assert(rebootWire.take()==restartAck && rebooted.confirmations()==1);
  assert(rebooted.state(31016)==LinkState::Lost);
}
