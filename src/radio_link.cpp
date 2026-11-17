#include "radio_link.h"
#include <string.h>
void PairingRegistry::open(uint32_t now,uint32_t ms){opened_=now;until_=now+ms;window_open_=ms>0;}
bool PairingRegistry::pair(uint8_t id,const uint8_t mac[6],const uint8_t key[16],uint32_t now){
 if(id<1||id>7||!mac||!key||!window_open_||int32_t(now-opened_)<0||int32_t(until_-now)<=0)return false;
 PairedPeer &p=peers_[id-1];if(p.valid&&memcmp(p.mac,mac,6)!=0)return false;
 p.valid=true;memcpy(p.mac,mac,6);memcpy(p.lmk,key,16);p.last_seen=0;return true;
}
bool PairingRegistry::beginAuthenticatedSession(uint8_t id,const uint8_t mac[6],uint32_t session,uint32_t now,bool authenticated){const PairedPeer*found=peer(id);if(!authenticated||!session||!found||!found->valid||!mac||memcmp(found->mac,mac,6))return false;PairedPeer&p=peers_[id-1];p.session=session;p.session_valid=true;p.last_seen=now;p.authenticated_rx_seen=true;return true;}
bool PairingRegistry::markAuthenticatedRx(uint8_t id,const uint8_t mac[6],uint32_t session,uint32_t now,bool authenticated){if(!authenticated||!trusted(id,mac,now,UINT32_MAX))return false;PairedPeer&p=peers_[id-1];if(p.session!=session)return false;p.last_seen=now;p.authenticated_rx_seen=true;return true;}
bool PairingRegistry::remove(uint8_t id){if(id<1||id>7)return false;peers_[id-1]=PairedPeer{};return true;}
bool PairingRegistry::trusted(uint8_t id,const uint8_t mac[6],uint32_t now,uint32_t stale)const {const PairedPeer*p=peer(id);return p&&p->valid&&p->session_valid&&mac&&!memcmp(p->mac,mac,6)&&uint32_t(now-p->last_seen)<stale;}
const PairedPeer *PairingRegistry::peer(uint8_t id)const{return id>=1&&id<=7?&peers_[id-1]:nullptr;}
LinkState PairingRegistry::state(uint8_t id,uint32_t now,uint32_t stale)const {const PairedPeer*p=peer(id);if(!p||!p->valid)return LinkState::Unpaired;if(!p->session_valid||!p->authenticated_rx_seen)return LinkState::AwaitingAuthenticatedSession;return uint32_t(now-p->last_seen)<stale?LinkState::Paired:LinkState::Lost;}
bool RadioAckTracker::begin(uint8_t peer,const uint8_t mac[6],uint32_t session,uint32_t seq,const uint8_t*f,uint8_t len,uint32_t now,uint32_t retry,uint8_t max){if(pending_||peer<1||peer>7||!mac||!session||!seq||!f||len!=RADIO_FRAME_SIZE||!max)return false;peer_=peer;memcpy(peer_mac_,mac,6);session_=session;seq_=seq;memcpy(frame_,f,len);length_=len;retry_ms_=retry;max_attempts_=max;attempts_=0;due_=now;pending_=true;failed_=false;return true;}
bool RadioAckTracker::due(uint32_t now,uint8_t*out,uint8_t&len){len=0;if(!pending_||int32_t(now-due_)<0)return false;if(attempts_>=max_attempts_){pending_=false;failed_=true;return false;}if(!out)return false;memcpy(out,frame_,length_);len=length_;++attempts_;due_=now+retry_ms_;return true;}
bool RadioAckTracker::acknowledge(const uint8_t mac[6],const RadioFrame&a,bool authenticated){if(!pending_||!authenticated||!mac||memcmp(mac,peer_mac_,6)||a.type!=RadioMessage::CommandAck||a.id!=peer_||a.destination!=RADIO_ID_CONTROLLER||a.session!=session_||a.ack_sequence!=seq_)return false;pending_=false;failed_=false;return true;}
bool ReplayWindow::beginAuthenticatedSession(uint32_t session,bool auth){if(!auth||!session)return false;if(valid_&&session==session_)return false;session_=session;last_=0;valid_=true;return true;}
bool ReplayWindow::accept(uint32_t session,uint32_t seq){if(!valid_||session!=session_||!seq||int32_t(seq-last_)<=0)return false;last_=seq;return true;}
void ReplayWindow::reset(){valid_=false;session_=last_=0;}
