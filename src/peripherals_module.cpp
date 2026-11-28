/**
 * @file peripherals_module.cpp
 * @brief Implementazione modulo periferiche per Dragon Radar Hardware Test:
 *        - TEST 3: Pulsante esterno su GPIO44 (INPUT_PULLUP, debounce 40ms, short/long press)
 *                  + diagnostica pulsanti onboard BOOT (GPIO0) e PWR (TCA9554 EXIO4 / SYS_OUT)
 *        - TEST 4: Motorino di vibrazione Grove V1.3 su GPIO16 (attivo HIGH, LOW al boot,
 *                  controller non bloccante + watchdog hardware esp_timer max 1000ms + telemetria VSYS/VBUS/I2C)
 *        - TEST 8: PMIC AXP2101 (0x34) e RTC PCF85063 (0x51) su bus Wire condiviso protetto da mutex.
 */

#include "peripherals_module.h"

#include <driver/gpio.h>
#include <esp_timer.h>
#include <stdio.h>
#include <string.h>

#ifndef XPOWERS_CHIP_AXP2101
#define XPOWERS_CHIP_AXP2101
#endif

#if __has_include("XPowersLib.h")
#include "XPowersLib.h"
#else
#include "../references/libraries/XPowersLib/src/XPowersLib.h"
#endif

#if __has_include("SensorPCF85063.hpp")
#include "SensorPCF85063.hpp"
#else
#include "../references/libraries/SensorLib/src/SensorPCF85063.hpp"
#endif

