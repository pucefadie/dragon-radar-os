#pragma once
#include <stdint.h>
#include "beacon_protocol.h"
enum class LinkState:uint8_t { Unpaired, PairWindow, AwaitingAuthenticatedSession, Paired, Lost };
struct PairedPeer { bool valid=false;uint8_t mac[6]{};uint8_t lmk[16]{};uint32_t last_seen=0,session=0;bool session_valid=false,authenticated_rx_seen=false; };
class PairingRegistry {
 public:
  void open(uint32_t now,uint32_t duration_ms);
  bool pair(uint8_t id,const uint8_t mac[6],const uint8_t lmk[16],uint32_t now);
  bool beginAuthenticatedSession(uint8_t id,const uint8_t mac[6],uint32_t session,uint32_t now,bool authenticated_handshake);
  // Call only after the transport confirms an encrypted unicast peer and MAC.
  bool markAuthenticatedRx(uint8_t id,const uint8_t mac[6],uint32_t session,uint32_t now,bool authenticated);
  bool remove(uint8_t id);
  bool trusted(uint8_t id,const uint8_t mac[6],uint32_t now,uint32_t stale_ms=15000)const;
  const PairedPeer *peer(uint8_t id)const;
  LinkState state(uint8_t id,uint32_t now,uint32_t stale_ms=15000)const;
 private:uint32_t opened_=0,until_=0;bool window_open_=false;PairedPeer peers_[7]{};
};
class RadioAckTracker {
 public:
  bool begin(uint8_t peer,const uint8_t peer_mac[6],uint32_t session,uint32_t seq,const uint8_t *frame,uint8_t length,uint32_t now,uint32_t retry_ms=450,uint8_t attempts=3);
  bool due(uint32_t now,uint8_t *out,uint8_t &length);
  // authenticated must represent verified ESP-NOW encrypted unicast, not CRC validity.
  bool acknowledge(const uint8_t peer_mac[6],const RadioFrame &ack,bool authenticated);
  bool pending()const{return pending_;}bool failed()const{return failed_;}uint8_t attempts()const{return attempts_;}
 private:uint8_t peer_=0,peer_mac_[6]{},length_=0,max_attempts_=0,attempts_=0,frame_[RADIO_FRAME_SIZE]{};uint32_t session_=0,seq_=0,due_=0,retry_ms_=0;bool pending_=false,failed_=false;
};
class ReplayWindow {public:bool beginAuthenticatedSession(uint32_t session,bool authenticated_handshake);bool accept(uint32_t session,uint32_t seq);void reset();private:uint32_t session_=0,last_=0;bool valid_=false;};
