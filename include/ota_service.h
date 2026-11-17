#pragma once
#include <stdint.h>
bool otaServiceStart();
void otaServiceStop();
bool otaServiceActive();
bool otaServiceUpdating();
const char *otaServicePassword();
const char *otaServiceStatus();
