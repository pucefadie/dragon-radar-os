#pragma once

/**
 * @file display_module.h
 * @brief Gestione Display AMOLED QSPI CO5300 (466x466), Touch Capacitivo CST9217 (0x5A)
 *        e Interfaccia Grafica Diagnostica Circolare "Dragon Radar" (Pagine 0..3 + Calibrazione).
 *
 * Riferimenti ufficiali Waveshare:
 * - examples/arduino/01_HelloWorld/01_HelloWorld.ino
 * - examples/arduino/06_LVGL_Widgets/06_LVGL_Widgets.ino
 * - examples/arduino/10_Touch_CST9217/10_Touch_CST9217.ino
 */

#include <Arduino.h>
#include <stdbool.h>
#include <stdint.h>

#include "hw_config.h"
#include "i2c_bus.h"

// Pagine dell'interfaccia Dragon Radar (466 x 466 circolare)
enum DragonRadarPage : uint8_t {
  RADAR_PAGE_MAIN_COMPASS = 0, // Pagina 0: Dragon Radar Principale + Bussola + Telemetria sintetica
  RADAR_PAGE_I2C_PMIC     = 1, // Pagina 1: Diagnostica Bus I2C, TCA9554, PMIC AXP2101 & RTC
  RADAR_PAGE_SENSORS_IMU  = 2, // Pagina 2: Bussola BMM150, IMU QMI8658 & Interferenza Motorino
  RADAR_PAGE_GNSS_LC76G   = 3, // Pagina 3: Diagnostica GNSS Quectel LC76G I2C & NMEA
  RADAR_PAGE_COUNT        = 4
};

// Codici stato GPS compatibili con gps_module.h (0=UNREACH, 1=NO DATA, 2=NO FIX, 3=VALID FIX)
enum RadarGpsStatusCode : uint8_t {
  RADAR_GPS_UNREACHABLE = 0,
  RADAR_GPS_NO_DATA     = 1,
  RADAR_GPS_NMEA_NO_FIX = 2,
  RADAR_GPS_VALID_FIX   = 3
};

// Codici stato pulsante compatibili con peripherals_module.h
enum RadarButtonUiState : uint8_t {
  RADAR_BTN_IDLE  = 0,
  RADAR_BTN_PRESS = 1,
  RADAR_BTN_SHORT = 2,
  RADAR_BTN_LONG  = 3
};

// Palette colori RGB565 a tema Dragon Radar
#define RADAR_COLOR_BG           0x0000 // Nero puro AMOLED
#define RADAR_COLOR_GRID_DARK    0x0260 // Verde radar scuro per griglia secondaria
#define RADAR_COLOR_GRID_MID     0x04A0 // Verde radar medio per cerchi concentrici
#define RADAR_COLOR_GRID_BRIGHT  0x07E0 // Verde radar brillante (RGB565_GREEN)
#define RADAR_COLOR_AMBER        0xFD20 // Giallo/Ambra Dragon Ball
#define RADAR_COLOR_YELLOW       0xFFE0 // Giallo pieno
#define RADAR_COLOR_ORANGE       0xFA60 // Arancione allerta/freccia
#define RADAR_COLOR_RED          0xF800 // Rosso errore / Nord bussola
#define RADAR_COLOR_CYAN         0x07FF // Ciano telemetria
#define RADAR_COLOR_WHITE        0xFFFF // Bianco testo primario
#define RADAR_COLOR_GRAY         0x8410 // Grigio testo secondario
#define RADAR_COLOR_DARKGRAY     0x3186 // Grigio scuro per badge/box

/**
 * @brief Snapshot indipendente dello stato di sistema per il rendering di tutte le pagine
 *        dell'interfaccia Dragon Radar senza dipendere dai dettagli interni degli altri moduli.
 */
struct RadarSystemSnapshot {
  // --- Sistema & diagnostica globale ---
  uint32_t uptime_ms = 0;
  uint8_t active_page = 0;
  uint16_t error_flags = 0;       // 0 = nessun errore hardware
  char error_summary[48] = "OK";  // "OK" oppure elenco sintetico anomalie (es. "BMM! GPS!")

  // --- Bus I2C condiviso (SDA=15, SCL=14) & TCA9554 (0x20) ---
  bool i2c_present[128] = {false};
  uint8_t i2c_found_count = 0;
  uint32_t i2c_tx_count = 0;
  uint32_t i2c_err_count = 0;
  bool tca_online = false;
  uint8_t tca_inputs = 0xFF;
  uint8_t tca_outputs = 0xFF;
  uint8_t tca_config = 0xFF;

  // --- Touch CST9217 (0x5A, TP_RST=40, TP_INT=11) ---
  bool touch_online = false;
  bool touch_pressed = false;
  int16_t touch_x = 0;
  int16_t touch_y = 0;
  uint32_t touch_tap_count = 0;

