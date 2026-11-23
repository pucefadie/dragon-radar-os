#include "secure_radio.h"
#include <cstring>

namespace {
constexpr uint32_t handshakeMs=5000, retryMs=450, intervalMs=3000;
// The full nonce pair gates every message before these legacy trackers are
// called. The handle is local to the current handshake and never on the wire.
constexpr uint32_t handle=1;
bool equal(const uint8_t *a,const uint8_t *b,size_t n) {
  uint8_t diff=0;
  for(size_t i=0;i<n;++i) diff |= a[i]^b[i];
  return diff==0;
}
bool nonzero(const uint8_t *a,size_t n) {
  uint8_t v=0;
  for(size_t i=0;i<n;++i) v|=a[i];
  return v!=0;
}
void put32(uint8_t *b,uint32_t n) { for(unsigned i=0;i<4;++i) b[i]=uint8_t(n>>(8*i)); }
uint32_t get32(const uint8_t *b) { uint32_t n=0; for(unsigned i=0;i<4;++i) n|=uint32_t(b[i])<<(8*i); return n; }
}

bool secureRadioDecodeConfig(const uint8_t *b,size_t len,SecureRadioConfig &out) {
  // Versioned magic, sphere ID, channel, peer MAC, LMK, auth key, PMK field.
  if (!b || len!=SECURE_CONFIG_SIZE || std::memcmp(b,"DRC",3) ||
      (b[3]!='1' && b[3]!='2') || b[4]!=1 ||
      b[5]<1 || b[5]>13 || (b[6]&1) || !nonzero(b+6,6) ||
      !nonzero(b+12,16) || !nonzero(b+28,32) || !nonzero(b+60,16) ||
      equal(b+12,b+60,16)) return false;
  if (b[3]=='2' && (equal(b+60,b+28,16) || equal(b+60,b+44,16) ||
                    equal(b+12,b+28,16) || equal(b+12,b+44,16))) return false;
  out.version=b[3]=='2'?SecureRecordVersion::V2:SecureRecordVersion::V1;
  out.channel=b[5];
  std::memcpy(out.peer,b+6,6);
  std::memcpy(out.lmk,b+12,16);
  std::memcpy(out.auth,b+28,32);
  std::memcpy(out.pmk,b+60,16);
  return true;
}

void secureRadioClear(void *data,size_t length) {
  volatile uint8_t *bytes=static_cast<volatile uint8_t*>(data);
  while (length--) *bytes++=0;
}

bool secureRadioLoadConfig(SecureRecordReader read,void *storage,SecureRadioConfig &out) {
  uint8_t data[SECURE_CONFIG_SIZE]{};
  size_t length=sizeof(data);
  SecureRecordVersion expected=SecureRecordVersion::V2;
  SecureRecordRead result=read(storage,SECURE_RECORD_V2_KEY,data,length);
  if (result==SecureRecordRead::Missing) {
    expected=SecureRecordVersion::V1;
    length=sizeof(data);
    result=read(storage,SECURE_RECORD_V1_KEY,data,length);
  }
  const bool valid=result==SecureRecordRead::Loaded &&
    secureRadioDecodeConfig(data,length,out) && out.version==expected;
  secureRadioClear(data,sizeof(data));
  if (!valid) secureRadioClear(&out,sizeof(out));
  return valid;
}

bool secureRadioResolvePmk(SecureRadioConfig &config,SecureRadioIo &io) {
  if (config.version==SecureRecordVersion::V2) return true;
  if (config.version!=SecureRecordVersion::V1) return false;
  // Exact label used by the working M1c profile. Changing it breaks DRC1 peers.
  static constexpr uint8_t context[]="DRA1 M1c bench shared PMK";
  uint8_t derived[32]{};
  const bool valid=io.sign(config.auth,context,sizeof(context)-1,derived) &&
    nonzero(derived,sizeof(config.pmk)) && !equal(derived,config.lmk,sizeof(config.pmk));
  if (valid) std::memcpy(config.pmk,derived,sizeof(config.pmk));
  else secureRadioClear(config.pmk,sizeof(config.pmk));
  secureRadioClear(derived,sizeof(derived));
  return valid;
}

SecureRadio::SecureRadio(SecureRadioIo &io,bool controller,const SecureRadioConfig &config)
    : io_(io),controller_(controller),config_(config) {
  peers_.open(0,1);
  peers_.pair(1,config_.peer,config_.lmk,0);
  secureRadioClear(config_.pmk,sizeof(config_.pmk));
  secureRadioClear(config_.lmk,sizeof(config_.lmk));
}

