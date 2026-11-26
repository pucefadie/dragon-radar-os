#pragma once
#include "beacon_protocol.h"
#include <stdint.h>
enum class DevOutcome : uint8_t { None, TxBusyTimeout, ResponseTimeout, Response };
enum class DevEvent : uint8_t { TxBusyTimeout, ResponseTimeout, LinkLost, LinkRecovered, Response };
const char *devMessageName(RadioMessage type);
class DevRadioTransport { public: virtual ~DevRadioTransport() {} virtual bool send(const uint8_t mac[6],const uint8_t frame[RADIO_FRAME_SIZE])=0; virtual void event(DevEvent, uint8_t, uint32_t, uint32_t) {} };
class DevRadar {
 public:
  DevRadar(DevRadioTransport &t,uint32_t session):tx(t),session_(session){}
  void beacon(const uint8_t mac[6],uint8_t id,uint32_t boot,uint32_t now);
  void receive(const uint8_t mac[6],const uint8_t *bytes,size_t n,uint32_t now);
  void tick(uint32_t now);
  bool linked() const {return linked_;} bool pending() const{return pending_;} DevOutcome outcome() const {return outcome_;}
 private:
  void request(RadioMessage type,uint32_t now);
  DevRadioTransport &tx; uint8_t mac_[6]{},frame_[RADIO_FRAME_SIZE]{}; uint8_t id_=0,attempts_=0; uint32_t session_,seq_=0,boot_=0,pendingSeq_=0,sent_=0,started_=0,next_=0,lastRx_=0; RadioMessage expected_=RadioMessage::Status; DevOutcome outcome_=DevOutcome::None; bool known_=false,pending_=false,linked_=false,scheduled_=false;
};
class DevSphere {
 public:
  DevSphere(DevRadioTransport &t,uint8_t id,uint32_t boot):tx(t),id_(id),boot_(boot){}
  void receive(const uint8_t mac[6],const uint8_t *bytes,size_t n,uint32_t now);
  void tick(uint32_t now);
  bool online() const{return online_;}
 private:
  DevRadioTransport &tx; uint8_t id_,mac_[6]{}; uint32_t boot_,seq_=0,lastRx_=0; bool known_=false,online_=false;
};