  // --- Pulsante esterno (GPIO44) + Pulsanti onboard (BOOT GPIO0, PWR EXIO4) ---
  bool btn_pressed = false;
  uint8_t btn_state = RADAR_BTN_IDLE; // 0=IDLE, 1=PRESS, 2=SHORT, 3=LONG
  char btn_state_str[12] = "IDLE";    // Se valorizzato ("IDLE"/"PRESS"/"SHORT"/"LONG"), usato direttamente
  uint32_t btn_press_count = 0;
  uint32_t btn_short_count = 0;
  uint32_t btn_long_count = 0;
  uint32_t btn_last_hold_ms = 0;
  bool boot_btn_pressed = false;
  bool pwr_btn_pressed = false;

  // --- Motorino di vibrazione Grove V1.3 (GPIO16) ---
  bool motor_active = false;
  bool motor_pattern_running = false;
  char motor_pattern_str[16] = "IDLE";
  uint32_t motor_activations = 0;
  uint16_t motor_min_vsys_mv = 0;
  int16_t motor_max_drop_mv = 0;
  uint32_t motor_i2c_err_delta = 0;
  bool motor_power_anomaly = false;

  // --- PMIC AXP2101 (0x34) & RTC PCF85063 (0x51) ---
  bool pmic_online = false;
  uint8_t pmic_chip_id = 0;
  bool vbus_present = false;
  bool vbus_good = false;
  uint16_t vbus_mv = 0;
  uint16_t vsys_mv = 0;
  bool battery_present = false;
  uint16_t batt_mv = 0;
  int16_t batt_percent = -1; // -1 se batteria scollegata
  bool is_charging = false;
  bool is_discharging = false;
  char charger_status_str[20] = "N/A";
  float pmic_temp_c = 0.0f;
  uint16_t dc1_mv = 3300;
  uint16_t dc3_mv = 0;
  uint16_t aldo1_mv = 0;
  uint16_t aldo2_mv = 0;
  uint16_t aldo3_mv = 0;
  uint16_t aldo4_mv = 0;
  uint16_t bldo1_mv = 0;
  uint16_t bldo2_mv = 0;
  bool rtc_online = false;
  char rtc_datetime[24] = "";

  // --- Bussola BMM150 (0x13/0x11) ---
  bool bmm_online = false;
  uint8_t bmm_addr = HW_ADDR_BMM150;
  uint8_t bmm_chip_id = 0;
  float mag_x_ut = 0.0f;
  float mag_y_ut = 0.0f;
  float mag_z_ut = 0.0f;
  float mag_b_ut = 0.0f;           // Modulo |B| = sqrt(X^2 + Y^2 + Z^2) (uT)
  float mag_noise_ut = 0.0f;       // Rumore / deviazione standard su finestra mobile (uT)
  float mag_rate_hz = 0.0f;        // Frequenza di aggiornamento effettiva (Hz)
  float heading_raw_deg = 0.0f;    // Heading raw non calibrato (0..359 deg)
  float heading_cal_deg = 0.0f;    // Heading calibrato 2D (0..359 deg)
  float heading_tilt_deg = 0.0f;   // Heading compensato per inclinazione (0..359 deg)
  float heading_deg = 0.0f;        // Heading attivo mostrato sulla bussola (0..359 deg)
  bool mag_calibrated = false;     // true = [CAL], false = [RAW]
  bool tilt_compensated = false;   // true = [TILT] attivo
  float cal_offset_x = 0.0f;
  float cal_offset_y = 0.0f;
  float cal_offset_z = 0.0f;
  float cal_scale_x = 1.0f;
  float cal_scale_y = 1.0f;
  float cal_scale_z = 1.0f;

  // --- Modalità Calibrazione Bussola Overlay ---
  bool cal_in_progress = false;
  float cal_min_x = 0.0f;
  float cal_min_y = 0.0f;
  float cal_min_z = 0.0f;
  float cal_max_x = 0.0f;
  float cal_max_y = 0.0f;
  float cal_max_z = 0.0f;
  uint32_t cal_samples = 0;
  uint32_t cal_elapsed_ms = 0;

  // --- IMU 6 assi QMI8658 (0x6B) ---
  bool imu_online = false;
  float acc_x_g = 0.0f;
  float acc_y_g = 0.0f;
  float acc_z_g = 0.0f;
  float gyr_x_dps = 0.0f;
  float gyr_y_dps = 0.0f;
  float gyr_z_dps = 0.0f;
  float roll_deg = 0.0f;
  float pitch_deg = 0.0f;

  // --- Report Interferenza Magnetica Motorino (TEST 6) ---
  bool interference_tested = false;
  float interf_base_b_ut = 0.0f;
  float interf_on_b_ut = 0.0f;
  float interf_delta_b_ut = 0.0f;        // Delta |B| max durante vibrazione (uT)
  float interf_delta_heading_deg = 0.0f; // Delta heading max durante vibrazione (deg)
  float interf_post_delta_b_ut = 0.0f;   // Residuo/isteresi post-vibrazione (uT)
  char interf_distance_label[16] = "N/A";

