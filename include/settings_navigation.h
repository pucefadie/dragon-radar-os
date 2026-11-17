#pragma once
#include <stdint.h>
constexpr uint8_t SETTINGS_PAGE_COUNT = 16;
enum class SettingsTouch : uint8_t { None, Previous, Next, Activate };
inline uint8_t settingsNextPage(uint8_t page) { return (page + 1) % SETTINGS_PAGE_COUNT; }
inline uint8_t settingsPreviousPage(uint8_t page) { return (page + SETTINGS_PAGE_COUNT - 1) % SETTINGS_PAGE_COUNT; }
inline SettingsTouch settingsTouchAt(int16_t x, int16_t y) {
  if (y < 160 || y >= 315 || x < 30 || x >= 436) return SettingsTouch::None;
  if (x < 110) return SettingsTouch::Previous;
  if (x >= 356) return SettingsTouch::Next;
  return SettingsTouch::Activate;
}