namespace {

// Stato aggregato delle periferiche
PeripheralsStatus s_status = {};

// --- Stato interno Pulsante (TEST 3) ---
bool s_btn_initialized = false;
bool s_btn_raw_last = false;
uint32_t s_btn_raw_change_ms = 0;
uint32_t s_btn_press_start_ms = 0;
bool s_btn_long_fired_this_hold = false;
uint32_t s_btn_state_banner_until_ms = 0;

// Latch eventi per consumatori asincroni (buttonConsume*())
bool s_latched_just_pressed = false;
bool s_latched_just_released = false;
bool s_latched_short_press = false;
bool s_latched_long_press = false;

// Debounce / stato BOOT (GPIO0) e PWR (TCA9554 EXIO4)
bool s_boot_raw_last = false;
uint32_t s_boot_raw_change_ms = 0;
uint32_t s_last_pwr_poll_ms = 0;

// --- Stato interno Motorino (TEST 4) ---
struct MotorStep {
  bool on;
  uint16_t duration_ms;
};

constexpr size_t kMaxPatternSteps = 8;
MotorStep s_pattern_steps[kMaxPatternSteps] = {};
uint8_t s_pattern_step_count = 0;
uint8_t s_pattern_step_idx = 0;
uint32_t s_step_start_ms = 0;
uint32_t s_last_motor_telemetry_ms = 0;

esp_timer_handle_t s_motor_wdt_timer = nullptr;
volatile bool s_watchdog_tripped_isr = false;
volatile uint32_t s_watchdog_trip_count_isr = 0;

// --- Istanze PMIC AXP2101 & RTC PCF85063 (TEST 8) ---
XPowersPMU s_pmu;
SensorPCF85063 s_rtc;
uint32_t s_last_pmic_poll_ms = 0;
uint32_t s_last_rtc_poll_ms = 0;

// Callback I2C per XPowersPMU su bus Wire condiviso protetto da i2cBusLock()
int pmuReadRegCallback(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len) {
  if (!i2cBusLock(150)) {
    s_status.pmic.read_error_count++;
    return -1;
  }
  bool ok = i2cReadBytes(devAddr, regAddr, data, len);
  if (!ok) {
    s_status.pmic.read_error_count++;
  }
  i2cBusUnlock();
  return ok ? 0 : -1;
}

int pmuWriteRegCallback(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len) {
  if (!i2cBusLock(150)) {
    s_status.pmic.read_error_count++;
    return -1;
  }
  bool ok = i2cWriteBytes(devAddr, regAddr, data, len);
  if (!ok) {
    s_status.pmic.read_error_count++;
  }
  i2cBusUnlock();
  return ok ? 0 : -1;
}

// Callback I2C per SensorPCF85063 su bus Wire condiviso protetto da i2cBusLock()
bool rtcI2cCallback(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len, bool writeReg, bool isWrite) {
  if (!i2cBusLock(150)) {
    return false;
  }
  bool ok = false;
  if (writeReg) {
    if (isWrite) {
      ok = i2cWriteBytes(addr, reg, buf, len);
    } else {
      ok = i2cReadBytes(addr, reg, buf, len);
    }
  } else {
    if (isWrite) {
      ok = i2cRawWrite(addr, buf, len);
    } else {
      ok = i2cRawRead(addr, buf, len);
    }
  }
  i2cBusUnlock();
  return ok;
}

const char *chargerStatusToString(uint8_t chg_status) {
  switch (chg_status) {
    case XPOWERS_AXP2101_CHG_TRI_STATE:
      return "tri_charge";
    case XPOWERS_AXP2101_CHG_PRE_STATE:
      return "pre_charge";
    case XPOWERS_AXP2101_CHG_CC_STATE:
      return "cc_charge";
    case XPOWERS_AXP2101_CHG_CV_STATE:
      return "cv_charge";
    case XPOWERS_AXP2101_CHG_DONE_STATE:
      return "charge_done";
    case XPOWERS_AXP2101_CHG_STOP_STATE:
    default:
      return "not_charging";
  }
}

const char *motorPatternToString(MotorPattern pattern) {
  switch (pattern) {
    case MOTOR_PATTERN_SHORT_100MS:
      return "SHORT_100MS";
    case MOTOR_PATTERN_MEDIUM_250MS:
      return "MEDIUM_250MS";
    case MOTOR_PATTERN_TRIPLE_100_120MS:
      return "TRIPLE_3x100MS";
    case MOTOR_PATTERN_DOUBLE_100_120MS:
      return "DOUBLE_2x100MS";
    case MOTOR_PATTERN_BUTTON_FEEDBACK:
      return "BTN_FEEDBACK_100MS";
    case MOTOR_PATTERN_CUSTOM_PULSE:
      return "CUSTOM_PULSE";
    case MOTOR_PATTERN_NONE:
    default:
      return "IDLE";
  }
}

// Callback hardware/FreeRTOS esp_timer per spegnimento forzato GPIO16 a max 1000 ms
void IRAM_ATTR motorWatchdogCallback(void *arg) {
  (void)arg;
  gpio_set_level((gpio_num_t)HW_MOTOR_PIN, HW_MOTOR_SAFE_OFF_LEVEL);
  s_watchdog_tripped_isr = true;
  s_watchdog_trip_count_isr = s_watchdog_trip_count_isr + 1;
}

// Campionamento rapido VSYS/VBUS per telemetria motorino
void sampleMotorPowerRail(uint16_t &vsys_mv_out, uint16_t &vbus_mv_out) {
  vsys_mv_out = s_status.pmic.vsys_mv;
  vbus_mv_out = s_status.pmic.vbus_mv;
  if (!s_status.pmic.online) {
    return;
  }
  if (i2cBusLock(50)) {
    uint16_t vsys = s_pmu.getSystemVoltage();
    uint16_t vbus = s_pmu.isVbusIn() ? s_pmu.getVbusVoltage() : 0;
    i2cBusUnlock();
    if (vsys > 0) {
      vsys_mv_out = vsys;
      s_status.pmic.vsys_mv = vsys;
    }
    vbus_mv_out = vbus;
    s_status.pmic.vbus_mv = vbus;
  }
}

void setMotorHardwareLevel(bool turn_on) {
  if (turn_on) {
    // Avvia/riavvia SEMPRE il timer hardware one-shot a max 1000 ms prima di alzare GPIO16
    if (s_motor_wdt_timer != nullptr) {
      esp_timer_stop(s_motor_wdt_timer);
      esp_timer_start_once(s_motor_wdt_timer, (uint64_t)HW_MOTOR_MAX_ON_MS * 1000ULL);
    }
    digitalWrite(HW_MOTOR_PIN, HW_MOTOR_ACTIVE_LEVEL);
    gpio_set_level((gpio_num_t)HW_MOTOR_PIN, HW_MOTOR_ACTIVE_LEVEL);
    s_status.motor.active = true;
    s_status.motor.pin_level = HW_MOTOR_ACTIVE_LEVEL;
    s_status.motor.pulse_started_ms = millis();
    s_status.motor.current_on_ms = 0;
    s_status.motor.activation_count++;
  } else {
    digitalWrite(HW_MOTOR_PIN, HW_MOTOR_SAFE_OFF_LEVEL);
    gpio_set_level((gpio_num_t)HW_MOTOR_PIN, HW_MOTOR_SAFE_OFF_LEVEL);
    if (s_motor_wdt_timer != nullptr) {
      esp_timer_stop(s_motor_wdt_timer);
    }
    s_status.motor.active = false;
    s_status.motor.pin_level = HW_MOTOR_SAFE_OFF_LEVEL;
    s_status.motor.current_on_ms = 0;
  }
}

void beginMotorTelemetryCapture() {
  uint16_t vsys = 0;
  uint16_t vbus = 0;
  sampleMotorPowerRail(vsys, vbus);
  s_status.motor.vsys_before_mv = vsys;
  s_status.motor.vsys_min_during_mv = vsys;
  s_status.motor.vsys_after_mv = vsys;
  s_status.motor.vsys_max_drop_mv = 0;
  s_status.motor.vbus_before_mv = vbus;
  s_status.motor.vbus_min_during_mv = vbus;
  s_status.motor.vbus_after_mv = vbus;
  s_status.motor.i2c_errors_before = i2cGetErrorCount();
  s_status.motor.i2c_errors_during = s_status.motor.i2c_errors_before;
  s_status.motor.i2c_errors_delta = 0;
  s_status.motor.voltage_dip_anomaly = false;
  s_status.motor.i2c_error_anomaly = false;
  s_last_motor_telemetry_ms = millis();
}

void updateMotorTelemetrySample(bool final_sample) {
  uint16_t vsys = 0;
  uint16_t vbus = 0;
  sampleMotorPowerRail(vsys, vbus);

  if (vsys > 0) {
    if (s_status.motor.vsys_min_during_mv == 0 || vsys < s_status.motor.vsys_min_during_mv) {
      s_status.motor.vsys_min_during_mv = vsys;
    }
    if (s_status.motor.vsys_before_mv > 0) {
      int16_t drop = (int16_t)s_status.motor.vsys_before_mv - (int16_t)s_status.motor.vsys_min_during_mv;
      if (drop > s_status.motor.vsys_max_drop_mv) {
        s_status.motor.vsys_max_drop_mv = drop;
      }
    }
    // Soglia di anomalia VSYS: < 3000 mV o caduta > 450 mV
    if (vsys < 3000 || s_status.motor.vsys_max_drop_mv > 450) {
      s_status.motor.voltage_dip_anomaly = true;
    }
  }

  if (vbus > 0) {
    if (s_status.motor.vbus_min_during_mv == 0 || vbus < s_status.motor.vbus_min_during_mv) {
      s_status.motor.vbus_min_during_mv = vbus;
    }
  }

  if (final_sample) {
    s_status.motor.vsys_after_mv = vsys;
    s_status.motor.vbus_after_mv = vbus;
  }

  uint32_t err_now = i2cGetErrorCount();
  s_status.motor.i2c_errors_during = err_now;
  s_status.motor.i2c_errors_delta =
      (err_now >= s_status.motor.i2c_errors_before) ? (err_now - s_status.motor.i2c_errors_before) : 0;
  if (s_status.motor.i2c_errors_delta > 0) {
    s_status.motor.i2c_error_anomaly = true;
  }
}

} // namespace

