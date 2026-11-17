#include "dev_radio.h"
#include <string.h>
const char *devMessageName(RadioMessage t){switch(t){case RadioMessage::Beacon:return "BEACON";case RadioMessage::Discovery:return "DISCOVERY";case RadioMessage::Status:return "STATUS";case RadioMessage::Ping:return "PING";case RadioMessage::Pong:return "PONG";case RadioMessage::Command:return "COMMAND";case RadioMessage::CommandAck:return "COMMAND_ACK";case RadioMessage::Heartbeat:return "HEARTBEAT";default:return "OTHER";}}
void DevRadar::request(RadioMessage type,uint32_t now) {
 if(pending_ || !known_) return;
 RadioFrame f{};f.type=type;f.id=0;f.destination=id_;f.flags=RADIO_FLAG_DIRECTED;f.session=session_;f.sequence=++seq_;
 if(type==RadioMessage::Command){f.payload_length=1;f.payload[0]=0;f.flags|=RADIO_FLAG_ACK_REQUIRED;}
 radioEncode(f,frame_);pendingSeq_=f.sequence;expected_=type==RadioMessage::Command?RadioMessage::CommandAck:type==RadioMessage::Ping?RadioMessage::Pong:RadioMessage::Status;pending_=true;outcome_=DevOutcome::None;attempts_=0;started_=now;sent_=now-500;
}
void DevRadar::beacon(const uint8_t mac[6],uint8_t id,uint32_t boot,uint32_t now) {
 if(!mac||id!=1||!boot)return;
 if(known_ && memcmp(mac_,mac,6))return;
 if(!known_||boot_!=boot){memcpy(mac_,mac,6);id_=id;boot_=boot;known_=true;pending_=false;linked_=false;scheduled_=false;}
 if(!pending_ && (!scheduled_ || int32_t(now-next_)>=0)){next_=now+3000;scheduled_=true;request(linked_?RadioMessage::Heartbeat:RadioMessage::Discovery,now);}
}
void DevRadar::receive(const uint8_t mac[6],const uint8_t *bytes,size_t n,uint32_t now){
 RadioFrame f{};if(!mac||!radioDecode(bytes,n,f)||!pending_||memcmp(mac_,mac,6)||f.type!=expected_||f.id!=id_||f.destination!=0||!(f.flags&RADIO_FLAG_DIRECTED)||f.session!=boot_||f.ack_sequence!=pendingSeq_||!attempts_||uint32_t(now-started_)>=4000||uint32_t(now-sent_)>=1500)return;
 pending_=false;outcome_=DevOutcome::Response;if(!linked_)tx.event(DevEvent::LinkRecovered,id_,f.ack_sequence,0);linked_=true;lastRx_=now;tx.event(DevEvent::Response,id_,f.ack_sequence,uint32_t(now-sent_));
 if(f.type==RadioMessage::Status&&f.payload_length==1&&f.payload[0]==1)request(RadioMessage::Ping,now);
 else if(f.type==RadioMessage::Pong)request(RadioMessage::Command,now);
}
void DevRadar::tick(uint32_t now){
 if(pending_&&uint32_t(now-started_)>=4000){outcome_=attempts_?DevOutcome::ResponseTimeout:DevOutcome::TxBusyTimeout;tx.event(attempts_?DevEvent::ResponseTimeout:DevEvent::TxBusyTimeout,id_,pendingSeq_,0);pending_=false;if(linked_)tx.event(DevEvent::LinkLost,id_,pendingSeq_,0);linked_=false;next_=now+2000;scheduled_=true;}
 else if(pending_&&uint32_t(now-sent_)>=500){if(attempts_>=3){outcome_=DevOutcome::ResponseTimeout;tx.event(DevEvent::ResponseTimeout,id_,pendingSeq_,0);pending_=false;if(linked_)tx.event(DevEvent::LinkLost,id_,pendingSeq_,0);linked_=false;next_=now+2000;scheduled_=true;}else if(tx.send(mac_,frame_)){sent_=now;++attempts_;}else sent_=now-450;}
 if(linked_&&uint32_t(now-lastRx_)>10000){linked_=false;tx.event(DevEvent::LinkLost,id_,0,0);}
}
void DevSphere::tick(uint32_t now){if(online_&&uint32_t(now-lastRx_)>=10000)online_=false;}
void DevSphere::receive(const uint8_t mac[6],const uint8_t *bytes,size_t n,uint32_t now){
 tick(now);RadioFrame f{};if(!mac||!radioDecode(bytes,n,f)||f.id!=0||f.destination!=id_||!(f.flags&RADIO_FLAG_DIRECTED))return;
 if(f.type!=RadioMessage::Discovery&&f.type!=RadioMessage::Ping&&f.type!=RadioMessage::Heartbeat&&!(f.type==RadioMessage::Command&&f.payload_length==1&&f.payload[0]==0))return;
 if(!known_){if(f.type!=RadioMessage::Discovery)return;memcpy(mac_,mac,6);known_=true;}if(memcmp(mac_,mac,6)||(!online_&&f.type!=RadioMessage::Discovery))return;
 online_=true;lastRx_=now;RadioFrame reply{};reply.type=f.type==RadioMessage::Ping?RadioMessage::Pong:f.type==RadioMessage::Command?RadioMessage::CommandAck:RadioMessage::Status;reply.id=id_;reply.destination=0;reply.flags=RADIO_FLAG_DIRECTED;reply.session=boot_;reply.sequence=++seq_;reply.ack_sequence=f.sequence;if(f.type==RadioMessage::Discovery){reply.payload_length=1;reply.payload[0]=1;}uint8_t out[RADIO_FRAME_SIZE];radioEncode(reply,out);tx.send(mac,out);
}
