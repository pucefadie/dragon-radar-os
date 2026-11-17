#include "beacon_protocol.h"
#include <string.h>
namespace {
uint32_t get32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24; }
void put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i)); }
bool validType(uint8_t t) { return t>=uint8_t(RadioMessage::Beacon)&&t<=uint8_t(RadioMessage::BatteryStatus); }
}
uint16_t radioCrc16(const uint8_t *p,size_t n) {
  uint16_t crc=0xffff;
  while(n--) { crc^=uint16_t(*p++)<<8; for(unsigned i=0;i<8;++i) crc=(crc&0x8000)?uint16_t((crc<<1)^0x1021):uint16_t(crc<<1); }
  return crc;
}
bool radioDecode(const uint8_t *p,size_t n,RadioFrame &o) {
  if(!p||n!=RADIO_FRAME_SIZE||memcmp(p,"DRB2",4)||p[4]!=RADIO_PROTOCOL_VERSION||!validType(p[5])||p[6]<1||p[6]>7||(p[7]&0xf0)||p[22]>RADIO_PAYLOAD_SIZE||p[23]!=0) return false;
  if(uint16_t(p[32]|uint16_t(p[33])<<8)!=radioCrc16(p,32)) return false;
  o.type=RadioMessage(p[5]);o.id=p[6];o.flags=p[7];o.session=get32(p+8);o.sequence=get32(p+12);o.ack_sequence=get32(p+16);o.power_profile=p[20];o.tx_power_qdbm=p[21];o.payload_length=p[22];memcpy(o.payload,p+24,RADIO_PAYLOAD_SIZE);
  for(unsigned i=o.payload_length;i<RADIO_PAYLOAD_SIZE;++i) if(o.payload[i]) return false;
  if(o.type==RadioMessage::Beacon||o.type==RadioMessage::BatteryStatus) { uint16_t mv=uint16_t(o.payload[0]|uint16_t(o.payload[1])<<8); if(o.payload[2]>1||(o.payload[2]&&(mv<2500||mv>5500))||(!o.payload[2]&&mv)) return false; }
  return true;
}
void radioEncode(const RadioFrame &f,uint8_t *p) {
  memset(p,0,RADIO_FRAME_SIZE);memcpy(p,"DRB2",4);p[4]=RADIO_PROTOCOL_VERSION;p[5]=uint8_t(f.type);p[6]=f.id;p[7]=f.flags;put32(p+8,f.session);put32(p+12,f.sequence);put32(p+16,f.ack_sequence);p[20]=f.power_profile;p[21]=f.tx_power_qdbm;p[22]=f.payload_length>RADIO_PAYLOAD_SIZE?RADIO_PAYLOAD_SIZE:f.payload_length;memcpy(p+24,f.payload,p[22]);uint16_t c=radioCrc16(p,32);p[32]=uint8_t(c);p[33]=uint8_t(c>>8);
}
bool beaconDecode(const uint8_t *p,size_t n,BeaconFrame &o) {
  RadioFrame f{};if(!radioDecode(p,n,f)||f.type!=RadioMessage::Beacon||f.payload_length<3)return false;
  uint16_t mv=uint16_t(f.payload[0]|uint16_t(f.payload[1])<<8);o={f.id,bool(f.payload[2]),f.session,f.sequence,mv};return true;
}
void beaconEncode(const BeaconFrame &b,uint8_t *p) {
  RadioFrame f{};f.type=RadioMessage::Beacon;f.id=b.id;f.session=b.boot_id;f.sequence=b.sequence;f.payload_length=3;f.payload[0]=uint8_t(b.battery_valid?b.battery_mv:0);f.payload[1]=uint8_t((b.battery_valid?b.battery_mv:0)>>8);f.payload[2]=b.battery_valid?1:0;radioEncode(f,p);
}
bool beaconSequenceNewer(uint32_t a,uint32_t b) { return int32_t(a-b)>0; }
