#pragma once
#include <stdint.h>
#include "settings_navigation.h"
void appSettingsNext();
void appSettingsActivate();
void appSettingsTap(int16_t x, int16_t y);
bool appSettingsResetConfirm();
bool appSettingsRestartConfirm();
bool appSettingsConsumeStandby();
bool appSettingsConsumeRestart();
uint8_t appSettingsSelected();
