#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_chip_info.h>

#include "hw_config.h"
#include "i2c_bus.h"
#include "peripherals_module.h"
#include "display_module.h"
#include "sensors_module.h"
#include "gps_module.h"
#include "app_manager.h"
#include "standby_button.h"
#include "app_radar.h"
#include "settings_manager.h"
#include "app_settings.h"
#include "wifi_service.h"
#include "beacon_service.h"
#include "sd_service.h"
#include "ota_service.h"
#include "audio_service.h"
#include "audio_codec_probe.h"
#include "audiobookshelf_client.h"
#include "gesture_manager.h"

namespace {

bool g_debugLegacy = false;
bool g_standby = false;
StandbyButtonGate g_standbyButton;
bool g_mag_stream = false;
bool g_abs_password_pending = false;
bool g_abs_password_overflow = false;
char g_abs_password[128] = {};
size_t g_abs_password_length = 0;
AppManager g_apps;
GestureManager g_gestures;
bool g_i2c_present_map[128] = {false};
uint8_t g_i2c_found_count = 0;
bool g_tca_online = false;
uint8_t g_tca_in = 0xFF;
uint8_t g_tca_out = 0xFF;
uint8_t g_tca_cfg = 0xFF;

uint8_t g_active_page = RADAR_PAGE_MAIN_COMPASS;
bool g_touch_pressed = false;
int16_t g_touch_x = 0;
int16_t g_touch_y = 0;

bool g_test0_pass = false;
bool g_test1_onboard_pass = false;
bool g_test2_pass = false;

const char *resetReasonToString(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_UNKNOWN:   return "ESP_RST_UNKNOWN";
    case ESP_RST_POWERON:   return "ESP_RST_POWERON (Power-on / USB reset)";
    case ESP_RST_EXT:       return "ESP_RST_EXT (External pin)";
    case ESP_RST_SW:        return "ESP_RST_SW (Software esp_restart)";
    case ESP_RST_PANIC:     return "ESP_RST_PANIC (Exception/panic)";
    case ESP_RST_INT_WDT:   return "ESP_RST_INT_WDT (Interrupt watchdog)";
    case ESP_RST_TASK_WDT:  return "ESP_RST_TASK_WDT (Task watchdog)";
    case ESP_RST_WDT:       return "ESP_RST_WDT (Other watchdog)";
    case ESP_RST_DEEPSLEEP: return "ESP_RST_DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "ESP_RST_BROWNOUT (Brownout reset!)";
    case ESP_RST_SDIO:      return "ESP_RST_SDIO";
    default:                return "ESP_RST_OTHER";
  }
}

const char *flashModeToString(FlashMode_t mode) {
  switch (mode) {
    case FM_QIO:       return "QIO";
    case FM_QOUT:      return "QOUT";
    case FM_DIO:       return "DIO";
    case FM_DOUT:      return "DOUT";
    case FM_FAST_READ: return "FAST_READ";
    case FM_SLOW_READ: return "SLOW_READ";
    default:           return "UNKNOWN";
  }
}

void printHexMatrix(const bool present_map[128]) {
  Serial.println("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");
  for (uint8_t row = 0; row < 8; ++row) {
    Serial.printf("%02x: ", row * 16);
    for (uint8_t col = 0; col < 16; ++col) {
      uint8_t addr = row * 16 + col;
      if (addr < 0x03 || addr > 0x77) {
        Serial.print("   ");
      } else if (present_map[addr]) {
        Serial.printf("%02X ", addr);
      } else {
        Serial.print("-- ");
      }
    }
    Serial.println();
  }
}

// ============================================================================
// TEST 0: Power, Boot, Memory & Serial (USB serial)
// ============================================================================
void runTest0() {
  Serial.println();
  Serial.println("========================================================================");
  Serial.println("[TEST 0] ALIMENTAZIONE, BOOT, MEMORIA & SERIALE (USB-CDC USB serial)");
  Serial.println("========================================================================");

  int motor_level = digitalRead(HW_MOTOR_PIN);
  int btn_level = digitalRead(HW_BUTTON_PIN);
  int boot_level = digitalRead(HW_BOOT_BTN_PIN);

  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  esp_reset_reason_t rst = esp_reset_reason();

  uint32_t flash_size = ESP.getFlashChipSize();
  uint32_t psram_size = ESP.getPsramSize();

  Serial.printf("  MCU Model          : %s (Revision v%d.%d, Cores: %d)\n",
                ESP.getChipModel(), chip_info.revision / 100, chip_info.revision % 100,
                ESP.getChipCores());
  Serial.printf("  CPU / XTAL Freq    : %u MHz / %u MHz\n",
                (unsigned)ESP.getCpuFreqMHz(), (unsigned)getXtalFrequencyMhz());
  Serial.printf("  Flash Memory       : %u bytes (%.1f MB) @ %u MHz, Mode: %s\n",
                (unsigned)flash_size, flash_size / (1024.0f * 1024.0f),
                (unsigned)(ESP.getFlashChipSpeed() / 1000000UL),
                flashModeToString(ESP.getFlashChipMode()));
  Serial.printf("  PSRAM (OPI)        : %u bytes (%.2f MB), Free PSRAM: %u bytes\n",
                (unsigned)psram_size, psram_size / (1024.0f * 1024.0f),
                (unsigned)ESP.getFreePsram());
  Serial.printf("  Internal SRAM Heap : Total %u bytes, Free %u bytes\n",
                (unsigned)ESP.getHeapSize(), (unsigned)ESP.getFreeHeap());
  Serial.printf("  ESP-IDF / SDK Ver  : %s\n", ESP.getSdkVersion());
  Serial.printf("  Last Reset Reason  : %d -> %s\n", (int)rst, resetReasonToString(rst));
  Serial.printf("  Uptime             : %lu ms\n", (unsigned long)millis());
  Serial.printf("  Radio State        : Wi-Fi=OFF, BLE=OFF (disabled for clean power/EMI baseline)\n");
  Serial.printf("  GPIO16 (Motor SIG) : %s (level=%d, expected LOW=0 at boot)\n",
                (motor_level == LOW) ? "SAFE LOW (OFF)" : "WARNING HIGH!", motor_level);
  Serial.printf("  GPIO44 (Ext Btn)   : %s (level=%d, INPUT_PULLUP)\n",
                (btn_level == HIGH) ? "RELEASED (HIGH)" : "PRESSED (LOW)", btn_level);
  Serial.printf("  GPIO0  (BOOT Btn)  : %s (level=%d, INPUT_PULLUP)\n",
                (boot_level == HIGH) ? "RELEASED (HIGH)" : "PRESSED (LOW)", boot_level);

  g_test0_pass = (flash_size == 16UL * 1024UL * 1024UL) &&
                 (psram_size >= 4UL * 1024UL * 1024UL) &&
                 (motor_level == LOW) &&
                 (rst != ESP_RST_BROWNOUT) &&
                 (rst != ESP_RST_PANIC);

  Serial.printf("[TEST 0 RESULT] %s (Flash16M=%s, PSRAM8M=%s, MotorSafeLow=%s, NoBrownout=%s)\n",
                g_test0_pass ? "PASS" : "FAIL",
                (flash_size == 16UL * 1024UL * 1024UL) ? "OK" : "ERR",
                (psram_size >= 4UL * 1024UL * 1024UL) ? "OK" : "ERR",
                (motor_level == LOW) ? "OK" : "ERR",
                (rst != ESP_RST_BROWNOUT) ? "OK" : "ERR");
}