  // --- GNSS Quectel LC76G (0x50 comando / 0x54 lettura) ---
  uint8_t gps_status = RADAR_GPS_UNREACHABLE; // 0=UNREACH, 1=NO DATA, 2=NO FIX, 3=3D FIX
  bool gps_i2c_online = false;
  bool gps_fix_valid = false;
  bool gps_uart_active = false, gps_ever_had_fix = false;
  uint8_t gps_fix_quality = 0;
  uint8_t gps_sats_in_use = 0;
  uint8_t gps_sats_in_view = 0;
  double gps_lat = 0.0;
  double gps_lon = 0.0;
  float gps_alt_m = 0.0f;
  float gps_hdop = 99.9f;
  char gps_utc_time[16] = "--:--:--";
  char gps_utc_date[16] = "----/--/--";
  uint32_t gps_fix_age_ms = 0;
  uint32_t gps_bytes_rx = 0;
  uint32_t gps_i2c_ok = 0;
  uint32_t gps_i2c_fail = 0;
  uint32_t gps_nmea_valid = 0;
  uint32_t gps_nmea_cs_err = 0;
  char gps_last_nmea[2][48] = {{0}, {0}};
};

// ============================================================================
// API PUBBLICA DEL MODULO DISPLAY & TOUCH
// ============================================================================

/**
 * @brief Inizializza il display QSPI CO5300 (466x466, col_offset1=6), il framebuffer
 *        Arduino_Canvas in PSRAM (466x466 RGB565) e opzionalmente il touch CST9217 (0x5A).
 * @param init_touch Se true, inizializza anche il controller touch CST9217 su Wire condiviso.
 * @return true se il display (e il touch, se richiesto) sono stati inizializzati con successo.
 */
bool displayInit(bool init_touch = true);

/**
 * @brief Imposta la luminosità del pannello AMOLED tramite comando DCS CO5300 (0x51).
 * @param level Livello di luminosità (0 = spento .. 255 = massima).
 */
void displaySetBrightness(uint8_t level);

/**
 * @brief Esegue il TEST 2 (Display Color & Geometry Pattern Test):
 *        verifica 466x466, schermate ROSSO, VERDE, BLU, BIANCO, NERO, rampa di luminosità,
 *        cerchi concentrici di allineamento bordo circolare e testo leggibile.
 * @param step_ms Durata in millisecondi di ciascuno step colore (default 600 ms).
 * @return true se il test viene completato senza errori sul bus QSPI.
 */
bool displayRunColorPatternTest(uint32_t step_ms = 600);

/**
 * @brief Mostra un banner diagnostico centrato nella safe-area circolare 466x466,
 *        utile durante l'esecuzione sequenziale dei test (TEST 2 .. TEST 9).
 */
void displayShowTestBanner(const char *title,
                           const char *subtitle,
                           const char *status_line,
                           uint16_t accent_color = 0x07E0);

/**
 * @brief Interroga in modo non bloccante il controller touch CST9217 (0x5A)
 *        proteggendo l'accesso al bus I2C condiviso con i2cBusLock()/i2cBusUnlock().
 * @param x Coordinate X ultimo punto toccato (0..465).
 * @param y Coordinate Y ultimo punto toccato (0..465).
 * @param tap_detected Impostato a true sul fronte di nuovo tap valido (con debounce).
 * @return true se il dito è attualmente premuto sullo schermo, false altrimenti.
 */
bool displayPollTouch(int16_t &x, int16_t &y, bool &tap_detected);

/**
 * @brief Esegue il rendering completo e senza sfarfallio (tramite framebuffer PSRAM)
 *        dell'interfaccia circolare Dragon Radar 466x466 per la pagina indicata
 *        (oppure dell'overlay di calibrazione bussola se snap.cal_in_progress == true).
 * @param snap Snapshot aggregato dello stato corrente di tutti i moduli.
 * @param active_page Indice pagina (0..3).
 */
void displayRenderDragonRadar(const RadarSystemSnapshot &snap, uint8_t active_page);

// --- Helper diagnostici di stato ---
bool displayIsInitialized();
bool displayIsTouchOnline();
bool displayHasPsramCanvas();
uint8_t displayGetBrightness();
const char *displayGetTouchModel();
uint32_t displayGetTouchTapCount();
void displayPrintDiagnostics(Stream &out = Serial);

// Single shared framebuffer; operating-system screens.
void displayRenderOsScreen(uint8_t app, bool launcher, uint8_t selected = 0);

struct RadarView;
void displayRenderRadarApp(const RadarView &view);

void displayRenderSettings(uint8_t selection, const RadarSystemSnapshot &snapshot);

// Larger, paginated debug UI; original displayRenderDragonRadar remains available.
void displayRenderDebugLarge(const RadarSystemSnapshot &snapshot, uint8_t page);
