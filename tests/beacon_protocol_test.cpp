#include "beacon_protocol.h"
#include <cassert>
#include <cstring>
int main() {
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
  for(unsigned i=0;i<256;++i){data[32]=uint8_t(i);data[33]=uint8_t(i>>8);if(radioDecode(data,sizeof(data),decoded))assert(i==radioCrc16(data,32));}
}