// ============================================================================
// TEST 1: Shared I2C Bus Scanner & Per-Device Verification
// ============================================================================
void runTest1(bool pulse_resets = true) {
  Serial.println();
  Serial.println("========================================================================");
  Serial.println("[TEST 1] SCANNER BUS I2C CONDIVISO (SDA=GPIO15, SCL=GPIO14 @ 100 kHz)");
  Serial.println("========================================================================");

  bool bus_ok = i2cBusInit();
  Serial.printf("  I2C Bus Init       : %s (SDA=%d, SCL=%d, Freq=%u Hz, BufSize=%d)\n",
                bus_ok ? "OK" : "FAIL", HW_I2C_SDA, HW_I2C_SCL,
                (unsigned)HW_I2C_FREQ_HZ, HW_I2C_BUF_SIZE);

  bool map_initial[128] = {false};
  uint8_t count_initial = 0;
  i2cScanBus(map_initial, count_initial);
  Serial.printf("\n--- [1A] Initial I2C Scan: %u devices found ---\n", count_initial);
  printHexMatrix(map_initial);

  g_tca_online = tca9554ReadAll(g_tca_in, g_tca_out, g_tca_cfg);
  Serial.println("\n--- [1B] TCA9554 (0x20) I/O Expander Register Dump ---");
  if (g_tca_online) {
    Serial.printf("  REG_INPUT  (0x00) = 0x%02X | REG_OUTPUT (0x01) = 0x%02X | REG_CONFIG (0x03) = 0x%02X\n",
                  g_tca_in, g_tca_out, g_tca_cfg);
    Serial.printf("  Decoded Pins: EXIO0=%d EXIO1=%d EXIO2=%d | RTC_INT(P3)=%d | PWR_BTN(P4)=%d | AXP_IRQ(P5)=%d | QMI_INT1(P6)=%d | GPS_RST(P7)=%d\n",
                  (g_tca_in >> 0) & 1, (g_tca_in >> 1) & 1, (g_tca_in >> 2) & 1,
                  (g_tca_in >> 3) & 1, (g_tca_in >> 4) & 1, (g_tca_in >> 5) & 1,
                  (g_tca_in >> 6) & 1, (g_tca_in >> 7) & 1);
  } else {
    Serial.println("  TCA9554 (0x20) READ FAILED!");
  }

  bool cst_after_rst = map_initial[HW_ADDR_CST9217];
  if (pulse_resets && !displayIsInitialized()) {
    Serial.println("\n--- [1C] Touch CST9217 (0x5A) Hardware Reset Sequence (TP_RESET=GPIO40) ---");
    pinMode(HW_TP_INT, INPUT_PULLUP);
    pinMode(HW_TP_RESET, OUTPUT);
    digitalWrite(HW_TP_RESET, LOW);
    delay(15);
    digitalWrite(HW_TP_RESET, HIGH);
    delay(50);
    cst_after_rst = i2cProbeAddress(HW_ADDR_CST9217);
    Serial.printf("  CST9217 (0x5A) before reset: %s | after GPIO40 reset pulse: %s | TP_INT(GPIO11)=%d\n",
                  map_initial[HW_ADDR_CST9217] ? "ACK" : "NACK",
                  cst_after_rst ? "ACK" : "NACK",
                  digitalRead(HW_TP_INT));
  }

  if (pulse_resets && g_tca_online) {
    Serial.println("\n--- [1D] GNSS LC76G (0x50/0x54) & TCA9554 EXIO7 (GPS_RST) Sequence ---");
    // Test 8-byte LC76G handshake BEFORE touching EXIO7
    const uint8_t lc76g_cmd[8] = {0x08, 0x00, 0x51, 0xAA, 0x04, 0x00, 0x00, 0x00};
    uint8_t lc76g_len_buf[4] = {0, 0, 0, 0};
    bool pre_cmd_ok = i2cRawWrite(HW_ADDR_LC76G_W, lc76g_cmd, sizeof(lc76g_cmd));
    delay(90);
    bool pre_rd_ok = pre_cmd_ok && i2cRawRead(HW_ADDR_LC76G_R, lc76g_len_buf, sizeof(lc76g_len_buf));
    uint32_t pre_avail = pre_rd_ok ? ((uint32_t)lc76g_len_buf[0] | ((uint32_t)lc76g_len_buf[1] << 8) |
                                      ((uint32_t)lc76g_len_buf[2] << 16) | ((uint32_t)lc76g_len_buf[3] << 24)) : 0;
    Serial.printf("  Before EXIO7 pulse : probe(0x50)=%s | Handshake write(0x50)=%s, read(0x54)=%s, avail=%u B\n",
                  map_initial[HW_ADDR_LC76G_W] ? "ACK" : "NACK",
                  pre_cmd_ok ? "ACK" : "NACK", pre_rd_ok ? "ACK" : "NACK", (unsigned)pre_avail);

    tca9554SetPinMode(HW_EXIO_GPS_RST, OUTPUT);
    tca9554WritePin(HW_EXIO_GPS_RST, LOW);
    delay(50);
    bool gps_50_low = i2cProbeAddress(HW_ADDR_LC76G_W);
    tca9554WritePin(HW_EXIO_GPS_RST, HIGH);
    // Wait up to 1200 ms for LC76G internal MCU to complete boot after EXIO7 reset release
    bool gps_50_high = false;
    uint32_t t_boot = millis();
    while (millis() - t_boot < 1300) {
      delay(100);
      if (i2cProbeAddress(HW_ADDR_LC76G_W)) {
        gps_50_high = true;
        break;
      }
    }
    Serial.printf("  With EXIO7=LOW     : probe(0x50)=%s\n", gps_50_low ? "ACK" : "NACK");
    Serial.printf("  After EXIO7=HIGH   : probe(0x50)=%s (boot time ~%lu ms)\n",
                  gps_50_high ? "ACK" : "NACK", (unsigned long)(millis() - t_boot));
  }

  i2cScanBus(g_i2c_present_map, g_i2c_found_count);
  Serial.printf("\n--- [1E] Post-Reset I2C Scan: %u devices found ---\n", g_i2c_found_count);
  printHexMatrix(g_i2c_present_map);

  Serial.println("\n--- [1F] Per-Device Verification & Chip ID Register Check ---");
  uint8_t bmm_addr = 0;
  if (g_i2c_present_map[HW_ADDR_BMM150]) bmm_addr = HW_ADDR_BMM150;
  else if (g_i2c_present_map[HW_ADDR_BMM150_ALT]) bmm_addr = HW_ADDR_BMM150_ALT;
  if (bmm_addr != 0) {
    uint8_t pwr_bit = 0x01;
    i2cWriteBytes(bmm_addr, 0x4B, &pwr_bit, 1);
    delay(5);
    uint8_t bmm_id = 0;
    bool id_ok = i2cReadBytes(bmm_addr, HW_BMM150_CHIP_ID_REG, &bmm_id, 1);
    Serial.printf("  [0x%02X] BMM150 Compass (External TP2/TP3) : FOUND | ChipID(0x40)=0x%02X (expected 0x32) -> %s\n",
                  bmm_addr, bmm_id, (id_ok && bmm_id == HW_BMM150_CHIP_ID) ? "VERIFIED" : "ID_MISMATCH");
  } else {
    Serial.println("  [0x13/0x11] BMM150 Compass (External TP2/TP3) : NOT FOUND (NACK on 0x13 and 0x11)");
  }

  Serial.printf("  [0x18] ES8311 Audio Codec (Onboard)      : %s\n", g_i2c_present_map[HW_ADDR_ES8311] ? "FOUND" : "MISSING");
  Serial.printf("  [0x20] TCA9554 I/O Expander (Onboard)    : %s | IN=0x%02X OUT=0x%02X CFG=0x%02X\n",
                g_i2c_present_map[HW_ADDR_TCA9554] ? "FOUND" : "MISSING", g_tca_in, g_tca_out, g_tca_cfg);
  Serial.printf("  [0x34] AXP2101 PMIC (Onboard)            : %s\n", g_i2c_present_map[HW_ADDR_AXP2101] ? "FOUND" : "MISSING");
  Serial.printf("  [0x40] ES7210 Audio ADC (Onboard)        : %s\n", g_i2c_present_map[HW_ADDR_ES7210] ? "FOUND" : "MISSING");
  Serial.printf("  [0x50] LC76G GNSS Write Addr (Onboard)   : %s\n", g_i2c_present_map[HW_ADDR_LC76G_W] ? "FOUND" : "NACK");
  Serial.printf("  [0x54] LC76G GNSS Read Addr  (Onboard)   : %s\n", g_i2c_present_map[HW_ADDR_LC76G_R] ? "FOUND" : "NACK");
  Serial.printf("  [0x51] PCF85063 RTC (Onboard)            : %s\n", g_i2c_present_map[HW_ADDR_PCF85063] ? "FOUND" : "MISSING");
  Serial.printf("  [0x5A] CST9217 Touch Controller          : %s\n",
                (g_i2c_present_map[HW_ADDR_CST9217] || cst_after_rst) ? "FOUND" : "MISSING");
  Serial.printf("  [0x6B] QMI8658 6-Axis IMU (Onboard)      : %s\n", g_i2c_present_map[HW_ADDR_QMI8658] ? "FOUND" : "MISSING");

  g_test1_onboard_pass = g_i2c_present_map[HW_ADDR_ES8311] &&
                         g_i2c_present_map[HW_ADDR_TCA9554] &&
                         g_i2c_present_map[HW_ADDR_AXP2101] &&
                         g_i2c_present_map[HW_ADDR_ES7210] &&
                         g_i2c_present_map[HW_ADDR_PCF85063] &&
                         (g_i2c_present_map[HW_ADDR_CST9217] || cst_after_rst) &&
                         g_i2c_present_map[HW_ADDR_QMI8658];

  Serial.printf("[TEST 1 SUMMARY] Onboard I2C Core Devices=%s | BMM150(0x13/0x11)=%s | LC76G(0x50/0x54)=%s\n",
                g_test1_onboard_pass ? "ALL PRESENT" : "PARTIAL",
                (bmm_addr != 0) ? "PRESENT" : "NOT DETECTED",
                (g_i2c_present_map[HW_ADDR_LC76G_W] && g_i2c_present_map[HW_ADDR_LC76G_R]) ? "PRESENT" : "NACK");
  Serial.println("========================================================================");
}