// ============================================================================
// IMPLEMENTAZIONE API MOTORINO DI VIBRAZIONE (TEST 4)
// ============================================================================

void motorSafePreInit() {
  gpio_reset_pin((gpio_num_t)HW_MOTOR_PIN);
  gpio_set_direction((gpio_num_t)HW_MOTOR_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)HW_MOTOR_PIN, HW_MOTOR_SAFE_OFF_LEVEL);
  pinMode(HW_MOTOR_PIN, OUTPUT);
  digitalWrite(HW_MOTOR_PIN, HW_MOTOR_SAFE_OFF_LEVEL);

  s_status.motor.pin = HW_MOTOR_PIN;
  s_status.motor.active = false;
  s_status.motor.pattern_running = false;
  s_status.motor.pin_level = HW_MOTOR_SAFE_OFF_LEVEL;
  s_status.motor.current_pattern = MOTOR_PATTERN_NONE;
  s_status.motor.pattern_str = "IDLE";
  s_status.motor.max_watchdog_ms = HW_MOTOR_MAX_ON_MS;
}

bool motorInit() {
  motorSafePreInit();

  if (s_motor_wdt_timer == nullptr) {
    const esp_timer_create_args_t wdt_args = {
        .callback = &motorWatchdogCallback,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "motor_wdt_1s",
        .skip_unhandled_events = true};
    esp_err_t err = esp_timer_create(&wdt_args, &s_motor_wdt_timer);
    if (err != ESP_OK) {
      s_motor_wdt_timer = nullptr;
      s_status.motor.initialized = false;
      return false;
    }
  }

  s_status.motor.initialized = true;
  return true;
}

void motorStop() {
  setMotorHardwareLevel(false);
  s_pattern_step_count = 0;
  s_pattern_step_idx = 0;
  s_status.motor.pattern_running = false;
  s_status.motor.step_index = 0;
  s_status.motor.total_steps = 0;
  s_status.motor.pattern_str = "IDLE";
  updateMotorTelemetrySample(true);
}

bool motorStartPattern(MotorPattern pattern) {
  if (!s_status.motor.initialized) {
    motorInit();
  }

  // Interrompi eventuale pattern precedente in modo sicuro
  setMotorHardwareLevel(false);
  s_pattern_step_count = 0;
  s_pattern_step_idx = 0;

  switch (pattern) {
    case MOTOR_PATTERN_SHORT_100MS:
      s_pattern_steps[0] = {true, 100};
      s_pattern_step_count = 1;
      break;

    case MOTOR_PATTERN_MEDIUM_250MS:
      s_pattern_steps[0] = {true, 250};
      s_pattern_step_count = 1;
      break;

    case MOTOR_PATTERN_DOUBLE_100_120MS:
      s_pattern_steps[0] = {true, 100};
      s_pattern_steps[1] = {false, 120};
      s_pattern_steps[2] = {true, 100};
      s_pattern_step_count = 3;
      break;

    case MOTOR_PATTERN_TRIPLE_100_120MS:
      s_pattern_steps[0] = {true, 100};
      s_pattern_steps[1] = {false, 120};
      s_pattern_steps[2] = {true, 100};
      s_pattern_steps[3] = {false, 120};
      s_pattern_steps[4] = {true, 100};
      s_pattern_step_count = 5;
      break;

    case MOTOR_PATTERN_BUTTON_FEEDBACK:
      s_pattern_steps[0] = {true, 100};
      s_pattern_step_count = 1;
      break;

    case MOTOR_PATTERN_NONE:
    default:
      motorStop();
      return false;
  }

  beginMotorTelemetryCapture();

  s_status.motor.current_pattern = pattern;
  s_status.motor.pattern_str = motorPatternToString(pattern);
  s_status.motor.pattern_running = true;
  s_status.motor.step_index = 0;
  s_status.motor.total_steps = s_pattern_step_count;
  s_status.motor.pattern_count++;

  s_pattern_step_idx = 0;
  s_step_start_ms = millis();
  setMotorHardwareLevel(s_pattern_steps[0].on);
  return true;
}

bool motorPulseMs(uint32_t duration_ms) {
  if (duration_ms == 0) {
    motorStop();
    return true;
  }
  if (!s_status.motor.initialized) {
    motorInit();
  }

  // Sicurezza hardware: nessun singolo impulso può superare HW_MOTOR_MAX_ON_MS (1000 ms)
  if (duration_ms > HW_MOTOR_MAX_ON_MS) {
    duration_ms = HW_MOTOR_MAX_ON_MS;
  }

  setMotorHardwareLevel(false);
  s_pattern_steps[0] = {true, (uint16_t)duration_ms};
  s_pattern_step_count = 1;
  s_pattern_step_idx = 0;

  beginMotorTelemetryCapture();

  s_status.motor.current_pattern = MOTOR_PATTERN_CUSTOM_PULSE;
  s_status.motor.pattern_str = motorPatternToString(MOTOR_PATTERN_CUSTOM_PULSE);
  s_status.motor.pattern_running = true;
  s_status.motor.step_index = 0;
  s_status.motor.total_steps = 1;
  s_status.motor.pattern_count++;

  s_step_start_ms = millis();
  setMotorHardwareLevel(true);
  return true;
}