bool SecureRadio::pack(uint8_t *b,Kind kind,const Nonces &nonce,uint32_t seq,uint32_t ack) {
  std::memcpy(b,"DRA1",4);
  b[4]=uint8_t(kind); b[5]=controller_?0:1; b[6]=controller_?1:0; b[7]=0;
  put32(b+8,seq); put32(b+12,ack);
  std::memcpy(b+16,nonce.a,16); std::memcpy(b+32,nonce.b,16);
  return io_.sign(config_.auth,b,48,b+48);
}
void SecureRadio::emit(Kind kind,const Nonces &nonce,uint32_t seq,uint32_t ack) {
  uint8_t bytes[SECURE_RADIO_SIZE];
  if(pack(bytes,kind,nonce,seq,ack)) io_.send(config_.peer,bytes,sizeof(bytes));
}
void SecureRadio::established(uint32_t now) {
  replay_.reset();
  replay_.beginAuthenticatedSession(handle,true);
  peers_.beginAuthenticatedSession(1,config_.peer,handle,now,true);
  online_=true;
  attempting_=false;
  seq_=lastProbe_=0;
  ack_=RadioAckTracker{};
  nextProbe_=now;
#if M1C_BENCH_FIXTURE
  benchAction_=M1cRadioAction::None;
#endif
}
#if M1C_BENCH_FIXTURE
bool SecureRadio::benchAction(M1cRadioAction action, uint32_t now) {
  if (!controller_ || state(now)!=LinkState::Paired || benchAction_!=M1cRadioAction::None) return false;
  if (action==M1cRadioAction::Replay) {
    if (!benchResult_.captured ||
        (equal(benchCaptured_+16,current_.a,16) && equal(benchCaptured_+32,current_.b,16)) ||
        !io_.send(config_.peer,benchCaptured_,sizeof(benchCaptured_))) return false;
    benchResult_.captured=false;
    std::memset(benchCaptured_,0,sizeof(benchCaptured_));
    ++benchResult_.replay_queued;
    return true;
  }
  if (action==M1cRadioAction::None) return false;
  benchAction_=action;
  return true;
}

bool SecureRadio::benchSendProbe(uint8_t *frame, uint8_t length) {
  const bool initial=ack_.attempts()==1;
  const bool corrupt=initial && benchAction_==M1cRadioAction::CorruptProbe;
  if (corrupt) frame[SECURE_RADIO_SIZE-1]^=1; // The tracker still owns the signed original.
  if (!io_.send(config_.peer,frame,length)) return false;
  if (corrupt) {
    ++benchResult_.corrupt_queued;
    benchAction_=M1cRadioAction::None;
  } else if (initial && benchAction_==M1cRadioAction::CaptureProbe) {
    std::memcpy(benchCaptured_,frame,sizeof(benchCaptured_));
    benchResult_.captured=true;
    benchAction_=M1cRadioAction::None;
  }
  return true;
}
#endif

SecureLinkStatus SecureRadio::linkStatus(uint32_t now, bool channelAligned) const {
  if (!channelAligned) return SecureLinkStatus::ChannelMismatch;
  switch (state(now)) {
    case LinkState::Paired:
      return online_ ? SecureLinkStatus::Paired : SecureLinkStatus::AwaitingHandshake;
    case LinkState::Lost:
      return SecureLinkStatus::Lost;
    case LinkState::AwaitingAuthenticatedSession:
      return SecureLinkStatus::AwaitingHandshake;
    default:
      return SecureLinkStatus::Unavailable;
  }
}

