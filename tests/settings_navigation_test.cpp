#include "settings_navigation.h"
#include <cassert>
int main() {
  assert(settingsNextPage(15) == 0);
  assert(settingsPreviousPage(0) == 15);
  assert(settingsTouchAt(70, 210) == SettingsTouch::Previous);
  assert(settingsTouchAt(395, 210) == SettingsTouch::Next);
  assert(settingsTouchAt(233, 210) == SettingsTouch::Activate);
  assert(settingsTouchAt(233, 430) == SettingsTouch::None);
  assert(settingsTouchAt(20, 210) == SettingsTouch::None);
}