bool motorTriggerShortPulse() {
  return motorStartPattern(MOTOR_PATTERN_SHORT_100MS);
}

bool motorTriggerMediumPulse() {
  return motorStartPattern(MOTOR_PATTERN_MEDIUM_250MS);
}

bool motorTriggerTriplePulse() {
  return motorStartPattern(MOTOR_PATTERN_TRIPLE_100_120MS);
}

bool motorTriggerDoublePulse() {
  return motorStartPattern(MOTOR_PATTERN_DOUBLE_100_120MS);
}

void motorSetButtonFeedbackEnabled(bool enabled) {
  s_status.motor.button_feedback_enabled = enabled;
}

bool motorIsButtonFeedbackEnabled() {
  return s_status.motor.button_feedback_enabled;
}

bool motorIsActive() {
  return s_status.motor.active;
}

void motorUpdate() {
  uint32_t now = millis();

  // Verifica se il timer hardware di sicurezza (esp_timer) è intervenuto
  if (s_watchdog_tripped_isr) {
    s_watchdog_tripped_isr = false;
    s_status.motor.watchdog_trip_count = s_watchdog_trip_count_isr;
    motorStop();
    return;
  }

  // Doppia verifica software oltre all'esp_timer hardware
  if (s_status.motor.active) {
    s_status.motor.current_on_ms = now - s_status.motor.pulse_started_ms;
    if (s_status.motor.current_on_ms >= HW_MOTOR_MAX_ON_MS) {
      s_status.motor.watchdog_trip_count++;
      motorStop();
      return;
    }
  }

  if (!s_status.motor.pattern_running || s_pattern_step_count == 0) {
    return;
  }

  // Campiona telemetria di stabilità VSYS/VBUS/I2C ogni 25 ms durante il pattern
  if ((now - s_last_motor_telemetry_ms) >= 25) {
    s_last_motor_telemetry_ms = now;
    updateMotorTelemetrySample(false);
  }

  const MotorStep &cur = s_pattern_steps[s_pattern_step_idx];
  if ((now - s_step_start_ms) >= cur.duration_ms) {
    s_pattern_step_idx++;
    if (s_pattern_step_idx >= s_pattern_step_count) {
      // Fine sequenza: spegni GPIO16 e acquisisci campione telemetrico post-impulso
      setMotorHardwareLevel(false);
      s_status.motor.pattern_running = false;
      s_status.motor.step_index = s_pattern_step_count;
      s_status.motor.pattern_str = "IDLE";
      updateMotorTelemetrySample(true);
    } else {
      s_status.motor.step_index = s_pattern_step_idx;
      s_step_start_ms = now;
      setMotorHardwareLevel(s_pattern_steps[s_pattern_step_idx].on);
      updateMotorTelemetrySample(false);
    }
  }
}

const MotorStatus &motorGetStatus() {
  return s_status.motor;
}

// ============================================================================
// IMPLEMENTAZIONE API PULSANTE (TEST 3)
// ============================================================================

bool buttonInit() {
  pinMode(HW_BUTTON_PIN, INPUT_PULLUP);
  pinMode(HW_BOOT_BTN_PIN, INPUT_PULLUP);

  // Configura TCA9554 EXIO4 (SYS_OUT / PWR_BTN) e EXIO3 (RTC_INT), EXIO5 (AXP_IRQ) come INPUT
  if (i2cBusLock(150)) {
    tca9554SetPinMode(HW_EXIO_PWR_BTN, INPUT);
    tca9554SetPinMode(HW_EXIO_RTC_INT, INPUT);
    tca9554SetPinMode(HW_EXIO_AXP_IRQ, INPUT);
    i2cBusUnlock();
  }

  uint32_t now = millis();
  int raw_level = digitalRead(HW_BUTTON_PIN);
  bool initial_pressed = (raw_level == LOW);

  memset(&s_status.button, 0, sizeof(s_status.button));
  s_status.button.pin = HW_BUTTON_PIN;
  s_status.button.raw_level = raw_level;
  s_status.button.pressed = initial_pressed;
  s_status.button.last_change_ms = now;
  s_status.button.last_event = BTN_EVENT_NONE;
  s_status.button.state_str = initial_pressed ? "PRESS" : "IDLE";

  s_btn_raw_last = initial_pressed;
  s_btn_raw_change_ms = now;
  s_btn_press_start_ms = now;
  s_btn_long_fired_this_hold = false;

  int boot_raw = digitalRead(HW_BOOT_BTN_PIN);
  s_status.button.boot_pressed = (boot_raw == LOW);
  s_boot_raw_last = s_status.button.boot_pressed;
  s_boot_raw_change_ms = now;

  if (i2cBusLock(100)) {
    int pwr_lvl = tca9554ReadPin(HW_EXIO_PWR_BTN);
    i2cBusUnlock();
    if (pwr_lvl >= 0) {
      s_status.button.pwr_valid = true;
      s_status.button.pwr_pressed = (pwr_lvl == HIGH);
    }
  }

  s_btn_initialized = true;
  return true;
}