// ============================================================================
// TEST 2: Display QSPI CO5300 (466x466) & Touch CST9217 (0x5A)
// ============================================================================
void runTest2() {
  Serial.println();
  Serial.println("========================================================================");
  Serial.println("[TEST 2] DISPLAY AMOLED QSPI CO5300 (466x466) & TOUCH CST9217 (0x5A)");
  Serial.println("========================================================================");

  if (!displayIsInitialized()) {
    displayInit(true);
  }
  g_test2_pass = displayRunColorPatternTest(350);
  displayPrintDiagnostics(Serial);
  Serial.printf("[TEST 2 RESULT] QSPI Framebuffer & Pattern Execution=%s (Visual check: PENDING MANUAL TEST)\n",
                g_test2_pass ? "PASS (HW/SW)" : "FAIL");
  Serial.println("========================================================================");
}

// ============================================================================
// TEST 3: External Button GPIO44 & Onboard Buttons (BOOT GPIO0, PWR EXIO4)
// ============================================================================
void runTest3() {
  Serial.println();
  Serial.println("========================================================================");
  Serial.println("[TEST 3] PULSANTE ESTERNO SU GPIO44 (INPUT_PULLUP) & PULSANTI ONBOARD");
  Serial.println("========================================================================");

  if (displayIsInitialized()) {
    displayShowTestBanner("TEST 3: BUTTON GPIO44",
                          "INPUT_PULLUP + Debounce 40ms",
                          "Sampling rest stability (1000 ms)...",
                          RADAR_COLOR_CYAN);
  }

  uint32_t t0 = millis();
  uint32_t initial_presses = buttonGetStatus().press_count;
  while (millis() - t0 < 1000) {
    peripheralsUpdate();
    delay(10);
  }
  peripheralsPrintButtonDiagnostics(Serial);
  uint32_t delta_presses = buttonGetStatus().press_count - initial_presses;
  Serial.printf("[TEST 3 RESULT] Electrical Pullup & Rest Stability=%s (spurious_events=%lu) | Manual Press=PENDING MANUAL TEST\n",
                (buttonGetStatus().raw_level == HIGH && delta_presses == 0) ? "PASS" : "CHECK",
                (unsigned long)delta_presses);
  Serial.println("========================================================================");
}

// ============================================================================
// TEST 4: Grove Vibration Motor V1.3 on GPIO16
// ============================================================================
void runTest4() {
  Serial.println();
  Serial.println("========================================================================");
  Serial.println("[TEST 4] MOTORINO DI VIBRAZIONE GROVE V1.3 SU GPIO16");
  Serial.println("========================================================================");

  if (displayIsInitialized()) {
    displayShowTestBanner("TEST 4: MOTOR GPIO16",
                          "100ms / 250ms / 3x100ms Patterns",
                          "Monitoring VSYS, VBUS & I2C...",
                          RADAR_COLOR_AMBER);
  }

  // Pattern 1: Short pulse 100 ms
  Serial.println("  -> Executing Pattern 1: Short Pulse (100 ms ON)...");
  motorTriggerShortPulse();
  uint32_t t0 = millis();
  while (millis() - t0 < 320) {
    peripheralsUpdate();
    delay(10);
  }
  const MotorStatus &m1 = motorGetStatus();
  Serial.printf("     Pattern 1 Done: GPIO16=%d | VSYS before=%u mV, min=%u mV, drop=%d mV | I2C err delta=%lu\n",
                digitalRead(HW_MOTOR_PIN), m1.vsys_before_mv, m1.vsys_min_during_mv,
                (int)m1.vsys_max_drop_mv, (unsigned long)m1.i2c_errors_delta);

  // Pattern 2: Medium pulse 250 ms
  Serial.println("  -> Executing Pattern 2: Medium Pulse (250 ms ON)...");
  motorTriggerMediumPulse();
  t0 = millis();
  while (millis() - t0 < 480) {
    peripheralsUpdate();
    delay(10);
  }
  const MotorStatus &m2 = motorGetStatus();
  Serial.printf("     Pattern 2 Done: GPIO16=%d | VSYS before=%u mV, min=%u mV, drop=%d mV | I2C err delta=%lu\n",
                digitalRead(HW_MOTOR_PIN), m2.vsys_before_mv, m2.vsys_min_during_mv,
                (int)m2.vsys_max_drop_mv, (unsigned long)m2.i2c_errors_delta);

  // Pattern 3: Triple pulse 3 x (100 ms ON / 120 ms OFF)
  Serial.println("  -> Executing Pattern 3: Triple Pulse (3 x 100 ms ON / 120 ms OFF)...");
  motorTriggerTriplePulse();
  t0 = millis();
  while (millis() - t0 < 950) {
    peripheralsUpdate();
    delay(10);
  }
  motorStop();
  peripheralsUpdate();
  peripheralsPrintMotorDiagnostics(Serial);

  const MotorStatus &m3 = motorGetStatus();
  bool test4_hw_ok = (digitalRead(HW_MOTOR_PIN) == LOW) &&
                     !m3.voltage_dip_anomaly &&
                     !m3.i2c_error_anomaly &&
                     (m3.watchdog_trip_count == 0);
  Serial.printf("[TEST 4 RESULT] Electrical Stability & Safe-Off=%s | Tactile Confirmation=PENDING MANUAL TEST\n",
                test4_hw_ok ? "PASS" : "WARN");
  Serial.println("========================================================================");
}

