#include "i2c_bus.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
SemaphoreHandle_t s_i2c_mutex = nullptr;
bool s_i2c_initialized = false;
volatile uint32_t s_i2c_error_count = 0;
volatile uint32_t s_i2c_tx_count = 0;
bool s_tca_initialized = false;

void ensureMutexCreated() {
  if (s_i2c_mutex == nullptr) {
    s_i2c_mutex = xSemaphoreCreateRecursiveMutex();
  }
}
} // namespace

bool i2cBusInit() {
  ensureMutexCreated();
  if (!i2cBusLock(500)) {
    return false;
  }

  if (s_i2c_initialized) {
    i2cBusUnlock();
    return true;
  }

  // Expand Wire buffer before begin() for LC76G I2C NMEA blocks
  Wire.setBufferSize(HW_I2C_BUF_SIZE);
  bool ok = Wire.begin(HW_I2C_SDA, HW_I2C_SCL, HW_I2C_FREQ_HZ);
  Wire.setTimeOut(50); // 50 ms timeout to prevent bus lockup
  if (ok) {
    s_i2c_initialized = true;
  } else {
    s_i2c_error_count++;
  }

  i2cBusUnlock();
  return ok;
}

bool i2cBusIsInitialized() {
  return s_i2c_initialized;
}

bool i2cBusLock(uint32_t timeout_ms) {
  ensureMutexCreated();
  if (s_i2c_mutex == nullptr) {
    return false;
  }
  TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
  if (timeout_ms > 0 && ticks == 0) {
    ticks = 1;
  }
  return (xSemaphoreTakeRecursive(s_i2c_mutex, ticks) == pdTRUE);
}

void i2cBusUnlock() {
  if (s_i2c_mutex != nullptr) {
    xSemaphoreGiveRecursive(s_i2c_mutex);
  }
}

bool i2cProbeAddress(uint8_t addr) {
  if (addr < 0x03 || addr > 0x77) {
    return false;
  }
  if (!s_i2c_initialized && !i2cBusInit()) {
    return false;
  }
  if (!i2cBusLock(150)) {
    s_i2c_error_count++;
    return false;
  }

  s_i2c_tx_count++;
  Wire.beginTransmission(addr);
  uint8_t err = Wire.endTransmission(true);
  i2cBusUnlock();
  return (err == 0);
}

void i2cScanBus(bool present_map[128], uint8_t &found_count) {
  found_count = 0;
  for (uint16_t i = 0; i < 128; ++i) {
    present_map[i] = false;
  }
  if (!s_i2c_initialized && !i2cBusInit()) {
    return;
  }
  if (!i2cBusLock(500)) {
    s_i2c_error_count++;
    return;
  }

  for (uint8_t addr = 0x03; addr <= 0x77; ++addr) {
    s_i2c_tx_count++;
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission(true);
    if (err == 0) {
      present_map[addr] = true;
      found_count++;
    }
  }

  i2cBusUnlock();
}

bool i2cReadBytes(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return false;
  }
  if (!s_i2c_initialized && !i2cBusInit()) {
    return false;
  }
  if (!i2cBusLock(150)) {
    s_i2c_error_count++;
    return false;
  }

  s_i2c_tx_count++;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  uint8_t err = Wire.endTransmission(false);
  if (err != 0) {
    // Fallback with stop=true for devices that prefer stop before read
    Wire.beginTransmission(addr);
    Wire.write(reg);
    err = Wire.endTransmission(true);
    if (err != 0) {
      s_i2c_error_count++;
      i2cBusUnlock();
      return false;
    }
  }

  size_t got = Wire.requestFrom((uint16_t)addr, (size_t)len, (bool)true);
  if (got != len) {
    s_i2c_error_count++;
    i2cBusUnlock();
    return false;
  }

  for (size_t i = 0; i < len; ++i) {
    buf[i] = (uint8_t)Wire.read();
  }

  i2cBusUnlock();
  return true;
}

bool i2cWriteBytes(uint8_t addr, uint8_t reg, const uint8_t *buf, size_t len) {
  if (len > 0 && buf == nullptr) {
    return false;
  }
  if (!s_i2c_initialized && !i2cBusInit()) {
    return false;
  }
  if (!i2cBusLock(150)) {
    s_i2c_error_count++;
    return false;
  }

  s_i2c_tx_count++;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (len > 0) {
    Wire.write(buf, len);
  }
  uint8_t err = Wire.endTransmission(true);
  if (err != 0) {
    s_i2c_error_count++;
    i2cBusUnlock();
    return false;
  }

  i2cBusUnlock();
  return true;
}

bool i2cRawWrite(uint8_t addr, const uint8_t *buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return false;
  }
  if (!s_i2c_initialized && !i2cBusInit()) {
    return false;
  }
  if (!i2cBusLock(200)) {
    s_i2c_error_count++;
    return false;
  }

  s_i2c_tx_count++;
  Wire.beginTransmission(addr);
  size_t written = Wire.write(buf, len);
  uint8_t err = Wire.endTransmission(true);
  if (written != len || err != 0) {
    s_i2c_error_count++;
    i2cBusUnlock();
    return false;
  }

  i2cBusUnlock();
  return true;
}

