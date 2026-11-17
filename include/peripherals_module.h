#pragma once

/**
 * @file peripherals_module.h
 * @brief Gestione periferiche Dragon Radar:
 *        - TEST 3: Pulsante esterno su GPIO44 (INPUT_PULLUP, debounce 40ms, short/long press)
 *                  + diagnostica pulsanti onboard BOOT (GPIO0) e PWR (TCA9554 EXIO4 / SYS_OUT)
 *        - TEST 4: Motorino di vibrazione Grove V1.3 su GPIO16 (attivo HIGH, LOW al boot,
 *                  controller non bloccante + watchdog hardware esp_timer max 1000ms + telemetria VSYS/VBUS/I2C)
 *        - TEST 8: PMIC AXP2101 (0x34) e RTC PCF85063 (0x51) su bus Wire condiviso protetto da mutex.
 */

#include <Arduino.h>
#include <stdbool.h>
#include <stdint.h>

#include "hw_config.h"
#include "i2c_bus.h"

// ============================================================================
// 1. TEST 3 - PULSANTE ESTERNO (GPIO44) & PULSANTI ONBOARD (BOOT GPIO0, PWR EXIO4)
// ============================================================================

enum ButtonEventType : uint8_t {
  BTN_EVENT_NONE = 0,
  BTN_EVENT_PRESSED,
  BTN_EVENT_SHORT_PRESS,
  BTN_EVENT_LONG_PRESS,
  BTN_EVENT_RELEASED
};

struct ButtonStatus {
  // Pulsante esterno principale su GPIO44 (INPUT_PULLUP: LOW=premuto, HIGH=rilasciato)
  uint8_t pin;                  // HW_BUTTON_PIN (44)
  int raw_level;                // Livello elettrico istantaneo (LOW=0, HIGH=1)
  bool pressed;                 // Stato debounced corrente (true = premuto)
  bool just_pressed;            // Fronte di pressione appena validato dal debounce
  bool just_released;           // Fronte di rilascio appena validato dal debounce
  bool short_press_event;       // Evento pressione breve (< 1000 ms, generato al rilascio)
  bool long_press_event;        // Evento pressione lunga (>= 1000 ms, generato appena superata la soglia)
  bool long_press_active;       // True mentre il tasto è ancora tenuto oltre la soglia di long press
  uint32_t press_count;         // Contatore totale pressioni validate
  uint32_t release_count;       // Contatore totale rilasci validati
  uint32_t short_press_count;   // Contatore pressioni brevi (< 1000 ms)
  uint32_t long_press_count;    // Contatore pressioni lunghe (>= 1000 ms)
  uint32_t current_hold_ms;     // Durata pressione corrente (ms, 0 se rilasciato)
  uint32_t last_hold_ms;        // Durata dell'ultima pressione (ms)
  uint32_t last_change_ms;      // Timestamp millis() dell'ultima transizione debounced
  ButtonEventType last_event;   // Ultimo evento significativo registrato
  const char *state_str;        // "IDLE", "PRESS", "SHORT", "LONG" (per UI Dragon Radar)

  // Pulsante BOOT onboard (GPIO0, INPUT_PULLUP: LOW=premuto)
  bool boot_pressed;
  bool boot_just_pressed;
  uint32_t boot_press_count;

  // Pulsante PWR onboard (TCA9554 EXIO4 / SYS_OUT: HIGH=premuto, LOW=rilasciato)
  bool pwr_valid;
  bool pwr_pressed;
  bool pwr_just_pressed;
  uint32_t pwr_press_count;
};

// ============================================================================
// 2. TEST 4 - MOTORINO DI VIBRAZIONE GROVE V1.3 SU GPIO16
// ============================================================================

enum MotorPattern : uint8_t {
  MOTOR_PATTERN_NONE = 0,
  MOTOR_PATTERN_SHORT_100MS,      // 1. Impulso breve di 100 ms
  MOTOR_PATTERN_MEDIUM_250MS,     // 2. Impulso medio di 250 ms
  MOTOR_PATTERN_TRIPLE_100_120MS, // 3. Sequenza 3 x (100 ms ON / 120 ms OFF)
  MOTOR_PATTERN_BUTTON_FEEDBACK,  // 4. Attivazione su pressione del pulsante
  MOTOR_PATTERN_CUSTOM_PULSE      // Impulso singolo custom (sempre limitato a <= 1000 ms)
};

