#pragma once

/**
 * @file sensors_module.h
 * @brief Gestione sensori magnetici e inerziali Dragon Radar:
 *        - TEST 5: Bussola magnetica 3 assi Seeed Grove BMM150 V2.0 (0x13, fallback 0x11)
 *                  su bus I2C condiviso (TP3=SDA=15, TP2=SCL=14), verifica Chip ID (0x32 su reg 0x40),
 *                  lettura trim registers Bosch, campionamento non bloccante ~20-25 Hz,
 *                  metriche di stabilità (std-dev / picco-picco su finestra mobile),
 *                  calibrazione magnetometrica 3D (hard-iron + soft-iron) con persistenza NVS ("dr_mag_cal").
 *        - TEST 6: Procedura di misura delle interferenze magnetiche del motorino di vibrazione
 *                  (Fase A Motor OFF baseline, Fase B Motor ON in vibrazione, Fase C Motor OFF post-vibrazione
 *                  e quantificazione offset statico al variare della distanza: >=20cm, 10cm, 5cm, 2cm).
 *        - TEST 9: IMU 6 assi onboard QMI8658C (0x6B) su bus Wire condiviso (ACC_RANGE_4G, GYR_RANGE_256DPS,
 *                  LPF_MODE_0), calcolo Roll/Pitch con filtro complementare e trasformazione assi
 *                  QMI8658 <-> BMM150 per il calcolo dell'heading compensato per inclinazione (0-359 deg).
 */

#include <Arduino.h>
#include <stdbool.h>
#include <stdint.h>

#include "hw_config.h"
#include "i2c_bus.h"
#include "bmm150.h"

struct RadarSystemSnapshot;

// Namespace NVS richiesto per la calibrazione magnetometrica
#define SENSORS_NVS_NAMESPACE       "dr_mag_cal"
#define SENSORS_BMM_POLL_INTERVAL_MS 40   // 25 Hz (intervallo 40 ms -> target ~20-25 Hz)
#define SENSORS_IMU_POLL_INTERVAL_MS 20   // 50 Hz per filtro complementare roll/pitch fluido
#define SENSORS_STATS_WINDOW_SIZE    32   // Finestra mobile per deviazione standard e rumore picco-picco
#define SENSORS_INTERF_MAX_STEPS     5    // Step di distanza (>=20cm, 10cm, 5cm, 2cm, custom/breadboard)

// ============================================================================
// 1. CALIBRAZIONE MAGNETOMETRICA (Hard-Iron + Soft-Iron + NVS "dr_mag_cal")
// ============================================================================

struct MagCalibrationData {
  bool valid;               // true = calibrazione valida applicata ([CAL]), false = [RAW]
  bool loaded_from_nvs;     // true = caricata da NVS "dr_mag_cal" all'avvio
  bool in_progress;         // true = acquisizione min/max in corso (rotazione a 8 del dispositivo)

  // Valori estremi rilevati durante la calibrazione (in uT compensati Bosch)
  float min_x;
  float min_y;
  float min_z;
  float max_x;
  float max_y;
  float max_z;

  // Escursioni picco-picco per asse (delta = max - min, in uT)
  float span_x;
  float span_y;
  float span_z;

  // Correzione Hard-Iron: offset_i = (max_i + min_i) / 2.0f (uT)
  float offset_x;
  float offset_y;
  float offset_z;

  // Correzione Soft-Iron: scale_i = avg_delta / delta_i (adimensionale, protetto da div/0)
  float scale_x;
  float scale_y;
  float scale_z;

  uint32_t sample_count;    // Campioni raccolti durante la sessione di calibrazione
  uint32_t started_ms;      // Timestamp avvio calibrazione
  uint32_t elapsed_ms;      // Durata sessione di calibrazione (ms)
};

// ============================================================================
// 2. TEST 5 - STATO BUSSOLA BOSCH BMM150 (0x13, fallback 0x11)
// ============================================================================