bool i2cRawRead(uint8_t addr, uint8_t *buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return false;
  }
  if (!s_i2c_initialized && !i2cBusInit()) {
    return false;
  }
  if (!i2cBusLock(250)) {
    s_i2c_error_count++;
    return false;
  }

  s_i2c_tx_count++;
  size_t offset = 0;
  const size_t max_chunk = (HW_I2C_BUF_SIZE > 32) ? (size_t)HW_I2C_BUF_SIZE : 256;

  while (offset < len) {
    size_t chunk = len - offset;
    if (chunk > max_chunk) {
      chunk = max_chunk;
    }
    bool send_stop = ((offset + chunk) >= len);
    size_t got = Wire.requestFrom((uint16_t)addr, (size_t)chunk, send_stop);
    if (got != chunk) {
      s_i2c_error_count++;
      i2cBusUnlock();
      return false;
    }
    for (size_t i = 0; i < chunk; ++i) {
      buf[offset + i] = (uint8_t)Wire.read();
    }
    offset += chunk;
  }

  i2cBusUnlock();
  return true;
}

uint32_t i2cGetErrorCount() {
  return s_i2c_error_count;
}

uint32_t i2cGetTransactionCount() {
  return s_i2c_tx_count;
}

void i2cResetErrorCount() {
  s_i2c_error_count = 0;
}

bool tca9554Init() {
  if (!s_i2c_initialized && !i2cBusInit()) {
    return false;
  }
  if (!i2cBusLock(150)) {
    return false;
  }

  uint8_t cfg = 0xFF;
  bool ok = i2cReadBytes(HW_ADDR_TCA9554, TCA9554_REG_CONFIG, &cfg, 1);
  if (ok) {
    // Ensure polarity inversion is disabled (0x00)
    uint8_t pol = 0x00;
    i2cWriteBytes(HW_ADDR_TCA9554, TCA9554_REG_POLARITY, &pol, 1);
    s_tca_initialized = true;
  }

  i2cBusUnlock();
  return ok;
}

bool tca9554SetPinMode(uint8_t pin, uint8_t mode) {
  if (pin > 7) {
    return false;
  }
  if (!s_tca_initialized && !tca9554Init()) {
    return false;
  }
  if (!i2cBusLock(150)) {
    return false;
  }

  uint8_t cfg = 0xFF;
  if (!i2cReadBytes(HW_ADDR_TCA9554, TCA9554_REG_CONFIG, &cfg, 1)) {
    i2cBusUnlock();
    return false;
  }

  // Support both Arduino OUTPUT/INPUT macros and raw TCA9554 0 (output) / 1 (input)
  bool is_output = (mode == OUTPUT || mode == OUTPUT_OPEN_DRAIN || mode == 0);
  if (is_output) {
    cfg &= (uint8_t)~(1U << pin); // 0 = output in TCA9554
  } else {
    cfg |= (uint8_t)(1U << pin);  // 1 = input in TCA9554
  }

  bool ok = i2cWriteBytes(HW_ADDR_TCA9554, TCA9554_REG_CONFIG, &cfg, 1);
  i2cBusUnlock();
  return ok;
}

bool tca9554WritePin(uint8_t pin, uint8_t level) {
  if (pin > 7) {
    return false;
  }
  if (!s_tca_initialized && !tca9554Init()) {
    return false;
  }
  if (!i2cBusLock(150)) {
    return false;
  }

  uint8_t out_reg = 0xFF;
  if (!i2cReadBytes(HW_ADDR_TCA9554, TCA9554_REG_OUTPUT, &out_reg, 1)) {
    i2cBusUnlock();
    return false;
  }

  if (level) {
    out_reg |= (uint8_t)(1U << pin);
  } else {
    out_reg &= (uint8_t)~(1U << pin);
  }

  bool ok = i2cWriteBytes(HW_ADDR_TCA9554, TCA9554_REG_OUTPUT, &out_reg, 1);
  i2cBusUnlock();
  return ok;
}

int tca9554ReadPin(uint8_t pin) {
  if (pin > 7) {
    return -1;
  }
  if (!s_tca_initialized && !tca9554Init()) {
    return -1;
  }
  if (!i2cBusLock(150)) {
    return -1;
  }

  uint8_t in_reg = 0;
  if (!i2cReadBytes(HW_ADDR_TCA9554, TCA9554_REG_INPUT, &in_reg, 1)) {
    i2cBusUnlock();
    return -1;
  }

  i2cBusUnlock();
  return (in_reg & (1U << pin)) ? 1 : 0;
}

bool tca9554ReadAll(uint8_t &inputs, uint8_t &outputs, uint8_t &config) {
  if (!s_tca_initialized && !tca9554Init()) {
    return false;
  }
  if (!i2cBusLock(150)) {
    return false;
  }

  bool ok = i2cReadBytes(HW_ADDR_TCA9554, TCA9554_REG_INPUT, &inputs, 1) &&
            i2cReadBytes(HW_ADDR_TCA9554, TCA9554_REG_OUTPUT, &outputs, 1) &&
            i2cReadBytes(HW_ADDR_TCA9554, TCA9554_REG_CONFIG, &config, 1);

  i2cBusUnlock();
  return ok;
}
