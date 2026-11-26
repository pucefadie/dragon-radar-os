#pragma once

#include <driver/gpio.h>

constexpr gpio_num_t kSphereMotorPin = GPIO_NUM_22;

inline bool sphereMotorSafeLow() {
  // Set the output latch before enabling output. Keep the input biased LOW.
  const esp_err_t level = gpio_set_level(kSphereMotorPin, 0);
  gpio_config_t config{};
  config.pin_bit_mask = 1ULL << kSphereMotorPin;
  config.mode = GPIO_MODE_OUTPUT;
  config.pull_down_en = GPIO_PULLDOWN_ENABLE;
  const esp_err_t pin = gpio_config(&config);
  return level == ESP_OK && pin == ESP_OK;
}
