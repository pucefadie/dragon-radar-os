#include "app_settings.h"
#include "settings_manager.h"
#include "display_module.h"
#include "sensors_module.h"
#include "wifi_service.h"
#include "sd_service.h"
#include "ota_service.h"
#include "peripherals_module.h"
namespace { uint8_t selection = 0; bool resetConfirm = false, restartConfirm = false, standbyRequested = false, restartRequested = false; }
void appSettingsNext() { selection = settingsNextPage(selection); resetConfirm = false; restartConfirm = false; }
bool appSettingsResetConfirm() { return resetConfirm; }
bool appSettingsRestartConfirm() { return restartConfirm; }
bool appSettingsConsumeStandby() { bool requested=standbyRequested; standbyRequested=false; return requested; }
bool appSettingsConsumeRestart() { bool requested=restartRequested; restartRequested=false; return requested; }
uint8_t appSettingsSelected() { return selection; }
void appSettingsActivate() {
  OsSettings &s = settingsGet();
  switch (selection) {
    case 0: s.brightness = s.brightness >= 225 ? 50 : s.brightness + 35; displaySetBrightness(s.brightness); break;
    case 1: s.volume = (s.volume + 25) % 125; break;
    case 2: s.vibration = !s.vibration; if (!s.vibration) motorStop(); break;
    case 3: s.radar_mode = nextRadarMode(s.radar_mode); break;
    case 4: s.heading_up = !s.heading_up; break;
    case 5: if (magCalIsInProgress()) magCalStop(true); else magCalStart(); return;
    case 6: wifiServiceBeginPortal(); return;
    case 7: s.mount_offset = s.mount_offset >= 165 ? -180 : s.mount_offset + 15; break;
    case 8: s.declination = s.declination >= 30 ? -30 : s.declination + 1; break;
    case 9: return;
    case 11: s.sound = !s.sound; break;
    case 12: if (!otaServiceActive()) standbyRequested = true; return;
    case 13: if (!restartConfirm) { restartConfirm = true; return; } restartConfirm = false; restartRequested = true; return;
    case 14: return;
    case 15: sdServiceRequestCheck(); return;
    case 10: if (!resetConfirm) { resetConfirm = true; return; } resetConfirm = false; s = OsSettings(); displaySetBrightness(s.brightness); break;
  }
  settingsChanged();
}
void appSettingsTap(int16_t x, int16_t y) {
  switch (settingsTouchAt(x, y)) {
    case SettingsTouch::Previous: selection = settingsPreviousPage(selection); resetConfirm = false; restartConfirm = false; break;
    case SettingsTouch::Next: appSettingsNext(); break;
    case SettingsTouch::Activate: appSettingsActivate(); break;
    default: break;
  }
}
