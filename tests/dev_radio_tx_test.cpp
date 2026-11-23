#include "dev_radio_tx.h"
#include <cassert>

MockSerial Serial;
int mockSends=0,mockError=0,mockAddError=0,mockAdds=0;
uint8_t mockPeer=1;
uint32_t mockMillis=0;

int main() {
  DevRadioTx tx;
  assert(tx.init());
  uint8_t mac[6]={1}, frame[SECURE_RADIO_SIZE]{};
  for (int i=0;i<8;++i) assert(tx.enqueue(mac,frame));
  assert(!tx.enqueue(mac,frame));
  tx.tick(); assert(mockSends==1);
  tx.tick(); assert(mockSends==1);
  tx.completed(ESP_NOW_SEND_SUCCESS);
  tx.completed(ESP_NOW_SEND_FAIL); // second completion cannot clear the next send
  tx.tick(); assert(mockSends==2 && tx.unexpected()==1);
  tx.tick(); assert(mockSends==2);
  tx.completed(ESP_NOW_SEND_FAIL); tx.tick(); assert(mockSends==3);
  tx.completed(ESP_NOW_SEND_SUCCESS); mockError=1; tx.tick(); assert(mockSends==4);
  mockError=0; tx.tick(); assert(mockSends==5);
  mockMillis=3000; tx.tick(); assert(mockSends==5 && tx.stalled());
  tx.completed(ESP_NOW_SEND_SUCCESS); tx.tick(); assert(mockSends==5 && !tx.stalled());

  DevRadioTx bounded;
  assert(bounded.init());
  uint8_t candidate[6]={2}, other[6]={3};
  mockAddError=1;
  assert(bounded.enqueue(candidate,frame)); bounded.tick(); assert(mockAdds==1 && mockSends==5);
  mockAddError=0;
  assert(bounded.enqueue(candidate,frame)); bounded.tick(); assert(mockAdds==2 && mockSends==6);
  assert(bounded.enqueue(other,frame));
  bounded.completed(ESP_NOW_SEND_SUCCESS);
  bounded.tick(); assert(mockAdds==2 && mockSends==6);

  DevRadioTx production;
  assert(production.init(false));
  assert(production.enqueue(other,frame)); production.tick(); assert(mockAdds==2 && mockSends==6);
  assert(production.enqueue(candidate,frame,sizeof(frame))); production.tick(); assert(mockSends==7);
  production.completed(ESP_NOW_SEND_SUCCESS);
  production.tick();
  assert(production.secureDiagnostics().enqueued==1 && production.secureDiagnostics().delivered==1);
  assert(production.enqueue(other,frame,sizeof(frame)));
  production.tick();
  assert(production.secureDiagnostics().missingPeer==1);
  mockError=-42;
  assert(production.enqueue(candidate,frame,sizeof(frame)));
  production.tick();
  assert(production.secureDiagnostics().apiRejected==1 && production.secureDiagnostics().lastApiError==-42);
  mockError=0;
  assert(production.enqueue(candidate,frame,sizeof(frame)));
  production.tick();
  production.completed(ESP_NOW_SEND_FAIL);
  production.tick();
  assert(production.secureDiagnostics().submitted==2 && production.secureDiagnostics().deliveryFailed==1);
  assert(production.enqueue(candidate,frame,sizeof(frame)));
  mockMillis+=1000;
  production.tick();
  assert(production.secureDiagnostics().expired==1);
  for (int i=0;i<8;++i) assert(production.enqueue(candidate,frame,sizeof(frame)));
  assert(!production.enqueue(candidate,frame,sizeof(frame)) && production.secureDiagnostics().queueFull==1);
}