// ============================================================================
// TEST 5: External 3-Axis Compass Seeed Grove BMM150 (0x13 / 0x11)
// ============================================================================
void runTest5() {
  Serial.println();
  Serial.println("========================================================================");
  Serial.println("[TEST 5] BUSSOLA MAGNETICA 3 ASSI GROVE BMM150 (0x13 / fallback 0x11)");
  Serial.println("========================================================================");

  if (displayIsInitialized()) {
    displayShowTestBanner("TEST 5: COMPASS BMM150",
                          "Addr 0x13/0x11 | ChipID 0x32",
                          "Sampling 25Hz window (1.5s)...",
                          RADAR_COLOR_GRID_BRIGHT);
  }

  if (!bmm150IsOnline()) {
    bmm150Init();
  }

  uint32_t t0 = millis();
  while (millis() - t0 < 1500) {
    sensorsUpdate();
    delay(15);
  }

  sensorsPrintBmm150Diagnostics(Serial);
  sensorsPrintCalibrationDiagnostics(Serial);
  Serial.println("========================================================================");
}

// ============================================================================
// TEST 6: Motor Magnetic Interference Procedure
// ============================================================================
void runTest6(const char *label = "breadboard", float dist_cm = 5.0f) {
  Serial.println();
  Serial.println("========================================================================");
  Serial.println("[TEST 6] PROCEDURA INTERFERENZE MAGNETICHE DEL MOTORINO");
  Serial.println("========================================================================");

  if (displayIsInitialized()) {
    displayShowTestBanner("TEST 6: MAG INTERFERENCE",
                          "Phase A (OFF) -> B (ON) -> C (POST)",
                          label,
                          RADAR_COLOR_ORANGE);
  }

  sensorsRunMotorInterferenceTest(label, dist_cm, 30, 20, 20);
  motorStop(); // Guarantee GPIO16 LOW
  sensorsPrintInterferenceReport(Serial);
  Serial.println("========================================================================");
}

// ============================================================================
// TEST 7: Quectel LC76G GNSS (0x50/0x54 I2C + NMEA Parser)
// ============================================================================
void runTest7() {
  Serial.println();
  Serial.println("========================================================================");
  Serial.println("[TEST 7] MODULO GPS QUECTEL LC76G (I2C 0x50/0x54 & NMEA-0183)");
  Serial.println("========================================================================");

  if (displayIsInitialized()) {
    displayShowTestBanner("TEST 7: GNSS LC76G I2C",
                          "0x50 Cmd / 0x54 Read NMEA",
                          "Polling 4-phase I2C protocol (3.5s)...",
                          RADAR_COLOR_YELLOW);
  }

  gpsRunDiagnosticTest(3500, Serial);
  Serial.println("========================================================================");
}

// ============================================================================
// TEST 8: PMIC AXP2101 (0x34) & RTC PCF85063 (0x51)
// ============================================================================
void runTest8() {
  Serial.println();
  Serial.println("========================================================================");
  Serial.println("[TEST 8] PMIC AXP2101 (0x34) & RTC ONBOARD PCF85063 (0x51)");
  Serial.println("========================================================================");

  if (displayIsInitialized()) {
    displayShowTestBanner("TEST 8: PMIC AXP2101 & RTC",
                          "VBUS/VSYS/VBAT + Rails + PCF85063",
                          "Reading PMIC ADC & RTC registers...",
                          RADAR_COLOR_CYAN);
  }

  pmicUpdate();
  rtcUpdate();
  peripheralsPrintPmicRtcDiagnostics(Serial);
  Serial.println("========================================================================");
}

// ============================================================================
// TEST 9: Integrated 6-Axis IMU QMI8658 (0x6B) & Tilt Compensation
// ============================================================================
void runTest9() {
  Serial.println();
  Serial.println("========================================================================");
  Serial.println("[TEST 9] IMU 6 ASSI INTEGRATA QMI8658 (0x6B) & TILT COMPENSATION");
  Serial.println("========================================================================");

  if (displayIsInitialized()) {
    displayShowTestBanner("TEST 9: IMU QMI8658 (0x6B)",
                          "Accel 4G + Gyro 256dps + Tilt Comp",
                          "Sampling 50Hz attitude (1.2s)...",
                          RADAR_COLOR_GRID_BRIGHT);
  }

  uint32_t t0 = millis();
  while (millis() - t0 < 1200) {
    sensorsUpdate();
    delay(15);
  }

  sensorsPrintQmi8658Diagnostics(Serial);
  Serial.println("========================================================================");
}

