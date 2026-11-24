#pragma once
#include <Arduino.h>
#include "secure_radio.h"
#include "proximity_engine.h"
void beaconServiceInit();
void beaconServiceUpdate();
void beaconServicePrint(Stream &out);
uint8_t beaconServiceFreshCount();
uint8_t beaconServiceChannel();
SecureLinkStatus beaconServiceSecureLinkStatus();
ProximitySnapshot beaconServiceProximity();
void beaconServiceSetProximitySuspended(bool suspended);
SearchEvent beaconServiceConfirmOrRestartSearch();
void beaconServiceRequestSphereMotor();
SecureMotorStatus beaconServiceSphereMotorStatus();
#if M1C_BENCH_FIXTURE
bool beaconServiceFixtureCommand(const char *command, Stream &out);
#endif