void SecureRadio::tick(uint32_t now) {
  if (!controller_) return;
  if (online_ && peers_.state(1,now)!=LinkState::Paired) online_=false;
  if (!online_) {
    if (!attempting_ || now-started_>=handshakeMs) {
      current_=Nonces{};
      io_.random(current_.a,sizeof(current_.a));
      started_=now;
      lastSend_=now-retryMs;
      attempting_=true;
      challenged_=false;
    }
    if (nonzero(current_.a,16) && now-lastSend_>=retryMs) {
      emit(challenged_?Kind::Finish:Kind::Hello,current_);
      lastSend_=now;
    }
    return;
  }
  uint8_t frame[SECURE_RADIO_SIZE], length=0;
  if (!ack_.pending() && int32_t(now-nextProbe_)>=0) {
    if (seq_==0x7ffffffe) { online_=false; return; } // Renew before signed replay window wraps.
    ++seq_;
    if (pack(frame,Kind::Probe,current_,seq_,0))
      ack_.begin(1,config_.peer,handle,seq_,frame,sizeof(frame),now);
    nextProbe_=now+intervalMs;
  }
  if (!ack_.due(now,frame,length)) return;
#if M1C_BENCH_FIXTURE
  const bool queued=benchSendProbe(frame,length);
#else
  const bool queued=io_.send(config_.peer,frame,length);
#endif
  if (queued && ack_.attempts()>1) {
    ++diagnostics_.probe_retry_queued;
  }
}
void SecureRadio::receive(const uint8_t *source,const uint8_t *b,size_t len,uint32_t now) {
  if (!source || !b || len!=SECURE_RADIO_SIZE || !equal(source,config_.peer,6) ||
      std::memcmp(b,"DRA1",4) || b[5]!=(controller_?1:0) || b[6]!=(controller_?0:1) || b[7]) return;
  const Kind kind=Kind(b[4]);
  const uint32_t seq=get32(b+8), ack=get32(b+12);
  Nonces nonce{};
  std::memcpy(nonce.a,b+16,16); std::memcpy(nonce.b,b+32,16);
  if (!nonzero(nonce.a,16) || (nonzero(nonce.b,16)==(kind==Kind::Hello))) return;
  if (kind==Kind::Probe ? (!seq || ack) : kind==Kind::Ack ? (seq || !ack) : (seq || ack)) return;
  uint8_t tag[32];
  if (!io_.sign(config_.auth,b,48,tag)) return;
  if (!equal(tag,b+48,sizeof(tag))) {
    ++diagnostics_.auth_rejected;
    return;
  }
  const bool current=equal(nonce.a,current_.a,16) && equal(nonce.b,current_.b,16);
  if (!current && online_ && (controller_ ? kind==Kind::Ack : kind==Kind::Probe)) {
    ++diagnostics_.stale_session;
    return;
  }
  if (controller_) {
    const bool freshAttempt=attempting_ && now-started_<handshakeMs;
    if (kind==Kind::Challenge && freshAttempt && equal(nonce.a,current_.a,16)) {
      if (challenged_ && !current) return;
      if (!challenged_) {
        current_=nonce;
        challenged_=true;
        lastSend_=now-retryMs;
      }
    } else if (kind==Kind::Ready && freshAttempt && challenged_ && current) {
      established(now);
    } else if (kind==Kind::Ack && online_ && current && peers_.state(1,now)==LinkState::Paired) {
#if M1C_BENCH_FIXTURE
      if (benchAction_==M1cRadioAction::DropAck && ack_.pending() && ack==seq_) {
        ++benchResult_.ack_dropped;
        benchAction_=M1cRadioAction::None;
        return;
      }
#endif
      RadioFrame result{};
      result.type=RadioMessage::CommandAck; result.id=1; result.destination=0;
      result.session=handle; result.ack_sequence=ack;
      if (ack_.acknowledge(source,result,true)) {
        peers_.markAuthenticatedRx(1,source,handle,now,true);
        ++confirmations_;
      }
    }
    return;
  }
  if (kind==Kind::Hello) {
    if (online_ && equal(nonce.a,current_.a,16)) return;
    if (!pendingValid_ || now-pendingAt_>=handshakeMs) {
      pending_=nonce;
      io_.random(pending_.b,16);
      pendingValid_=nonzero(pending_.b,16);
      pendingAt_=now;
    }
    if (pendingValid_ && equal(nonce.a,pending_.a,16)) emit(Kind::Challenge,pending_);
  } else if (kind==Kind::Finish) {
    if (online_ && current) {
      emit(Kind::Ready,current_); // Lost READY: reply without changing session or freshness.
    } else if (pendingValid_ && now-pendingAt_<handshakeMs &&
        equal(nonce.a,pending_.a,16) && equal(nonce.b,pending_.b,16)) {
      current_=pending_;
      pendingValid_=false;
      established(now);
      emit(Kind::Ready,current_);
    }
  } else if (kind==Kind::Probe && online_ && current && peers_.state(1,now)==LinkState::Paired) {
    if (replay_.accept(handle,seq)) {
      lastProbe_=seq;
      peers_.markAuthenticatedRx(1,source,handle,now,true);
      ++confirmations_;
    } else if (seq==lastProbe_) {
      ++diagnostics_.duplicate_probe;
    }
    // Retransmit exactly the last diagnostic acknowledgment; never refresh on duplicate.
    if (seq==lastProbe_) emit(Kind::Ack,current_,0,seq);
  }
}
