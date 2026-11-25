#pragma once
#include <stdint.h>

enum class RadarOtaState : uint8_t {
  Idle, Ready, Receiving, Cancelled, Timeout, Rejected, Committing, Rebooting,
  BootIncompatible, BusyWifi, Protected, Error
};
void otaServiceBeginBoot();
bool otaServiceBootPending();
void otaServiceFinishBoot(bool healthy);
bool otaServiceStart();
void otaServiceStop();
void otaServiceUpdate();
bool otaServiceActive();
uint32_t otaServiceVersion();
unsigned otaServiceProgress();
RadarOtaState otaServiceState();
const char *otaServicePassword();
const char *otaServiceManifestKey();
const char *otaServiceStatus();