void buttonUpdate() {
  if (!s_btn_initialized) {
    buttonInit();
  }

  uint32_t now = millis();

  // Azzeramento flag one-shot del ciclo precedente
  s_status.button.just_pressed = false;
  s_status.button.just_released = false;
  s_status.button.short_press_event = false;
  s_status.button.long_press_event = false;
  s_status.button.boot_just_pressed = false;
  s_status.button.pwr_just_pressed = false;

  // 1. Lettura e debounce (40 ms) pulsante esterno su GPIO44 (INPUT_PULLUP: LOW = premuto)
  int raw_level = digitalRead(HW_BUTTON_PIN);
  s_status.button.raw_level = raw_level;
  bool raw_pressed = (raw_level == LOW);

  if (raw_pressed != s_btn_raw_last) {
    s_btn_raw_last = raw_pressed;
    s_btn_raw_change_ms = now;
  }

  if ((now - s_btn_raw_change_ms) >= HW_BTN_DEBOUNCE_MS && raw_pressed != s_status.button.pressed) {
    s_status.button.pressed = raw_pressed;
    s_status.button.last_change_ms = now;

    if (raw_pressed) {
      // Fronte di PRESSIONE validato
      s_status.button.just_pressed = true;
      s_latched_just_pressed = true;
      s_status.button.press_count++;
      s_btn_press_start_ms = now;
      s_btn_long_fired_this_hold = false;
      s_status.button.long_press_active = false;
      s_status.button.current_hold_ms = 0;
      s_status.button.last_event = BTN_EVENT_PRESSED;
      s_status.button.state_str = "PRESS";

      // Se l'attivazione motorino su pressione pulsante è abilitata, avvia feedback aptico
      if (s_status.motor.button_feedback_enabled) {
        motorStartPattern(MOTOR_PATTERN_BUTTON_FEEDBACK);
      }
    } else {
      // Fronte di RILASCIO validato
      s_status.button.just_released = true;
      s_latched_just_released = true;
      s_status.button.release_count++;

      uint32_t hold_ms = now - s_btn_press_start_ms;
      s_status.button.last_hold_ms = hold_ms;
      s_status.button.current_hold_ms = 0;
      s_status.button.long_press_active = false;

      if (!s_btn_long_fired_this_hold && hold_ms < HW_BTN_LONG_PRESS_MS) {
        s_status.button.short_press_event = true;
        s_latched_short_press = true;
        s_status.button.short_press_count++;
        s_status.button.last_event = BTN_EVENT_SHORT_PRESS;
        s_status.button.state_str = "SHORT";
        s_btn_state_banner_until_ms = now + 500;
      } else {
        s_status.button.long_press_event = true;
        s_latched_long_press = true;
        s_status.button.long_press_count++;
        s_status.button.last_event = BTN_EVENT_LONG_PRESS;
        s_status.button.state_str = "LONG";
        s_btn_state_banner_until_ms = now + 500;
      }
    }
  }

  // Gestione tenuta del pulsante e rilevazione immediata LONG PRESS (>= 1000 ms)
  if (s_status.button.pressed) {
    uint32_t hold_ms = now - s_btn_press_start_ms;
    s_status.button.current_hold_ms = hold_ms;
    if (!s_btn_long_fired_this_hold && hold_ms >= HW_BTN_LONG_PRESS_MS) {
      s_btn_long_fired_this_hold = true;
      s_status.button.long_press_active = true;
      s_status.button.last_hold_ms = hold_ms;
      s_status.button.state_str = "LONG";
    }
  } else {
    s_status.button.current_hold_ms = 0;
    if (s_btn_state_banner_until_ms != 0 && (int32_t)(now - s_btn_state_banner_until_ms) >= 0) {
      s_btn_state_banner_until_ms = 0;
      s_status.button.state_str = "IDLE";
    }
  }

  // 2. Pulsante BOOT onboard (GPIO0, INPUT_PULLUP: LOW = premuto)
  bool boot_raw = (digitalRead(HW_BOOT_BTN_PIN) == LOW);
  if (boot_raw != s_boot_raw_last) {
    s_boot_raw_last = boot_raw;
    s_boot_raw_change_ms = now;
  }
  if ((now - s_boot_raw_change_ms) >= HW_BTN_DEBOUNCE_MS && boot_raw != s_status.button.boot_pressed) {
    s_status.button.boot_pressed = boot_raw;
    if (boot_raw) {
      s_status.button.boot_just_pressed = true;
      s_status.button.boot_press_count++;
    }
  }

  // 3. Pulsante PWR onboard tramite TCA9554 EXIO4 (SYS_OUT: HIGH = premuto, campionato ogni 80 ms)
  if ((now - s_last_pwr_poll_ms) >= 80) {
    s_last_pwr_poll_ms = now;
    if (i2cBusLock(50)) {
      int pwr_lvl = tca9554ReadPin(HW_EXIO_PWR_BTN);
      i2cBusUnlock();
      if (pwr_lvl >= 0) {
        s_status.button.pwr_valid = true;
        bool pwr_now = (pwr_lvl == HIGH);
        if (pwr_now && !s_status.button.pwr_pressed) {
          s_status.button.pwr_just_pressed = true;
          s_status.button.pwr_press_count++;
        }
        s_status.button.pwr_pressed = pwr_now;
      } else {
        s_status.button.pwr_valid = false;
      }
    }
  }
}

const ButtonStatus &buttonGetStatus() {
  return s_status.button;
}

bool buttonConsumeShortPress() {
  bool ev = s_latched_short_press;
  s_latched_short_press = false;
  return ev;
}

bool buttonConsumeLongPress() {
  bool ev = s_latched_long_press;
  s_latched_long_press = false;
  return ev;
}

bool buttonConsumeJustPressed() {
  bool ev = s_latched_just_pressed;
  s_latched_just_pressed = false;
  return ev;
}

bool buttonConsumeJustReleased() {
  bool ev = s_latched_just_released;
  s_latched_just_released = false;
  return ev;
}

