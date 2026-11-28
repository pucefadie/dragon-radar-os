#pragma once
#include <Arduino.h>
#include "secure_radio.h"
#include "proximity_engine.h"
void beaconServiceInit();
void beaconServiceUpdate();
bool beaconServiceReady();
void beaconServicePrint(Stream &out);
uint8_t beaconServiceFreshCount();
uint8_t beaconServiceChannel();
SecureLinkStatus beaconServiceSecureLinkStatus();
struct SphereGameView {
  uint8_t selected=0, associated=0, found=0, foundCount=0;
};
SphereGameView beaconServiceSphereGame();
bool beaconServiceSelectSphere(uint8_t id);
void beaconServiceNewGame();
ProximitySnapshot beaconServiceProximity();
uint32_t beaconServiceProximityEpoch();
void beaconServiceSetProximitySuspended(bool suspended);
SearchEvent beaconServiceConfirmSphereFound();
void beaconServiceRequestSphereMotor();
void beaconServiceSphereFoundFeedback();
SecureLinkStatus beaconServiceLinkForId(uint8_t id);
uint32_t beaconServiceSphereVersion(uint8_t id);
bool beaconServiceRequestRgb(uint8_t id,uint32_t command);
SecureMotorStatus beaconServiceRgbStatus(uint8_t id);
SecureMotorStatus beaconServiceSphereMotorStatus();
#if M1C_BENCH_FIXTURE
bool beaconServiceFixtureCommand(const char *command, Stream &out);
#endif