void printOverallSummary() {
  const PeripheralsStatus &p = peripheralsGetStatus();
  const SensorsStatus &s = sensorsGetStatus();
  const GpsStatusSnapshot &g = gpsGetStatus();

  Serial.println();
  Serial.println("########################################################################");
  Serial.println("#          RIEPILOGO GENERALE COLLAUDO HARDWARE (TEST 0 .. 9)          #");
  Serial.println("########################################################################");
  Serial.printf("  TEST 0 (Power/Boot/16M/8M/Serial) : %s\n", g_test0_pass ? "PASS" : "FAIL");
  Serial.printf("  TEST 1 (I2C Bus Shared Scanner)   : Onboard=%s | BMM150(0x13)=%s | LC76G(0x50/54)=%s\n",
                g_test1_onboard_pass ? "PASS (7/7)" : "PARTIAL",
                s.bmm.online ? "PASS (0x32)" : "NOT DETECTED",
                g.i2c_reachable ? "PASS (ACK)" : "NACK");
  Serial.printf("  TEST 2 (AMOLED CO5300 + CST9217)  : QSPI/Touch HW=%s | Visual/Touch=PENDING MANUAL TEST\n",
                (displayIsInitialized() && displayIsTouchOnline()) ? "PASS" : "CHECK");
  Serial.printf("  TEST 3 (Button GPIO44 Pull-up)    : Rest HIGH=%s (presses=%lu) | Manual Press=PENDING MANUAL TEST\n",
                (p.button.raw_level == HIGH) ? "PASS" : "LOW", (unsigned long)p.button.press_count);
  Serial.printf("  TEST 4 (Vibration Motor GPIO16)   : Elec/SafeOff=%s (minVSYS=%u mV, drop=%d mV, i2cErr=%lu) | Tactile=PENDING MANUAL TEST\n",
                (!p.motor.voltage_dip_anomaly && !p.motor.i2c_error_anomaly && digitalRead(HW_MOTOR_PIN) == LOW) ? "PASS" : "WARN",
                p.motor.vsys_min_during_mv, (int)p.motor.vsys_max_drop_mv, (unsigned long)p.motor.i2c_errors_delta);
  Serial.printf("  TEST 5 (BMM150 Compass 3-Axis)    : %s (Addr=0x%02X, ChipID=0x%02X, |B|=%.2f uT, Hdg=%.1f deg [%s])\n",
                s.bmm.online ? "PASS (Static)" : "FAIL / OFFLINE (Check TP2/TP3)",
                s.bmm.i2c_addr, s.bmm.chip_id, s.bmm.b_norm_ut, s.bmm.heading_deg,
                s.bmm.calibrated ? "CAL" : "RAW");
  Serial.printf("  TEST 6 (Motor Mag Interference)   : %s (dB_peak=%.2f uT, dHdg_peak=%.1f deg)\n",
                (s.interference.tested && s.interference.bmm_was_online) ? "MEASURED (Breadboard)" : "SKIPPED (BMM150 Offline)",
                s.interference.latest.delta_b_peak_ut, s.interference.latest.delta_heading_peak_deg);
  Serial.printf("  TEST 7 (Quectel LC76G GNSS I2C)   : State=%s | Bytes=%lu | ValidNMEA=%lu | CrcErr=%lu | SatsUse=%u View=%u\n",
                g.status_str, (unsigned long)g.total_bytes_read,
                (unsigned long)g.nmea_sentences_valid, (unsigned long)g.nmea_sentences_crc_err,
                g.satellites_in_use, g.satellites_in_view);
  Serial.printf("  TEST 8 (PMIC AXP2101 & RTC 0x51)  : PMIC=%s (VBUS=%u mV, VSYS=%u mV, BAT=%s) | RTC=%s (%s)\n",
                p.pmic.online ? "PASS" : "FAIL", p.pmic.vbus_mv, p.pmic.vsys_mv,
                p.pmic.battery_present ? "CONNECTED" : "DISCONNECTED (USB Only)",
                p.rtc.online ? "PASS" : "FAIL", p.rtc.datetime_str);
  Serial.printf("  TEST 9 (IMU QMI8658 & Tilt Comp)  : %s (WHO_AM_I=0x%02X, |a|=%.3f g, Roll=%.2f deg, Pitch=%.2f deg)\n",
                s.imu.online ? "PASS" : "FAIL", s.imu.who_am_i, s.imu.acc_norm_g,
                s.imu.roll_deg, s.imu.pitch_deg);
  Serial.println("########################################################################");
}

void runAllTestsSequential() {
  runTest0();
  runTest1(true);
  runTest2();
  runTest3();
  runTest4();
  runTest5();
  runTest6("breadboard", 5.0f);
  runTest7();
  runTest8();
  runTest9();
  printOverallSummary();
}

void buildRadarSnapshot(RadarSystemSnapshot &snap) {
  snap.uptime_ms = millis();
  snap.active_page = g_active_page;

  // I2C & TCA9554
  memcpy(snap.i2c_present, g_i2c_present_map, sizeof(snap.i2c_present));
  snap.i2c_found_count = g_i2c_found_count;
  snap.i2c_tx_count = i2cGetTransactionCount();
  snap.i2c_err_count = i2cGetErrorCount();
  snap.tca_online = g_tca_online;
  snap.tca_inputs = g_tca_in;
  snap.tca_outputs = g_tca_out;
  snap.tca_config = g_tca_cfg;

  // Touch
  snap.touch_online = displayIsTouchOnline();
  snap.touch_pressed = g_touch_pressed;
  snap.touch_x = g_touch_x;
  snap.touch_y = g_touch_y;
  snap.touch_tap_count = displayGetTouchTapCount();

  // Peripherals (Button, Motor, PMIC, RTC)
  const PeripheralsStatus &p = peripheralsGetStatus();
  snap.btn_pressed = p.button.pressed;
  if (p.button.long_press_active) snap.btn_state = RADAR_BTN_LONG;
  else if (p.button.pressed) snap.btn_state = RADAR_BTN_PRESS;
  else if (p.button.last_event == BTN_EVENT_SHORT_PRESS && (millis() - p.button.last_change_ms < 1200)) snap.btn_state = RADAR_BTN_SHORT;
  else snap.btn_state = RADAR_BTN_IDLE;
  strncpy(snap.btn_state_str, p.button.state_str ? p.button.state_str : "IDLE", sizeof(snap.btn_state_str) - 1);
  snap.btn_press_count = p.button.press_count;
  snap.btn_short_count = p.button.short_press_count;
  snap.btn_long_count = p.button.long_press_count;
  snap.btn_last_hold_ms = p.button.last_hold_ms;
  snap.boot_btn_pressed = p.button.boot_pressed;
  snap.pwr_btn_pressed = p.button.pwr_pressed;

  snap.motor_active = p.motor.active;
  snap.motor_pattern_running = p.motor.pattern_running;
  strncpy(snap.motor_pattern_str, p.motor.pattern_str ? p.motor.pattern_str : "IDLE", sizeof(snap.motor_pattern_str) - 1);
  snap.motor_activations = p.motor.activation_count;
  snap.motor_min_vsys_mv = p.motor.vsys_min_during_mv;
  snap.motor_max_drop_mv = p.motor.vsys_max_drop_mv;
  snap.motor_i2c_err_delta = p.motor.i2c_errors_delta;
  snap.motor_power_anomaly = p.motor.voltage_dip_anomaly || p.motor.i2c_error_anomaly;

  snap.pmic_online = p.pmic.online;
  snap.pmic_chip_id = p.pmic.chip_id;
  snap.vbus_present = p.pmic.vbus_present;
  snap.vbus_good = p.pmic.vbus_good;
  snap.vbus_mv = p.pmic.vbus_mv;
  snap.vsys_mv = p.pmic.vsys_mv;
  snap.battery_present = p.pmic.battery_present;
  snap.batt_mv = p.pmic.batt_mv;
  snap.batt_percent = (int16_t)p.pmic.batt_percent;
  snap.is_charging = p.pmic.is_charging;
  snap.is_discharging = p.pmic.is_discharging;
  strncpy(snap.charger_status_str, p.pmic.charger_status_str ? p.pmic.charger_status_str : "N/A", sizeof(snap.charger_status_str) - 1);
  snap.pmic_temp_c = p.pmic.temp_c;
  snap.dc1_mv = p.pmic.dc1_mv;
  snap.dc3_mv = p.pmic.dc3_mv;
  snap.aldo1_mv = p.pmic.aldo1_mv;
  snap.aldo2_mv = p.pmic.aldo2_mv;
  snap.aldo3_mv = p.pmic.aldo3_mv;
  snap.aldo4_mv = p.pmic.aldo4_mv;
  snap.bldo1_mv = p.pmic.bldo1_mv;
  snap.bldo2_mv = p.pmic.bldo2_mv;
  snap.rtc_online = p.rtc.online;
  strncpy(snap.rtc_datetime, p.rtc.datetime_str, sizeof(snap.rtc_datetime) - 1);

  // Sensors (BMM150, Cal, IMU QMI8658, Interference)
  sensorsFillRadarSnapshot(snap);

  // GPS (LC76G)
  const GpsStatusSnapshot &g = gpsGetStatus();
  snap.gps_status = (uint8_t)g.status;
  snap.gps_i2c_online = g.i2c_reachable;
  snap.gps_fix_valid = g.fix_valid;
  snap.gps_fix_quality = g.fix_quality;
  snap.gps_sats_in_use = g.satellites_in_use;
  snap.gps_sats_in_view = g.satellites_in_view;
  snap.gps_lat = g.latitude_deg;
  snap.gps_lon = g.longitude_deg;
  snap.gps_alt_m = g.altitude_m;
  snap.gps_hdop = g.hdop;
  strncpy(snap.gps_utc_time, g.utc_time_str, sizeof(snap.gps_utc_time) - 1);
  strncpy(snap.gps_utc_date, g.utc_date_str, sizeof(snap.gps_utc_date) - 1);
  snap.gps_fix_age_ms = g.fix_age_ms;
  snap.gps_bytes_rx = g.total_bytes_read;
  snap.gps_i2c_ok = g.i2c_tx_ok;
  snap.gps_i2c_fail = g.i2c_tx_fail;
  snap.gps_nmea_valid = g.nmea_sentences_valid;
  snap.gps_nmea_cs_err = g.nmea_sentences_crc_err;
  if (g.last_nmea_lines_count >= 1) {
    uint8_t idx0 = (g.last_nmea_lines_count >= 2) ? (g.last_nmea_lines_count - 2) : 0;
    uint8_t idx1 = g.last_nmea_lines_count - 1;
    strncpy(snap.gps_last_nmea[0], g.last_nmea_lines[idx0], sizeof(snap.gps_last_nmea[0]) - 1);
    if (g.last_nmea_lines_count >= 2) {
      strncpy(snap.gps_last_nmea[1], g.last_nmea_lines[idx1], sizeof(snap.gps_last_nmea[1]) - 1);
    }
  }

  // Error flags & summary
  uint16_t err = 0;
  String err_str = "";
  if (!snap.bmm_online) { err |= (1 << 0); err_str += "BMM150! "; }
  if (!snap.imu_online) { err |= (1 << 1); err_str += "IMU! "; }
  if (!snap.pmic_online) { err |= (1 << 2); err_str += "PMIC! "; }
  if (!snap.touch_online) { err |= (1 << 3); err_str += "TOUCH! "; }
  if (snap.gps_status == RADAR_GPS_UNREACHABLE) { err |= (1 << 4); err_str += "GPS_I2C! "; }
  if (snap.motor_power_anomaly) { err |= (1 << 5); err_str += "MOT_DIP! "; }
  snap.error_flags = err;
  if (err == 0) {
    strncpy(snap.error_summary, "OK", sizeof(snap.error_summary) - 1);
  } else {
    err_str.trim();
    strncpy(snap.error_summary, err_str.c_str(), sizeof(snap.error_summary) - 1);
  }
}