void buttonResetCounters() {
  s_status.button.press_count = 0;
  s_status.button.release_count = 0;
  s_status.button.short_press_count = 0;
  s_status.button.long_press_count = 0;
  s_status.button.last_hold_ms = 0;
  s_status.button.boot_press_count = 0;
  s_status.button.pwr_press_count = 0;
  s_latched_just_pressed = false;
  s_latched_just_released = false;
  s_latched_short_press = false;
  s_latched_long_press = false;
}

// ============================================================================
// IMPLEMENTAZIONE API PMIC AXP2101 (0x34) & RTC PCF85063 (0x51) (TEST 8)
// ============================================================================

bool pmicInit() {
  memset(&s_status.pmic, 0, sizeof(s_status.pmic));
  s_status.pmic.i2c_addr = HW_ADDR_AXP2101;
  s_status.pmic.batt_percent = -1;
  s_status.pmic.charger_status_str = "not_charging";

  if (!i2cBusLock(250)) {
    return false;
  }

  // Inizializza XPowersPMU su 0x34 usando callback su Wire protetti da mutex
  // (evita di richiamare Wire.begin() una seconda volta sul bus condiviso)
  bool ok = s_pmu.begin(HW_ADDR_AXP2101, pmuReadRegCallback, pmuWriteRegCallback);
  if (!ok) {
    i2cBusUnlock();
    s_status.pmic.online = false;
    return false;
  }

  s_status.pmic.online = true;
  s_status.pmic.chip_id = s_pmu.getChipID();

  // Abilita canali ADC interni del PMIC AXP2101 (come da 05_LVGL_AXP2101_ADC_Data.ino e port_axp2101.cpp)
  s_pmu.enableTemperatureMeasure();
  s_pmu.enableBattDetection();
  s_pmu.enableVbusVoltageMeasure();
  s_pmu.enableBattVoltageMeasure();
  s_pmu.enableSystemVoltageMeasure();

  // Disabilita misura pin TS (connettore batteria J1 a 2 pin privo di NTC, da port_axp2101.cpp riga 114)
  s_pmu.disableTSPinMeasure();
  s_status.pmic.ts_pin_measure_disabled = true;

  // Configurazione IRQ pulita come da esempio ufficiale Waveshare
  s_pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
  s_pmu.clearIrqStatus();
  s_pmu.enableIRQ(
      XPOWERS_AXP2101_BAT_INSERT_IRQ | XPOWERS_AXP2101_BAT_REMOVE_IRQ |
      XPOWERS_AXP2101_VBUS_INSERT_IRQ | XPOWERS_AXP2101_VBUS_REMOVE_IRQ |
      XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ |
      XPOWERS_AXP2101_BAT_CHG_DONE_IRQ | XPOWERS_AXP2101_BAT_CHG_START_IRQ);

  // Parametri di carica standard e sicuri da esempio ufficiale Waveshare (port_axp2101.cpp righe 130-137):
  // NON viene modificata alcuna tensione dei rail DC/ALDO/BLDO!
  s_pmu.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
  s_pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_400MA); // 400mA (~0.27C per LiPo 1500mAh)
  s_pmu.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_25MA);
  s_pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);

  s_status.pmic.precharge_curr_ma = 50;
  s_status.pmic.constant_curr_ma = 400;
  s_status.pmic.termination_curr_ma = 25;
  s_status.pmic.target_voltage_mv = 4200;

  i2cBusUnlock();

  return pmicUpdate();
}

bool pmicUpdate() {
  if (!s_status.pmic.online) {
    return false;
  }

  if (!i2cBusLock(200)) {
    s_status.pmic.read_error_count++;
    return false;
  }

  s_status.pmic.chip_id = s_pmu.getChipID();
  s_status.pmic.vbus_good = s_pmu.isVbusGood();
  s_status.pmic.vbus_present = s_pmu.isVbusIn();
  s_status.pmic.vbus_mv = s_status.pmic.vbus_present ? s_pmu.getVbusVoltage() : 0;
  s_status.pmic.vsys_mv = s_pmu.getSystemVoltage();

  s_status.pmic.battery_present = s_pmu.isBatteryConnect();
  if (s_status.pmic.battery_present) {
    s_status.pmic.batt_mv = s_pmu.getBattVoltage();
    s_status.pmic.batt_percent = s_pmu.getBatteryPercent();
  } else {
    s_status.pmic.batt_mv = 0;
    s_status.pmic.batt_percent = -1;
  }

  s_status.pmic.is_charging = s_pmu.isCharging();
  s_status.pmic.is_discharging = s_pmu.isDischarge();
  s_status.pmic.is_standby = s_pmu.isStandby();

  uint8_t chg_stat = (uint8_t)s_pmu.getChargerStatus();
  s_status.pmic.charger_status = chg_stat;
  s_status.pmic.charger_status_str = chargerStatusToString(chg_stat);

  s_status.pmic.temp_c = s_pmu.getTemperature();

  // Lettura (SOLA LETTURA!) dello stato di abilitazione e tensione dei rail richiesti
  s_status.pmic.dc1_enabled = s_pmu.isEnableDC1();
  s_status.pmic.dc1_mv = s_pmu.getDC1Voltage();

  s_status.pmic.dc3_enabled = s_pmu.isEnableDC3();
  s_status.pmic.dc3_mv = s_pmu.getDC3Voltage();

  s_status.pmic.aldo1_enabled = s_pmu.isEnableALDO1();
  s_status.pmic.aldo1_mv = s_pmu.getALDO1Voltage();

  s_status.pmic.aldo2_enabled = s_pmu.isEnableALDO2();
  s_status.pmic.aldo2_mv = s_pmu.getALDO2Voltage();

  s_status.pmic.aldo3_enabled = s_pmu.isEnableALDO3();
  s_status.pmic.aldo3_mv = s_pmu.getALDO3Voltage();

  s_status.pmic.aldo4_enabled = s_pmu.isEnableALDO4();
  s_status.pmic.aldo4_mv = s_pmu.getALDO4Voltage();

  s_status.pmic.bldo1_enabled = s_pmu.isEnableBLDO1();
  s_status.pmic.bldo1_mv = s_pmu.getBLDO1Voltage();

  s_status.pmic.bldo2_enabled = s_pmu.isEnableBLDO2();
  s_status.pmic.bldo2_mv = s_pmu.getBLDO2Voltage();

  s_status.pmic.rails[0] = {"DC1", s_status.pmic.dc1_enabled, s_status.pmic.dc1_mv};
  s_status.pmic.rails[1] = {"DC3", s_status.pmic.dc3_enabled, s_status.pmic.dc3_mv};
  s_status.pmic.rails[2] = {"ALDO1", s_status.pmic.aldo1_enabled, s_status.pmic.aldo1_mv};
  s_status.pmic.rails[3] = {"ALDO2", s_status.pmic.aldo2_enabled, s_status.pmic.aldo2_mv};
  s_status.pmic.rails[4] = {"ALDO3", s_status.pmic.aldo3_enabled, s_status.pmic.aldo3_mv};
  s_status.pmic.rails[5] = {"ALDO4", s_status.pmic.aldo4_enabled, s_status.pmic.aldo4_mv};
  s_status.pmic.rails[6] = {"BLDO1", s_status.pmic.bldo1_enabled, s_status.pmic.bldo1_mv};
  s_status.pmic.rails[7] = {"BLDO2", s_status.pmic.bldo2_enabled, s_status.pmic.bldo2_mv};

  s_status.pmic.last_update_ms = millis();

  i2cBusUnlock();
  return true;
}