struct MotorStatus {
  uint8_t pin;                    // HW_MOTOR_PIN (16)
  bool initialized;               // True dopo motorInit()
  bool active;                    // True quando GPIO16 è pilotato HIGH (motore in rotazione)
  bool pattern_running;           // True mentre una sequenza (anche in pausa OFF tra impulsi) è in corso
  int pin_level;                  // Livello elettrico corrente su GPIO16 (0=LOW, 1=HIGH)
  MotorPattern current_pattern;   // Pattern corrente o ultimo eseguito
  const char *pattern_str;        // Nome leggibile del pattern
  bool button_feedback_enabled;   // Se true, una pressione di GPIO44 attiva un impulso aptico
  uint8_t step_index;             // Step corrente nel pattern
  uint8_t total_steps;            // Numero totale di step nel pattern
  uint32_t pulse_started_ms;      // Timestamp millis() di inizio impulso ON corrente
  uint32_t current_on_ms;         // Durata ON continua corrente (ms)
  uint32_t max_watchdog_ms;       // Soglia hardware watchdog (HW_MOTOR_MAX_ON_MS = 1000 ms)
  uint32_t activation_count;      // Numero totale di attivazioni ON eseguite
  uint32_t pattern_count;         // Numero totale di pattern avviati
  uint32_t watchdog_trip_count;   // Numero di interventi del timer hardware di sicurezza (0 = nominale)

  // Telemetria di stabilità alimentazione e bus I2C prima/durante/dopo attivazione motorino
  uint16_t vsys_before_mv;        // VSYS (mV) letta prima dell'attivazione
  uint16_t vsys_min_during_mv;    // VSYS minima (mV) osservata durante la vibrazione
  uint16_t vsys_after_mv;         // VSYS (mV) letta dopo lo spegnimento
  int16_t vsys_max_drop_mv;       // Massima caduta VSYS (vsys_before_mv - vsys_min_during_mv)
  uint16_t vbus_before_mv;        // VBUS (mV) letta prima dell'attivazione
  uint16_t vbus_min_during_mv;    // VBUS minima (mV) osservata durante la vibrazione
  uint16_t vbus_after_mv;         // VBUS (mV) letta dopo lo spegnimento
  uint32_t i2c_errors_before;     // Contatore errori I2C prima dell'attivazione
  uint32_t i2c_errors_during;     // Contatore errori I2C durante/dopo l'attivazione
  uint32_t i2c_errors_delta;      // Nuovi errori I2C insorti durante la vibrazione
  bool voltage_dip_anomaly;       // True se VSYS scende sotto soglia critica (< 3000 mV) o drop > 450 mV
  bool i2c_error_anomaly;         // True se i2c_errors_delta > 0 durante la vibrazione
};

// ============================================================================
// 3. TEST 8 - PMIC AXP2101 (0x34) & RTC PCF85063 (0x51)
// ============================================================================

struct PmicRailInfo {
  const char *name;               // "DC1", "DC3", "ALDO1".."ALDO4", "BLDO1".."BLDO2"
  bool enabled;                   // Stato abilitazione rail (sola lettura)
  uint16_t voltage_mv;            // Tensione configurata letta dal registro (mV, sola lettura)
};

struct PmicStatus {
  bool online;                    // True se AXP2101 risponde a 0x34 e chip_id è valido
  uint8_t i2c_addr;               // 0x34 (HW_ADDR_AXP2101)
  uint8_t chip_id;                // Chip ID letto da registro 0x03 (atteso 0x4A = XPOWERS_AXP2101_CHIP_ID)

  bool vbus_present;              // VBUS inserita (isVbusIn)
  bool vbus_good;                 // VBUS stabile (isVbusGood)
  uint16_t vbus_mv;               // Tensione VBUS (mV, 0 se assente)
  uint16_t vsys_mv;               // Tensione di sistema VSYS (mV)

  bool battery_present;           // Batteria LiPo rilevata (isBatteryConnect)
  uint16_t batt_mv;               // Tensione batteria (mV, 0 se scollegata)
  int batt_percent;               // Percentuale carica (0..100%, -1 se batteria scollegata)

  bool is_charging;               // In carica
  bool is_discharging;            // In scarica (alimentazione da batteria)
  bool is_standby;                // Standby (né carica né scarica)
  uint8_t charger_status;         // Codice stato caricatore (xpowers_chg_status_t)
  const char *charger_status_str; // "tri_charge", "pre_charge", "cc_charge", "cv_charge", "charge_done", "not_charging"

  float temp_c;                   // Temperatura interna die AXP2101 (°C)
  bool ts_pin_measure_disabled;   // True dopo disableTSPinMeasure() (connettore 2-pin senza NTC)

  // Parametri di carica prudenti verificati/impostati (sola verifica/impostazione standard Waveshare)
  uint16_t precharge_curr_ma;     // 50 mA
  uint16_t constant_curr_ma;      // 400 mA (~0.27C su LiPo 1500mAh)
  uint16_t termination_curr_ma;   // 25 mA
  uint16_t target_voltage_mv;     // 4200 mV (4.20V)