struct Bmm150TrimSnapshot {
  int8_t dig_x1;
  int8_t dig_y1;
  int8_t dig_x2;
  int8_t dig_y2;
  uint16_t dig_z1;
  int16_t dig_z2;
  int16_t dig_z3;
  int16_t dig_z4;
  uint8_t dig_xy1;
  int8_t dig_xy2;
  uint16_t dig_xyz1;
  bool trim_valid;          // true se dig_xyz1 != 0 e dig_z1 != 0
};

struct Bmm150Status {
  bool online;              // true se BMM150 risponde su 0x13 (o 0x11) con Chip ID == 0x32
  uint8_t i2c_addr;         // Indirizzo attivo (0x13 default, 0x11 alternativo)
  uint8_t chip_id;          // Chip ID letto da reg 0x40 (atteso 0x32)
  uint8_t op_mode;          // BMM150_NORMAL_MODE (0x00)
  uint8_t preset_mode;      // BMM150_PRESETMODE_HIGHACCURACY / REGULAR
  uint8_t odr_code;         // BMM150_DATA_RATE_25HZ (0x06) o 20HZ (0x05)
  Bmm150TrimSnapshot trim;  // Registri di trim di fabbrica Bosch

  // Valori grezzi (registri 0x42..0x49)
  int16_t raw_x;
  int16_t raw_y;
  int16_t raw_z;
  uint16_t raw_rhall;

  // Valori compensati Bosch (prima della calibrazione hard/soft-iron utente)
  int16_t comp_x_int_ut;
  int16_t comp_y_int_ut;
  int16_t comp_z_int_ut;
  float raw_x_ut;           // Compensato float sub-uT (non calibrato)
  float raw_y_ut;
  float raw_z_ut;
  float raw_b_norm_ut;      // Modulo |B| non calibrato (uT)

  // Valori finali in uT (con correzione hard-iron e soft-iron se mag_calibrated == true)
  float x_ut;
  float y_ut;
  float z_ut;
  float b_norm_ut;          // Modulo |B| = sqrt(X^2 + Y^2 + Z^2) (uT)

  // Angoli di direzione magnetica (0.0 .. 359.9 deg)
  float heading_raw_deg;    // Heading preliminare RAW (non calibrato, atan2)
  float heading_cal_deg;    // Heading 2D calibrato (hard/soft-iron applicati)
  float heading_tilt_deg;   // Heading compensato per inclinazione tramite IMU QMI8658
  float heading_deg;        // Heading attivo principale (tilt-comp se IMU online, altrimenti 2D)
  const char *cardinal_str; // Punto cardinale ("N", "NE", "E", "SE", "S", "SW", "W", "NW")
  bool calibrated;          // false = RAW (non calibrato), true = CAL (calibrato)
  bool tilt_compensated;    // true se la compensazione inclinazione QMI8658 è attiva

  // Frequenza di campionamento e metriche di stabilità su finestra mobile (32 campioni)
  float update_rate_hz;     // Frequenza effettiva di aggiornamento misurata (Hz)
  float std_x_ut;           // Deviazione standard X (uT)
  float std_y_ut;           // Deviazione standard Y (uT)
  float std_z_ut;           // Deviazione standard Z (uT)
  float std_b_ut;           // Deviazione standard |B| (uT)
  float p2p_b_ut;           // Rumore picco-picco |B| (uT)
  float std_heading_deg;    // Deviazione standard circolare heading a riposo (deg)
  float p2p_heading_deg;    // Escursione picco-picco heading a riposo (deg)
  bool field_plausible;     // true se 15 uT <= |B| <= 120 uT (assenza di saturazione/overflow)
  bool overflow_detected;   // true se uno degli assi segnala ADC flip/hall overflow

  uint32_t sample_count;    // Contatore letture valide eseguite
  uint32_t error_count;     // Contatore errori I2C o overflow
  uint32_t last_update_ms;  // Timestamp millis() dell'ultimo campione
};

// ============================================================================
// 3. TEST 9 - STATO IMU 6 ASSI INTEGRATA QMI8658C (0x6B) & ASSETTO
// ============================================================================