const PmicStatus &pmicGetStatus() {
  return s_status.pmic;
}

bool rtcInit() {
  memset(&s_status.rtc, 0, sizeof(s_status.rtc));
  s_status.rtc.i2c_addr = HW_ADDR_PCF85063;
  snprintf(s_status.rtc.datetime_str, sizeof(s_status.rtc.datetime_str), "N/A");

  if (!i2cBusLock(200)) {
    return false;
  }

  bool ok = s_rtc.begin(rtcI2cCallback);
  if (!ok) {
    i2cBusUnlock();
    s_status.rtc.online = false;
    return false;
  }

  s_status.rtc.online = true;
  i2cBusUnlock();

  return rtcUpdate();
}

bool rtcUpdate() {
  if (!s_status.rtc.online) {
    return false;
  }

  if (!i2cBusLock(150)) {
    return false;
  }

  s_status.rtc.running = s_rtc.isRunning();
  s_status.rtc.clock_integrity_ok = s_rtc.isClockIntegrityGuaranteed();
  RTC_DateTime dt = s_rtc.getDateTime();

  i2cBusUnlock();

  s_status.rtc.year = dt.getYear();
  s_status.rtc.month = dt.getMonth();
  s_status.rtc.day = dt.getDay();
  s_status.rtc.hour = dt.getHour();
  s_status.rtc.minute = dt.getMinute();
  s_status.rtc.second = dt.getSecond();
  s_status.rtc.weekday = dt.getWeek();

  // Do not invent a timestamp when the RTC is invalid.
  snprintf(
      s_status.rtc.datetime_str,
      sizeof(s_status.rtc.datetime_str),
      "%04u-%02u-%02u %02u:%02u:%02u",
      (unsigned)s_status.rtc.year,
      (unsigned)s_status.rtc.month,
      (unsigned)s_status.rtc.day,
      (unsigned)s_status.rtc.hour,
      (unsigned)s_status.rtc.minute,
      (unsigned)s_status.rtc.second);
  s_status.rtc.last_update_ms = millis();
  return true;
}

const RtcStatus &rtcGetStatus() {
  return s_status.rtc;
}

// ============================================================================
// FUNZIONI AGGREGATE DEL MODULO PERIPHERALS
// ============================================================================

bool peripheralsInit() {
  motorInit();
  buttonInit();
  bool pmic_ok = pmicInit();
  rtcInit();
  s_last_pmic_poll_ms = millis();
  s_last_rtc_poll_ms = millis();
  return pmic_ok;
}

void peripheralsUpdate() {
  buttonUpdate();
  motorUpdate();

  uint32_t now = millis();
  if ((now - s_last_pmic_poll_ms) >= 500) {
    s_last_pmic_poll_ms = now;
    pmicUpdate();
  }
  if ((now - s_last_rtc_poll_ms) >= 1000) {
    s_last_rtc_poll_ms = now;
    rtcUpdate();
  }
}

const PeripheralsStatus &peripheralsGetStatus() {
  return s_status;
}

// ============================================================================
// HELPER DIAGNOSTICI PER STAMPA SERIALE (TEST 3, TEST 4, TEST 8)
// ============================================================================

