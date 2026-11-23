#pragma once
#include <Arduino.h>
void beaconServiceInit();
void beaconServiceUpdate();
void beaconServicePrint(Stream &out);
uint8_t beaconServiceFreshCount();
uint8_t beaconServiceChannel();
#if M1C_BENCH_FIXTURE
bool beaconServiceFixtureCommand(const char *command, Stream &out);
#endif
