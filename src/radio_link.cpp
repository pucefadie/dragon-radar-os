#include "radio_link.h"
#include <string.h>
void PairingRegistry::open(uint32_t now,uint32_t ms){opened_=now;until_=now+ms;window_open_=ms>0;}
bool PairingRegistry::pair(uint8_t id,const uint8_t mac[6],const uint8_t key[16],uint32_t now){
 if(id<1||id>7||!mac||!key||!window_open_||int32_t(now-opened_)<0||int32_t(until_-now)<=0)return false;
 PairedPeer &p=peers_[id-1];if(p.valid&&memcmp(p.mac,mac,6)!=0)return false;
 p.valid=true;memcpy(p.mac,mac,6);memcpy(p.lmk,key,16);p.last_seen=now;return true;
}
bool PairingRegistry::remove(uint8_t id){if(id<1||id>7)return false;peers_[id-1]=PairedPeer{};return true;}
bool PairingRegistry::trusted(uint8_t id,const uint8_t mac[6],uint32_t now,uint32_t stale)const {const PairedPeer*p=peer(id);return p&&p->valid&&mac&&!memcmp(p->mac,mac,6)&&uint32_t(now-p->last_seen)<stale;}
const PairedPeer *PairingRegistry::peer(uint8_t id)const{return id>=1&&id<=7?&peers_[id-1]:nullptr;}
LinkState PairingRegistry::state(uint8_t id,uint32_t now,uint32_t stale)const {const PairedPeer*p=peer(id);if(!p)return LinkState::Unpaired;if(!p->valid)return LinkState::Unpaired;return uint32_t(now-p->last_seen)<stale?LinkState::Paired:LinkState::Lost;}
bool RadioAckTracker::begin(uint8_t peer,uint32_t seq,const uint8_t*f,uint8_t len,uint32_t now,uint32_t retry,uint8_t max){if(pending_||!peer||!seq||!f||len!=RADIO_FRAME_SIZE||!max)return false;peer_=peer;seq_=seq;memcpy(frame_,f,len);length_=len;retry_ms_=retry;max_attempts_=max;attempts_=0;due_=now;pending_=true;failed_=false;return true;}
bool RadioAckTracker::due(uint32_t now,uint8_t*out,uint8_t&len){len=0;if(!pending_||int32_t(now-due_)<0)return false;if(attempts_>=max_attempts_){pending_=false;failed_=true;return false;}if(!out)return false;memcpy(out,frame_,length_);len=length_;++attempts_;due_=now+retry_ms_;return true;}
bool RadioAckTracker::acknowledge(uint8_t peer,uint32_t seq){if(!pending_||peer!=peer_||seq!=seq_)return false;pending_=false;failed_=false;return true;}
bool ReplayWindow::accept(uint32_t session,uint32_t seq){if(!seq)return false;if(!valid_){valid_=true;session_=session;last_=seq;return true;}if(session!=session_||int32_t(seq-last_)<=0)return false;last_=seq;return true;}
void ReplayWindow::reset(){valid_=false;session_=last_=0;}