void peripheralsPrintButtonDiagnostics(Stream &out) {
  const ButtonStatus &b = s_status.button;
  out.println(F("=== [TEST 3] DIAGNOSTICA PULSANTE ESTERNO (GPIO44) & ONBOARD ==="));
  out.printf(
      "  GPIO%u (Esterno, INPUT_PULLUP): raw=%s (%d) | debounced=%s | state=%s\n",
      b.pin,
      b.raw_level == LOW ? "LOW" : "HIGH",
      b.raw_level,
      b.pressed ? "PRESSED" : "RELEASED",
      b.state_str ? b.state_str : "IDLE");
  out.printf(
      "  Contatori GPIO%u: press=%lu | release=%lu | short(<1000ms)=%lu | long(>=1000ms)=%lu\n",
      b.pin,
      (unsigned long)b.press_count,
      (unsigned long)b.release_count,
      (unsigned long)b.short_press_count,
      (unsigned long)b.long_press_count);
  out.printf(
      "  Durata tenuta: current=%lu ms | last=%lu ms (debounce=%u ms, long_thr=%u ms)\n",
      (unsigned long)b.current_hold_ms,
      (unsigned long)b.last_hold_ms,
      (unsigned)HW_BTN_DEBOUNCE_MS,
      (unsigned)HW_BTN_LONG_PRESS_MS);
  out.printf(
      "  Onboard BOOT (GPIO0): %s (count=%lu) | Onboard PWR (TCA9554 EXIO4): %s (valid=%s, count=%lu)\n",
      b.boot_pressed ? "PRESSED" : "RELEASED",
      (unsigned long)b.boot_press_count,
      b.pwr_pressed ? "PRESSED" : "RELEASED",
      b.pwr_valid ? "YES" : "NO",
      (unsigned long)b.pwr_press_count);
}

void peripheralsPrintMotorDiagnostics(Stream &out) {
  const MotorStatus &m = s_status.motor;
  out.println(F("=== [TEST 4] DIAGNOSTICA MOTORINO VIBRAZIONE GROVE V1.3 (GPIO16) ==="));
  out.printf(
      "  GPIO%u: init=%s | active=%s | level=%s (%d) | pattern=%s (running=%s)\n",
      m.pin,
      m.initialized ? "YES" : "NO",
      m.active ? "ON" : "OFF",
      m.pin_level ? "HIGH" : "LOW",
      m.pin_level,
      m.pattern_str ? m.pattern_str : "IDLE",
      m.pattern_running ? "YES" : "NO");
  out.printf(
      "  Sicurezza: max_watchdog=%lu ms | watchdog_trips=%lu | activations=%lu | patterns=%lu | btn_feedback=%s\n",
      (unsigned long)m.max_watchdog_ms,
      (unsigned long)m.watchdog_trip_count,
      (unsigned long)m.activation_count,
      (unsigned long)m.pattern_count,
      m.button_feedback_enabled ? "ON" : "OFF");
  out.printf(
      "  Telemetria alimentazione: VSYS before=%u mV, min_during=%u mV, after=%u mV, max_drop=%d mV\n",
      m.vsys_before_mv,
      m.vsys_min_during_mv,
      m.vsys_after_mv,
      (int)m.vsys_max_drop_mv);
  out.printf(
      "  Telemetria VBUS & I2C:    VBUS before=%u mV, min_during=%u mV, after=%u mV | I2C err delta=%lu (before=%lu, after=%lu)\n",
      m.vbus_before_mv,
      m.vbus_min_during_mv,
      m.vbus_after_mv,
      (unsigned long)m.i2c_errors_delta,
      (unsigned long)m.i2c_errors_before,
      (unsigned long)m.i2c_errors_during);
  out.printf(
      "  Anomalie rilevate:        voltage_dip=%s | i2c_error=%s\n",
      m.voltage_dip_anomaly ? "ANOMALY" : "NONE",
      m.i2c_error_anomaly ? "ANOMALY" : "NONE");
}

void peripheralsPrintPmicRtcDiagnostics(Stream &out) {
  const PmicStatus &p = s_status.pmic;
  const RtcStatus &r = s_status.rtc;

  out.println(F("=== [TEST 8] DIAGNOSTICA PMIC AXP2101 (0x34) & RTC PCF85063 (0x51) ==="));
  out.printf(
      "  AXP2101 (0x%02X): online=%s | chip_id=0x%02X | die_temp=%.2f C | TS_pin_disabled=%s | i2c_read_err=%lu\n",
      p.i2c_addr,
      p.online ? "YES" : "NO",
      p.chip_id,
      p.temp_c,
      p.ts_pin_measure_disabled ? "YES" : "NO",
      (unsigned long)p.read_error_count);
  out.printf(
      "  VBUS: present=%s | good=%s | voltage=%u mV | VSYS=%u mV\n",
      p.vbus_present ? "YES" : "NO",
      p.vbus_good ? "YES" : "NO",
      p.vbus_mv,
      p.vsys_mv);
  out.printf(
      "  BATTERIA: present=%s | voltage=%u mV | percent=%d%% | charging=%s | discharging=%s | standby=%s | status=%s\n",
      p.battery_present ? "YES" : "NO",
      p.batt_mv,
      p.batt_percent,
      p.is_charging ? "YES" : "NO",
      p.is_discharging ? "YES" : "NO",
      p.is_standby ? "YES" : "NO",
      p.charger_status_str ? p.charger_status_str : "unknown");
  out.printf(
      "  Configurazione Carica: precharge=%u mA | CC=%u mA (~0.27C su 1500mAh) | Iterm=%u mA | CV=%u mV\n",
      p.precharge_curr_ma,
      p.constant_curr_ma,
      p.termination_curr_ma,
      p.target_voltage_mv);
  out.println(F("  Rail Alimentazione AXP2101 (SOLA LETTURA):"));
  for (size_t i = 0; i < 8; ++i) {
    out.printf(
        "    %-6s : %s (%u mV)\n",
        p.rails[i].name,
        p.rails[i].enabled ? "ON " : "OFF",
        p.rails[i].voltage_mv);
  }
  out.printf(
      "  RTC PCF85063 (0x%02X): online=%s | running=%s | integrity_ok=%s | datetime=%s\n",
      r.i2c_addr,
      r.online ? "YES" : "NO",
      r.running ? "YES" : "NO",
      r.clock_integrity_ok ? "YES" : "NO",
      r.datetime_str);
}
