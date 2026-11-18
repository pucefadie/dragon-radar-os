#include "beacon_protocol.h"
#include <cassert>
#include <cstring>
int main() {
  // DRB2 before DIRECTED was introduced, and the current directed wire layout.
  constexpr uint8_t broadcast[34]={
    0x44,0x52,0x42,0x32,2,1,1,0,4,3,2,1,0x44,0x33,0x22,0x11,
    0,0,0,0,0,0,3,0,0x74,0x0e,1,0,0,0,0,0,0x8e,0x74};
  constexpr uint8_t directed[34]={
    0x44,0x52,0x42,0x32,2,9,0,3,4,3,2,1,0x44,0x33,0x22,0x11,
    0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0xe3,0x40};
  uint8_t encoded[34];
  beaconEncode(BeaconFrame{1,true,0x01020304,0x11223344,3700},encoded);
  assert(!std::memcmp(broadcast,encoded,sizeof(encoded)));
  RadioFrame golden{};
  assert(radioDecode(broadcast,sizeof(broadcast),golden));
  assert(golden.destination==RADIO_ID_BROADCAST);
  assert(radioDecode(directed,sizeof(directed),golden));
  assert(golden.id==0 && golden.destination==1 && golden.flags==3 && golden.payload_length==1);
  radioEncode(golden,encoded);
  assert(!std::memcmp(directed,encoded,sizeof(encoded)));
  uint8_t data[BEACON_FRAME_SIZE]; BeaconFrame in={7,true,0x12345678,0xfedcba98,3700}, out={};
  beaconEncode(in,data); assert(beaconDecode(data,sizeof(data),out));
  assert(out.id==7 && out.boot_id==in.boot_id && out.sequence==in.sequence && out.battery_mv==3700 && out.battery_valid);
  assert(!beaconDecode(data,sizeof(data)-1,out));
  data[5]=0; assert(!beaconDecode(data,sizeof(data),out)); data[5]=1;
  data[6]=2; assert(!beaconDecode(data,sizeof(data),out)); data[6]=0;
  assert(!beaconDecode(data,sizeof(data),out));
  in.battery_valid=false; beaconEncode(in,data); assert(beaconDecode(data,sizeof(data),out) && !out.battery_valid && !out.battery_mv);
  assert(beaconSequenceNewer(1,0)); assert(!beaconSequenceNewer(4,4)); assert(!beaconSequenceNewer(4,5)); assert(beaconSequenceNewer(0,0xffffffff));
  RadioFrame typed{}; typed.type=RadioMessage::Command;typed.id=3;typed.session=0x87654321;typed.sequence=9;typed.ack_sequence=8;typed.payload_length=2;typed.payload[0]=uint8_t(RadioCommand::Vibrate);typed.payload[1]=1;
  radioEncode(typed,data); RadioFrame decoded{};assert(radioDecode(data,sizeof(data),decoded));assert(decoded.type==typed.type&&decoded.id==3&&decoded.session==typed.session&&decoded.sequence==9&&decoded.ack_sequence==8&&decoded.payload_length==2&&decoded.payload[0]==typed.payload[0]);
  data[24]^=1;assert(!radioDecode(data,sizeof(data),decoded));data[24]^=1;
  data[4]=9;assert(!radioDecode(data,sizeof(data),decoded));
  typed.type=RadioMessage::Discovery;typed.id=1;typed.payload_length=0;radioEncode(typed,data);assert(radioDecode(data,sizeof(data),decoded));
  typed.type=RadioMessage::Command;typed.id=0;typed.destination=7;typed.flags=RADIO_FLAG_DIRECTED|RADIO_FLAG_ACK_REQUIRED;typed.payload_length=1;typed.payload[0]=2;radioEncode(typed,data);assert(radioDecode(data,sizeof(data),decoded)&&decoded.id==0&&decoded.destination==7&&(decoded.flags&RADIO_FLAG_ACK_REQUIRED));
  typed.destination=255;radioEncode(typed,data);assert(!radioDecode(data,sizeof(data),decoded));
  typed.destination=1;typed.id=8;radioEncode(typed,data);assert(!radioDecode(data,sizeof(data),decoded));
  for(unsigned i=0;i<256;++i){data[32]=uint8_t(i);data[33]=uint8_t(i>>8);if(radioDecode(data,sizeof(data),decoded))assert(i==radioCrc16(data,32));}
}