/**
 * @brief Mappatura assi tra QMI8658 onboard (serigrafia retro PCB U3) e BMM150 esterno.
 *
 * Come verificato su Schematico Pagina 2 (serigrafia U3):
 * - Vista dal FRONTALE del display AMOLED (con H2 in basso a ore 6 e BAT/SPK in alto a ore 12):
 *   +X_qmi punta in ALTO (ore 12, Nord del quadrante radar)
 *   +Y_qmi punta a DESTRA (ore 3, Est del quadrante radar, lato tasti PWR/BOOT)
 *   +Z_qmi punta in BASSO (entrante nello schermo / uscente dal retro del PCB)
 * - Con il modulo esterno Grove BMM150 allineato in piano (+X_bmm verso ore 12, +Y_bmm verso ore 3):
 *   sign_x = +1, sign_y = +1, sign_z = +1, swap_xy = false.
 */
struct SensorsAxisMapping {
  int8_t bmm_to_body_sign_x; // +1 o -1 (default +1)
  int8_t bmm_to_body_sign_y; // +1 o -1 (default +1)
  int8_t bmm_to_body_sign_z; // +1 o -1 (default +1)
  bool bmm_swap_xy;          // true se X e Y del BMM150 sono ruotati di 90 deg rispetto a QMI8658
};

struct Qmi8658Status {
  bool online;              // true se QMI8658 risponde a 0x6B (WHO_AM_I == 0x05)
  uint8_t i2c_addr;         // 0x6B (HW_ADDR_QMI8658 / QMI8658_L_SLAVE_ADDRESS)
  uint8_t who_am_i;         // Registro 0x00 WHO_AM_I (atteso 0x05)
  uint8_t revision_id;      // Registro 0x01 REVISION_ID (es. 0x7C)

  // Accelerazione lineare (in g) nel riferimento QMI8658
  float acc_x_g;
  float acc_y_g;
  float acc_z_g;
  float acc_norm_g;         // Modulo |a| = sqrt(ax^2 + ay^2 + az^2) (atteso ~1.00 g a riposo)

  // Velocità angolare giroscopio (in deg/s)
  float gyr_x_dps;
  float gyr_y_dps;
  float gyr_z_dps;

  float temp_c;             // Temperatura interna die QMI8658 (°C)

  // Angoli di inclinazione (Roll e Pitch in gradi, 0 deg quando la scheda è orizzontale)
  float roll_acc_deg;       // Roll istantaneo da solo accelerometro (deg)
  float pitch_acc_deg;      // Pitch istantaneo da solo accelerometro (deg)
  float roll_deg;           // Roll filtrato (filtro complementare accel + gyro, deg)
  float pitch_deg;          // Pitch filtrato (filtro complementare accel + gyro, deg)

  // Stabilità a riposo (finestra mobile)
  float std_roll_deg;
  float std_pitch_deg;
  float update_rate_hz;     // Frequenza effettiva di campionamento IMU (Hz)

  uint32_t sample_count;
  uint32_t error_count;
  uint32_t last_update_ms;
};

// ============================================================================
// 4. TEST 6 - PROCEDURA INTERFERENZE MAGNETICHE DEL MOTORINO
// ============================================================================

struct SensorsMotorInterferencePhaseStats {
  uint16_t samples;         // Numero di campioni acquisiti nella fase
  float mean_x_ut;          // Media X (uT)
  float mean_y_ut;          // Media Y (uT)
  float mean_z_ut;          // Media Z (uT)
  float mean_b_ut;          // Media modulo |B| (uT)
  float mean_heading_deg;   // Media circolare heading (0..359.9 deg)
  float std_x_ut;           // Deviazione standard X (uT)
  float std_y_ut;           // Deviazione standard Y (uT)
  float std_z_ut;           // Deviazione standard Z (uT)
  float std_b_ut;           // Deviazione standard |B| (uT)
  float std_heading_deg;    // Deviazione standard circolare heading (deg)
  float peak_x_ut;          // Valore X con massima deviazione (uT)
  float peak_y_ut;          // Valore Y con massima deviazione (uT)
  float peak_z_ut;          // Valore Z con massima deviazione (uT)
  float peak_b_ut;          // Valore |B| di picco (uT)
  float min_b_ut;           // Minimo |B| osservato nella fase (uT)
  float max_b_ut;           // Massimo |B| osservato nella fase (uT)
};