void printHelp() {
  Serial.println("\n=== DRAGON RADAR INTERACTIVE SERIAL COMMANDS ===");
  Serial.println("  help                : Show this command list");
  Serial.println("  status              : Print overall hardware status & test summary");
  Serial.println("  beacons             : ESP-NOW beacon status (untrusted, no direction)");
  Serial.println("  debug_legacy on/off : Legacy compact or large debug UI");
  Serial.println("  sd_check            : Check microSD without formatting");
  Serial.println("  test0 .. test9      : Run individual test (test0, test1, ..., test9)");
  Serial.println("  testall             : Run full sequential test suite (TEST 0 -> TEST 9)");
  Serial.println("  scan                : Run shared I2C bus scan (0x03..0x77)");
  Serial.println("  color               : Run TEST 2 AMOLED color & geometry pattern");
  Serial.println("  vibe1 | vibe2 | vibe3 : Trigger motor 100ms | 250ms | 3x100ms pattern");
  Serial.println("  vibe_off            : Force motor GPIO16 LOW immediately");
  Serial.println("  cal_start           : Start 3-axis compass calibration (figure-8)");
  Serial.println("  cal_stop            : Finish & save compass calibration to NVS");
  Serial.println("  cal_clear           : Clear compass calibration in NVS");
  Serial.println("  interference [cm]   : Run TEST 6 motor magnetic interference test (e.g. interference 20)");
  Serial.println("  abs_url <https-origin> : Configure Audiobookshelf server origin (no credentials)");
  Serial.println("  abs_probe           : Check Audiobookshelf server over validated HTTPS");
  Serial.println("  abs_status          : Show Audiobookshelf service status");
  Serial.println("  abs_login           : Enter dragon-user password privately over USB (RAM only)");
  Serial.println("  abs_catalog         : Read dragon-user in-progress books (no playback)");
  Serial.println("  abs_forget          : Remove saved Audiobookshelf password from NVS");
  Serial.println("  audio_test          : Play three synthetic tones (physical confirmation needed)");
  Serial.println("  codec_test          : Decode a local MP3 sample without speaker output");
  Serial.println("  nmea_raw            : Toggle live raw NMEA stream on Serial");
  Serial.println("  gps_reset           : Hardware reset LC76G via TCA9554 EXIO7");
  Serial.println("  gps_uart            : Test optional UART fallback on GPIO17/18");
  Serial.println("  page0 .. page3      : Switch Dragon Radar UI page on AMOLED display");
  Serial.println("================================================");
}

void handleSerialCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;
  if (cmd.startsWith("!SECRET!")) { Serial.println("[ABS] Segreto ignorato: nessuna richiesta attiva."); return; }
  String lower = cmd;
  lower.toLowerCase();

  if (lower.startsWith("abs_url ")) Serial.println("\n[CMD] Configurazione indirizzo server...");
  else Serial.printf("\n[CMD] Executing '%s'...\n", cmd.c_str());

  if (lower == "debug_legacy on") { g_debugLegacy=true; Serial.println("[DEBUG] UI compatta originale.");
  } else if (lower == "debug_legacy off") { g_debugLegacy=false; Serial.println("[DEBUG] UI grande.");
  } else if (lower == "mag_stream on") { g_mag_stream = true; Serial.println("[MAG] stream ON");
  } else if (lower == "mag_stream off") { g_mag_stream = false; Serial.println("[MAG] stream OFF");
  } else if (lower == "help" || lower == "?") {
    printHelp();
  } else if (lower == "sd_check") {
    Serial.println(sdServiceRequestCheck() ? "[SD] Controllo avviato." : sdServiceStatusText());
  } else if (lower == "beacons") {
    beaconServicePrint(Serial);
  } else if (lower == "status") {
    printOverallSummary();
  } else if (lower == "test0") {
    runTest0();
  } else if (lower == "test1" || lower == "scan") {
    runTest1(false);
  } else if (lower == "test2" || lower == "color") {
    runTest2();
  } else if (lower == "test3") {
    runTest3();
  } else if (lower == "test4") {
    runTest4();
  } else if (lower == "test5") {
    runTest5();
  } else if (lower == "test6" || lower == "interference") {
    runTest6("breadboard", 5.0f);
  } else if (lower.startsWith("interference ")) {
    float cm = lower.substring(13).toFloat();
    if (cm <= 0.0f) cm = 5.0f;
    char lbl[16];
    if (cm >= 20.0f) snprintf(lbl, sizeof(lbl), ">=20cm");
    else snprintf(lbl, sizeof(lbl), "%.0fcm", cm);
    runTest6(lbl, cm);
  } else if (lower == "test7") {
    runTest7();
  } else if (lower == "test8") {
    runTest8();
  } else if (lower == "test9") {
    runTest9();
  } else if (lower == "testall") {
    runAllTestsSequential();
  } else if (lower == "vibe1") {
    motorTriggerShortPulse();
    Serial.println("[MOTOR] Triggered 100ms short pulse.");
  } else if (lower == "vibe2") {
    motorTriggerMediumPulse();
    Serial.println("[MOTOR] Triggered 250ms medium pulse.");
  } else if (lower == "vibe3") {
    motorTriggerTriplePulse();
    Serial.println("[MOTOR] Triggered 3x100ms triple pulse.");
  } else if (lower == "vibe_off") {
    motorStop();
    Serial.println("[MOTOR] Forced GPIO16 LOW (OFF).");
  } else if (lower == "cal_start") {
    magCalStart();
    Serial.println("[CAL] Compass calibration started. Rotate device in 3D figure-8, then run cal_stop.");
  } else if (lower == "cal_stop") {
    bool ok = magCalStop(true);
    Serial.printf("[CAL] Compass calibration stopped & saved to NVS: %s\n", ok ? "SUCCESS" : "INSUFFICIENT SPAN");
    sensorsPrintCalibrationDiagnostics(Serial);
  } else if (lower == "cal_clear") {
    magCalClearNvs();
    Serial.println("[CAL] Cleared compass calibration from NVS.");
  } else if (lower.startsWith("abs_url ")) {
    bool ok = audiobookshelfSetOrigin(cmd.substring(8).c_str());
    Serial.println(ok ? "[ABS] Server configurato." : "[ABS] URL non valido oppure servizio occupato.");
  } else if (lower == "abs_probe") {
    audiobookshelfCheckServer(); Serial.println(audiobookshelfStatusText());
  } else if (lower == "abs_status") {
    Serial.println(audiobookshelfStatusText());
  } else if (lower == "abs_forget") {
    Serial.println(audiobookshelfForgetPassword() ? "[ABS] Password rimossa." : "[ABS] Nessuna password rimossa.");
  } else if (lower == "abs_catalog") {
    audiobookshelfLoadCatalog(); Serial.println(audiobookshelfStatusText());
  } else if (lower == "abs_login") {
    if (audiobookshelfLoginReady()) { g_abs_password_pending = true; g_abs_password_overflow = false; g_abs_password_length = 0; Serial.println("ABS_PASSWORD_READY"); }
    else Serial.println(audiobookshelfStatusText());
  } else if (lower == "codec_test") {
    Serial.println(audioCodecProbeStart() ? "[CODEC] Test avviato." : "[CODEC] Test occupato o non disponibile.");
  } else if (lower == "audio_test") {
    AudioResult result = audioServiceTestTone();
    Serial.println(result == AudioResult::Started ? "[AUDIO] Test avviato: ascoltare la cassa." : audioServiceStatus());
  } else if (lower == "nmea_raw") {
    bool en = gpsToggleRawStream();
    Serial.printf("[GPS] Raw NMEA stream on Serial: %s\n", en ? "ENABLED" : "DISABLED");
  } else if (lower == "gps_reset") {
    gpsHardwareReset();
    gpsPrintDiagnostics(Serial);
  } else if (lower == "gps_uart") {
    gpsTryUartFallback(HW_GPS_UART_TX_MCU, HW_GPS_UART_RX_MCU, 0, 1200);
  } else if (lower == "page0") {
    g_active_page = 0; g_apps.showDebugPage(0);
    Serial.println("[UI] Switched to Page 0 (Dragon Radar Main Compass).");
  } else if (lower == "page1") {
    g_active_page = 1; g_apps.showDebugPage(1);
    Serial.println("[UI] Switched to Page 1 (I2C Bus & PMIC AXP2101).");
  } else if (lower == "page2") {
    g_active_page = 2; g_apps.showDebugPage(2);
    Serial.println("[UI] Switched to Page 2 (BMM150, IMU QMI8658 & Motor Interference).");
  } else if (lower == "page3") {
    g_active_page = 3; g_apps.showDebugPage(3);
    Serial.println("[UI] Switched to Page 3 (GNSS Quectel LC76G).");
  } else {
    Serial.printf("[CMD] Unknown command '%s'. Type 'help' for available commands.\n", cmd.c_str());
  }
}

} // namespace

void setup() {
  // 1. CRITICAL HARDWARE SAFETY: Force GPIO16 (Motor) LOW as very first instruction
  motorSafePreInit();

  // 2. Disable Wi-Fi and Bluetooth for clean power/EMI baseline
  WiFi.mode(WIFI_OFF);
  btStop();

  // 3. Initialize USB CDC Serial
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0 < 2500)) {
    delay(10);
  }
  delay(200);

  Serial.println("\n\n########################################################################");
  Serial.println("#    DRAGON RADAR HARDWARE TEST & DIAGNOSTIC SUITE (ESP32-S3R8)        #");
  Serial.println("#    Target: Waveshare ESP32-S3-Touch-AMOLED-1.75-G (SKU 31264)        #");
  Serial.println("########################################################################");

  // 4. Initialize shared I2C bus (SDA=15, SCL=14, 100kHz, buf=512)
  i2cBusInit();
  tca9554Init();

  // 5. Initialize core peripherals, display and sensors first;
  //    gpsInit() is called right after TEST 1 so TEST 1's EXIO7 reset test doesn't interrupt an active GPS session
  peripheralsInit();
  settingsInit();
  audiobookshelfInit();
  wifiServiceSetServerOriginHandler(audiobookshelfSetOrigin);
  displayInit(true);
  displaySetBrightness(settingsGet().brightness);
  sensorsInit();
  wifiServiceInit();
  beaconServiceInit();

  // Normal boot does not execute diagnostic tests or reset the GNSS.
  gpsInit();

  // 7. Ensure motor is strictly OFF and enable button haptic feedback for interactive loop
  motorStop();
  printHelp();
}

