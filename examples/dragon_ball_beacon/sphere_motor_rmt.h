#pragma once

#include <driver/rmt_tx.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>
#include "sphere_motor.h"
#include "sphere_motor_gpio.h"

class SphereMotorRmt {
 public:
  bool begin() {
    if (!sphereMotorSafeLow()) return false;
    rmt_tx_channel_config_t config{};
    config.gpio_num = kSphereMotorPin;
    config.clk_src = RMT_CLK_SRC_DEFAULT;
    config.resolution_hz = kResolutionHz;
    config.mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL;
    config.trans_queue_depth = 1;
    config.flags.init_level = 0;
    if (rmt_new_tx_channel(&config, &channel_) != ESP_OK) return forceLow();
    rmt_copy_encoder_config_t encoder{};
    if (rmt_new_copy_encoder(&encoder, &encoder_) != ESP_OK) {
      rmt_del_channel(channel_);
      channel_ = nullptr;
      return forceLow();
    }
    return true;
  }

  bool start(uint16_t durationMs) {
    const uint32_t halfTicks = durationMs * (kResolutionHz / 1000) / 2;
    if (durationMs == 0 || durationMs > kMotorMaxPulseMs || halfTicks > 32767) return false;
    pulse_[0].duration0 = halfTicks;
    pulse_[0].duration1 = halfTicks;
    if (rmt_enable(channel_) != ESP_OK) return forceLow();
    running_ = true;
    rmt_transmit_config_t config{};
    config.flags.eot_level = 0;
    config.flags.queue_nonblocking = 1;
    return rmt_transmit(channel_, encoder_, pulse_, sizeof(pulse_), &config) == ESP_OK;
  }

  bool stop() {
    if (!running_) return true;
    running_ = false;
    return rmt_disable(channel_) == ESP_OK || forceLow();
  }

 private:
  static bool forceLow() {
    // Disconnect RMT from the pad if the peripheral reports an error.
    gpio_set_level(kSphereMotorPin, 0);
    esp_rom_gpio_connect_out_signal(kSphereMotorPin, SIG_GPIO_OUT_IDX, false, false);
    sphereMotorSafeLow();
    return false;
  }

  static constexpr uint32_t kResolutionHz = 500000;
  static_assert(kMotorPulseMs * (kResolutionHz / 1000) / 2 <= 32767,
                "Motor pulse does not fit RMT symbols");
  // RMT finishes this one-shot in hardware even if USB or the loop stop responding.
  rmt_symbol_word_t pulse_[2] = {
    {.duration0 = 0, .level0 = 1, .duration1 = 0, .level1 = 1},
    {.duration0 = 1, .level0 = 0, .duration1 = 1, .level1 = 0}
  };
  rmt_channel_handle_t channel_ = nullptr;
  rmt_encoder_handle_t encoder_ = nullptr;
  bool running_ = false;
};