struct SensorsMotorInterferenceStep {
  bool valid;               // true se il BMM150 era online e i campioni sono stati acquisiti
  char distance_label[16];  // Etichetta distanza (es. ">=20cm", "10cm", "5cm", "2cm", "breadboard")
  float distance_cm;        // Distanza nominale in cm (es. 20.0, 10.0, 5.0, 2.0)

  // Fase A: Motor OFF baseline (30 campioni)
  SensorsMotorInterferencePhaseStats phase_a_off;
  // Fase B: Motor ON in vibrazione
  SensorsMotorInterferencePhaseStats phase_b_on;
  // Fase C: Motor OFF post-vibrazione (verifica ritorno al baseline / assenza isteresi)
  SensorsMotorInterferencePhaseStats phase_c_post;

  // Variazioni dinamiche Motor ON rispetto a Fase A (Motor OFF alla stessa distanza)
  float delta_b_mean_ut;            // |mean_B_on - mean_B_off| (uT)
  float delta_b_peak_ut;            // Massima escursione |B_on(i) - mean_B_off| durante vibrazione (uT)
  float delta_heading_mean_deg;     // Scarto angolare medio heading ON vs OFF (deg)
  float delta_heading_peak_deg;     // Scarto angolare massimo heading durante ON vs baseline OFF (deg)

  // Verifica ritorno a baseline in Fase C (Motor OFF post-vibrazione vs Fase A)
  float post_residual_b_ut;         // |mean_B_post - mean_B_off| (uT) - isteresi residua
  float post_residual_heading_deg;  // Scarto angolare residuo post-vibrazione vs baseline (deg)
  bool returned_to_baseline;        // true se post_residual_b_ut < 2.0 uT e post_residual_heading_deg < 3.0 deg

  // Interferenza magnetica STATICA a motore spento rispetto alla baseline lontana (>= 20 cm)
  bool has_far_reference;           // true se è presente uno step a >= 20 cm come riferimento
  float static_delta_b_ut;          // |mean_B_off(d) - mean_B_off(>=20cm)| (uT)
  float static_delta_heading_deg;   // Scarto heading a motore spento rispetto a >= 20 cm (deg)

  bool acceptable_for_case;         // true se delta_heading_peak_deg <= 5.0 deg e static_delta_heading_deg <= 5.0 deg
  uint32_t timestamp_ms;
};

struct SensorsMotorInterferenceReport {
  bool tested;                      // true se almeno uno step è stato eseguito
  bool bmm_was_online;              // true se il BMM150 era online durante il test
  uint8_t step_count;               // Numero di step registrati (0..SENSORS_INTERF_MAX_STEPS)
  uint8_t latest_step_index;        // Indice dell'ultimo step eseguito
  bool has_far_baseline;            // true se è stato acquisito lo step di riferimento >= 20 cm
  float far_baseline_b_ut;          // |B| medio a motore spento a >= 20 cm (uT)
  float far_baseline_heading_deg;   // Heading medio a motore spento a >= 20 cm (deg)
  float recommended_min_dist_cm;    // Distanza minima consigliata per il case finale (cm)
  SensorsMotorInterferenceStep steps[SENSORS_INTERF_MAX_STEPS];
  SensorsMotorInterferenceStep latest; // Copia dell'ultimo step eseguito (per accesso rapido UI)
  char summary[96];                 // Sintesi esito per report / UI
};

// ============================================================================
// 5. STATO AGGREGATO MODULO SENSORI
// ============================================================================

struct SensorsStatus {
  Bmm150Status bmm;
  MagCalibrationData cal;
  Qmi8658Status imu;
  SensorsAxisMapping axis_map;
  SensorsMotorInterferenceReport interference;
};

// ============================================================================
// API PUBBLICA DEL MODULO SENSORS
// ============================================================================