  // Stato abilitazione e tensione letta (SOLA LETTURA) dei rail richiesti
  bool dc1_enabled;
  uint16_t dc1_mv;
  bool dc3_enabled;
  uint16_t dc3_mv;
  bool aldo1_enabled;
  uint16_t aldo1_mv;
  bool aldo2_enabled;
  uint16_t aldo2_mv;
  bool aldo3_enabled;
  uint16_t aldo3_mv;
  bool aldo4_enabled;
  uint16_t aldo4_mv;
  bool bldo1_enabled;
  uint16_t bldo1_mv;
  bool bldo2_enabled;
  uint16_t bldo2_mv;

  PmicRailInfo rails[8];          // Array indicizzato [DC1, DC3, ALDO1, ALDO2, ALDO3, ALDO4, BLDO1, BLDO2]
  uint32_t last_update_ms;        // Timestamp ultimo campionamento PMIC
  uint32_t read_error_count;      // Contatore letture I2C PMIC fallite
};

struct RtcStatus {
  bool online;                    // True se PCF85063 a 0x51 risponde e supera verifica RAM
  uint8_t i2c_addr;               // 0x51 (HW_ADDR_PCF85063)
  bool running;                   // Clock attivo (STOP bit = 0)
  bool clock_integrity_ok;        // Bit OS (Oscillator Stop) = 0
  uint16_t year;                  // Anno (es. 2026)
  uint8_t month;                  // Mese (1..12)
  uint8_t day;                    // Giorno (1..31)
  uint8_t hour;                   // Ora (0..23)
  uint8_t minute;                 // Minuto (0..59)
  uint8_t second;                 // Secondo (0..59)
  uint8_t weekday;                // Giorno settimana (0..6)
  char datetime_str[24];          // "YYYY-MM-DD HH:MM:SS"
  uint32_t last_update_ms;
};

struct PeripheralsStatus {
  ButtonStatus button;
  MotorStatus motor;
  PmicStatus pmic;
  RtcStatus rtc;
};

// ============================================================================
// API PUBBLICA DEL MODULO PERIPHERALS
// ============================================================================

/**
 * @brief Primissima istruzione di sicurezza da chiamare all'inizio di setup():
 *        configura GPIO16 (HW_MOTOR_PIN) come OUTPUT e lo forza immediatamente a LOW.
 */
void motorSafePreInit();

/**
 * @brief Inizializza tutte le periferiche del modulo (Motorino GPIO16 + esp_timer watchdog,
 *        Pulsante GPIO44 + GPIO0 + TCA9554 EXIO4, PMIC AXP2101 0x34, RTC PCF85063 0x51).
 * @return true se il PMIC AXP2101 e i GPIO critici sono inizializzati correttamente.
 */
bool peripheralsInit();

/**
 * @brief Aggiornamento ciclico non bloccante da chiamare nel loop() principale.
 *        Aggiorna debounce pulsante, macchina a stati motorino + telemetria, PMIC e RTC.
 */
void peripheralsUpdate();

/**
 * @brief Restituisce lo snapshot completo e aggiornato di tutte le periferiche.
 */
const PeripheralsStatus &peripheralsGetStatus();

// --- API Pulsante (TEST 3) ---
bool buttonInit();
void buttonUpdate();
const ButtonStatus &buttonGetStatus();
bool buttonConsumeShortPress();
bool buttonConsumeLongPress();
bool buttonConsumeJustPressed();
bool buttonConsumeJustReleased();
void buttonResetCounters();

// --- API Motorino di Vibrazione (TEST 4) ---
bool motorInit();
void motorUpdate();
void motorStop();
bool motorPulseMs(uint32_t duration_ms);
bool motorStartPattern(MotorPattern pattern);
bool motorTriggerShortPulse();   // Pattern 1: 100 ms ON
bool motorTriggerMediumPulse();  // Pattern 2: 250 ms ON
bool motorTriggerTriplePulse();  // Pattern 3: 3 x (100 ms ON / 120 ms OFF)
void motorSetButtonFeedbackEnabled(bool enabled);
bool motorIsButtonFeedbackEnabled();
bool motorIsActive();
const MotorStatus &motorGetStatus();

// --- API PMIC AXP2101 & RTC PCF85063 (TEST 8) ---
bool pmicInit();
bool pmicUpdate();
const PmicStatus &pmicGetStatus();

bool rtcInit();
bool rtcUpdate();
const RtcStatus &rtcGetStatus();

// --- Helper diagnostici per report seriale (TEST 3, TEST 4, TEST 8) ---
void peripheralsPrintButtonDiagnostics(Stream &out = Serial);
void peripheralsPrintMotorDiagnostics(Stream &out = Serial);
void peripheralsPrintPmicRtcDiagnostics(Stream &out = Serial);
