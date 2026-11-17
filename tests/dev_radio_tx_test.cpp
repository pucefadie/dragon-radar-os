#include "dev_radio_tx.h"
#include <cassert>
MockSerial Serial;int mockSends=0;int mockError=0;uint32_t mockMillis=0;
int main(){DevRadioTx tx;assert(tx.init());uint8_t mac[6]={1},frame[RADIO_FRAME_SIZE]={};for(int i=0;i<8;++i)assert(tx.enqueue(mac,frame));assert(!tx.enqueue(mac,frame));tx.tick();assert(mockSends==1);tx.tick();assert(mockSends==1);tx.completed(ESP_NOW_SEND_SUCCESS);tx.tick();assert(mockSends==2);tx.completed(ESP_NOW_SEND_FAIL);tx.tick();assert(mockSends==3);tx.completed(ESP_NOW_SEND_SUCCESS);mockError=1;tx.tick();assert(mockSends==4);mockError=0;tx.tick();assert(mockSends==5);mockMillis=3000;tx.tick();assert(mockSends==5);tx.completed(ESP_NOW_SEND_SUCCESS);tx.tick();assert(mockSends==5);}