/**
 * @brief Inizializza entrambi i sensori (BMM150 a 0x13/0x11 e QMI8658 a 0x6B) sul bus
 *        Wire condiviso (usando i2cBusLock/i2cBusUnlock) e carica l'eventuale calibrazione
 *        magnetometrica salvata in NVS ("dr_mag_cal").
 * @return true se almeno l'IMU onboard QMI8658 (e/o BMM150) è stata inizializzata.
 */
bool sensorsInit();

/**
 * @brief Aggiornamento ciclico non bloccante da chiamare nel loop() principale.
 *        Campiona BMM150 (~25 Hz) e QMI8658 (~50 Hz), aggiorna filtro Roll/Pitch,
 *        calibrazione in corso e heading compensato per inclinazione.
 */
void sensorsUpdate();

/**
 * @brief Restituisce lo stato aggregato corrente di BMM150, Calibrazione, QMI8658 e TEST 6.
 */
const SensorsStatus &sensorsGetStatus();

/**
 * @brief Popola i campi relativi a BMM150, Calibrazione, IMU QMI8658 e TEST 6
 *        nella struttura RadarSystemSnapshot usata da display_module.
 */
void sensorsFillRadarSnapshot(RadarSystemSnapshot &snap);

// --- API TEST 5: Bussola BMM150 (0x13, fallback 0x11) ---
bool bmm150Init();
bool bmm150Update();
const Bmm150Status &bmm150GetStatus();
bool bmm150IsOnline();

// --- API TEST 5: Calibrazione Magnetometrica (Hard-Iron + Soft-Iron + NVS "dr_mag_cal") ---
bool magCalStart();
bool magCalStop(bool save_to_nvs = true);
bool magCalLoadFromNvs();
bool magCalSaveToNvs();
bool magCalClearNvs();
bool magCalIsCalibrated();
bool magCalIsInProgress();
const MagCalibrationData &magCalGetData();

// --- API TEST 6: Interferenza Magnetica del Motorino ---
/**
 * @brief Esegue la procedura in 3 fasi (Fase A: Motor OFF baseline, Fase B: Motor ON in vibrazione,
 *        Fase C: Motor OFF post-vibrazione) per la distanza specificata.
 * @param distance_label Etichetta dello step (es. ">=20cm", "10cm", "5cm", "2cm", "breadboard")
 * @param distance_cm Distanza in cm tra motorino e BMM150 (es. 20.0f, 10.0f, 5.0f, 2.0f)
 * @param baseline_samples Numero di campioni in Fase A (default 30)
 * @param motor_on_samples Numero di campioni in Fase B con motorino attivo (default 20, max < 850 ms)
 * @param post_samples Numero di campioni in Fase C post-vibrazione (default 20)
 * @return true se il test è stato completato con letture BMM150 valide.
 */
bool sensorsRunMotorInterferenceTest(const char *distance_label = "breadboard",
                                     float distance_cm = 5.0f,
                                     uint16_t baseline_samples = 30,
                                     uint16_t motor_on_samples = 20,
                                     uint16_t post_samples = 20);
const SensorsMotorInterferenceReport &sensorsGetInterferenceReport();
void sensorsClearInterferenceReport();

// --- API TEST 9: IMU QMI8658 (0x6B) & Compensazione Inclinazione ---
bool qmi8658Init();
bool qmi8658Update();
const Qmi8658Status &qmi8658GetStatus();
bool qmi8658IsOnline();
void sensorsSetAxisMapping(int8_t sign_x, int8_t sign_y, int8_t sign_z, bool swap_xy = false);

// --- Helper di stampa diagnostica su Serial per TEST 5, TEST 6, TEST 9 ---
void sensorsPrintBmm150Diagnostics(Stream &out = Serial);
void sensorsPrintCalibrationDiagnostics(Stream &out = Serial);
void sensorsPrintInterferenceReport(Stream &out = Serial);
void sensorsPrintQmi8658Diagnostics(Stream &out = Serial);
void sensorsPrintAllDiagnostics(Stream &out = Serial);
