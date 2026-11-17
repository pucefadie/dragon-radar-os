#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <Arduino.h>
#include <Wire.h>
#include <stddef.h>
#include <stdint.h>

#if __has_include("hw_config.h")
#include "hw_config.h"
#endif

#ifndef HW_I2C_SDA
#define HW_I2C_SDA 15
#endif
#ifndef HW_I2C_SCL
#define HW_I2C_SCL 14
#endif
#ifndef HW_I2C_FREQ_HZ
#define HW_I2C_FREQ_HZ 100000UL
#endif
#ifndef HW_I2C_BUF_SIZE
#define HW_I2C_BUF_SIZE 512
#endif
#ifndef HW_ADDR_TCA9554
#define HW_ADDR_TCA9554 0x20
#endif

// TCA9554 Register Map (0x20)
#define TCA9554_REG_INPUT    0x00
#define TCA9554_REG_OUTPUT   0x01
#define TCA9554_REG_POLARITY 0x02
#define TCA9554_REG_CONFIG   0x03

// Shared I2C Bus API (SDA=15, SCL=14, 100kHz, recursive FreeRTOS mutex)
bool i2cBusInit();
bool i2cBusIsInitialized();
bool i2cBusLock(uint32_t timeout_ms = 100);
void i2cBusUnlock();

bool i2cProbeAddress(uint8_t addr);
void i2cScanBus(bool present_map[128], uint8_t &found_count);

bool i2cReadBytes(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len);
bool i2cWriteBytes(uint8_t addr, uint8_t reg, const uint8_t *buf, size_t len);
bool i2cRawWrite(uint8_t addr, const uint8_t *buf, size_t len);
bool i2cRawRead(uint8_t addr, uint8_t *buf, size_t len);

// Diagnostics / error counters for bus stability monitoring (TEST 1, TEST 4, TEST 8)
uint32_t i2cGetErrorCount();
uint32_t i2cGetTransactionCount();
void i2cResetErrorCount();

// TCA9554 (0x20) I/O Expander Helpers on shared Wire bus
// Note: mode accepts Arduino OUTPUT / INPUT constants as well as raw TCA9554 0 (output) / 1 (input).
bool tca9554Init();
bool tca9554SetPinMode(uint8_t pin, uint8_t mode);
bool tca9554WritePin(uint8_t pin, uint8_t level);
int tca9554ReadPin(uint8_t pin);
bool tca9554ReadAll(uint8_t &inputs, uint8_t &outputs, uint8_t &config);

#endif // I2C_BUS_H