void loop() {
  // 1. Non-blocking periodic updates of all modules
  peripheralsUpdate();
  settingsUpdate();
  wifiServiceUpdate();
  audiobookshelfUpdate();
  beaconServiceUpdate();
  const ButtonStatus &powerButton = buttonGetStatus();
  if (g_standby) {
    const bool shortPress = buttonConsumeShortPress();
    const bool longPress = buttonConsumeLongPress();
    if (!g_standbyButton.shouldWake(powerButton.pressed,shortPress || longPress)) { delay(10); return; }
    g_standby = false;
    beaconServiceSetProximitySuspended(false);
    g_gestures = GestureManager();
    g_apps = AppManager();
    displaySetBrightness(settingsGet().brightness);
  } else if (g_standbyButton.shouldEnter(powerButton.pressed,powerButton.current_hold_ms,otaServiceActive())) {
    motorStop(); displaySetBrightness(0);
    g_standbyButton.entered(true); g_standby = true;
    beaconServiceSetProximitySuspended(true);
    return;
  }
  sensorsUpdate();
  gpsUpdate();

  if (otaServiceUpdating()) motorStop();
  static uint32_t mag_stream_last = 0;
  if (g_mag_stream && millis() - mag_stream_last >= 500) {
    mag_stream_last = millis();
    const SensorsStatus &s = sensorsGetStatus();
    char msg[220];
    int n = snprintf(msg,sizeof(msg),"[MAG] t=%lu raw=%.1f,%.1f,%.1f rawB=%.1f calB=%.1f heading=%.1f roll=%.1f pitch=%.1f online=%d plausible=%d errors=%lu\n", (unsigned long)millis(),s.bmm.raw_x_ut,s.bmm.raw_y_ut,s.bmm.raw_z_ut,s.bmm.raw_b_norm_ut,s.bmm.b_norm_ut,s.bmm.heading_deg,s.imu.roll_deg,s.imu.pitch_deg,s.bmm.online,s.bmm.field_plausible,(unsigned long)s.bmm.error_count);
    if (n > 0 && n < (int)sizeof(msg) && Serial.availableForWrite() >= n) Serial.write((const uint8_t*)msg,n);
  }

  // 2. Poll touch controller (CST9217 0x5A)
  bool tap = false;
  g_touch_pressed = displayPollTouch(g_touch_x, g_touch_y, tap);
  GestureEvent gesture = g_gestures.update(g_touch_pressed, g_touch_x, g_touch_y, millis());
  if (!g_apps.launcherOpen() && g_apps.active() == AppId::Radar && gesture.type == Gesture::Tap) {
    if (settingsGet().demo) appRadarApproach();
    else if (gesture.y >= RADAR_SPHERE_Y && gesture.y < RADAR_SPHERE_Y + RADAR_SPHERE_HEIGHT &&
             gesture.x >= RADAR_SPHERE_X && gesture.x < RADAR_SPHERE_X + SphereSearch::count*RADAR_SPHERE_PITCH) {
      const int16_t offset=gesture.x-RADAR_SPHERE_X;
      if (offset%RADAR_SPHERE_PITCH<RADAR_SPHERE_WIDTH)
        beaconServiceSelectSphere(1+offset/RADAR_SPHERE_PITCH);
    } else if (gesture.x >= RADAR_GAME_X && gesture.x < RADAR_GAME_X + RADAR_GAME_WIDTH &&
               gesture.y >= RADAR_GAME_Y && gesture.y < RADAR_GAME_Y + RADAR_GAME_HEIGHT &&
               beaconServiceSphereGame().foundCount) {
      beaconServiceNewGame();
    } else if (gesture.x >= RADAR_FOUND_X && gesture.x < RADAR_FOUND_X + RADAR_FOUND_WIDTH &&
               gesture.y >= RADAR_FOUND_Y && gesture.y < RADAR_FOUND_Y + RADAR_FOUND_HEIGHT) {
      if (beaconServiceConfirmSphereFound() == SearchEvent::SphereFound) beaconServiceRequestSphereMotor();
    }
  }
  if (!g_apps.launcherOpen() && g_apps.active() == AppId::Audiobookshelf && gesture.type == Gesture::Tap) {
    if (gesture.y >= 245 && gesture.y < 315) { if (gesture.x < 233) { if (audiobookshelfIsAuthenticated()) audiobookshelfLoadCatalog(); else audiobookshelfCheckServer(); } else audioServiceTestTone(); }
    else if (gesture.y >= 150 && gesture.y < 240 && audiobookshelfIsAuthenticated()) audiobookshelfSelectNext();
    else if (gesture.y >= 330 && gesture.y < 390) {
      OsSettings &s = settingsGet();
      if (gesture.x >= 108 && gesture.x < 173) { s.volume = s.volume >= 10 ? s.volume - 10 : 0; settingsChanged(); }
      else if (gesture.x >= 293 && gesture.x < 358) { s.volume = s.volume <= 90 ? s.volume + 10 : 100; settingsChanged(); }
    }
  }
  if (!g_apps.launcherOpen() && g_apps.active() == AppId::Settings && gesture.type == Gesture::Tap) appSettingsTap(gesture.x, gesture.y);
  const bool sphereMotorPage = !g_debugLegacy && !g_apps.launcherOpen() &&
                               g_apps.active() == AppId::Debug && g_apps.debugPage() == DEBUG_SPHERE_MOTOR_PAGE;
  if (sphereMotorPage && debugSphereMotorTap(gesture)) beaconServiceRequestSphereMotor();
  else {
    if (!sphereMotorPage && !g_apps.launcherOpen() && g_apps.active() == AppId::Debug &&
        gesture.type == Gesture::Hold) motorTriggerShortPulse();
    g_apps.gesture(gesture);
  }
  if (buttonConsumeShortPress()) { if (!g_apps.launcherOpen() && g_apps.active() == AppId::Radar) appRadarZoom(); else if (!g_apps.launcherOpen() && g_apps.active() == AppId::Settings) appSettingsNext(); else g_apps.shortPress(); }
  if (buttonConsumeLongPress()) g_apps.longPress();
  if (appSettingsConsumeStandby() && !otaServiceActive()) {
    motorStop();
    displaySetBrightness(0);
    g_standbyButton.entered(buttonGetStatus().pressed);
    g_standby = true;
    beaconServiceSetProximitySuspended(true);
    return;
  }
  if (appSettingsConsumeRestart() && !otaServiceUpdating()) ESP.restart();

  // Refresh TCA9554 register snapshot every 500 ms
  static uint32_t last_tca_ms = 0;
  if (millis() - last_tca_ms >= 500) {
    last_tca_ms = millis();
    g_tca_online = tca9554ReadAll(g_tca_in, g_tca_out, g_tca_cfg);
  }

  // 4. Render Dragon Radar UI at ~16 FPS (every 60 ms)
  static uint32_t last_render_ms = 0;
  if (displayIsInitialized() && (millis() - last_render_ms >= 60)) {
    last_render_ms = millis();
    RadarSystemSnapshot snap;
    buildRadarSnapshot(snap);
    if (g_apps.launcherOpen()) displayRenderOsScreen(0, true, g_apps.selection());
    else if (g_apps.active() == AppId::Radar) {
      RadarView view = appRadarUpdate(snap);
      static uint32_t lastBeepCycle = 0;
      if (!otaServiceUpdating()) {
        appRadarHaptic(view, millis());
        const uint32_t beepCycle = millis() / RADAR_SCAN_PERIOD_MS;
        if (view.count && settingsGet().sound && settingsGet().volume && beepCycle != lastBeepCycle) {
          lastBeepCycle = beepCycle;
          audioServiceRadarBeep();
        }
      }
      displayRenderRadarApp(view);
    }
    else if (g_apps.active() == AppId::Settings) displayRenderSettings(appSettingsSelected(), snap);
    else if (g_apps.active() == AppId::Debug) { if (g_debugLegacy) displayRenderDragonRadar(snap, g_apps.debugPage() % 4); else displayRenderDebugLarge(snap, g_apps.debugPage()); }
    else displayRenderOsScreen(static_cast<uint8_t>(g_apps.active()), false);
  }

  // 5. Non-blocking Serial command processing
  static String line;
  for (unsigned n = 0; n < 64 && Serial.available(); ++n) {
    char c = (char)Serial.read();
    if (g_abs_password_pending) {
      if (c == '\n' || c == '\r') {
        if (g_abs_password_length || g_abs_password_overflow) {
          bool accepted = !g_abs_password_overflow && strncmp(g_abs_password, "!SECRET!", 8) == 0 && audiobookshelfLoginUser(g_abs_password + 8);
          volatile char *secret = g_abs_password; for (size_t i=0;i<sizeof(g_abs_password);++i) secret[i]=0;
          g_abs_password_pending = false; g_abs_password_overflow = false; g_abs_password_length = 0;
          Serial.println(accepted ? "[ABS] Accesso dragon-user avviato." : "[ABS] Accesso non avviato.");
        }
      } else if (c == 27) {
        volatile char *secret = g_abs_password; for (size_t i=0;i<sizeof(g_abs_password);++i) secret[i]=0;
        g_abs_password_pending = false; g_abs_password_overflow = false; g_abs_password_length = 0; Serial.println("[ABS] Accesso annullato.");
      } else if (g_abs_password_length < sizeof(g_abs_password)-1) g_abs_password[g_abs_password_length++] = c;
      else g_abs_password_overflow = true;
    } else if (c == '\n' || c == '\r') { if (line.length()) { handleSerialCommand(line); line = ""; } }
    else if (line.length() < 128) line += c;
  }

  delay(4);
}
