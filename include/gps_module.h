#pragma once

/**
 * @file gps_module.h
 * @brief Driver I2C non bloccante e parser NMEA-0183 completo per il modulo GNSS
 *        Quectel LC76G onboard (Waveshare ESP32-S3-Touch-AMOLED-1.75-G, TEST 7).
 *
 * Riferimenti ufficiali verificati:
 * - examples/arduino/09_LC76G_I2C/09_LC76G_I2C.ino (Protocollo I2C 2 fasi 0x50 / 0x54)
 * - HARDWARE_REFERENCE.md (righe 13, 34, 59-60, 87, 93, 106, 166)
 * - Schematic/ESP32-S3-Touch-AMOLED-1.75-schematic.pdf (Blocco Q1 LC76GABMD, U5 TCA9554 EXIO7 = GPS_RST)
 *
 * Vincoli architetturali rispettati:
 * - Nessuna installazione di secondo driver I2C (non usa i2c_driver_install).
 * - Tutte le transazioni I2C passano attraverso i2cRawWrite(0x50, ...) / i2cRawRead(0x54, ...)
 *   e gli helper TCA9554 (tca9554SetPinMode / tca9554WritePin per HW_EXIO_GPS_RST = 7)
 *   definiti in include/i2c_bus.h sul bus Wire condiviso protetto da mutex.
 */

#include <Arduino.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hw_config.h"
#include "i2c_bus.h"

#ifndef HW_ADDR_LC76G_W
#define HW_ADDR_LC76G_W 0x50
#endif
#ifndef HW_ADDR_LC76G_R
#define HW_ADDR_LC76G_R 0x54
#endif
#ifndef HW_EXIO_GPS_RST
#define HW_EXIO_GPS_RST 7
#endif
#ifndef HW_GPS_UART_TX_MCU
#define HW_GPS_UART_TX_MCU 17
#endif
#ifndef HW_GPS_UART_RX_MCU
#define HW_GPS_UART_RX_MCU 18
#endif

// ============================================================================
// 1. I 4 STATI RIGOROSI RICHIESTI DAL TEST 7
// ============================================================================
enum GpsStatus : uint8_t {
  GPS_STATUS_UNREACHABLE = 0, // Il modulo non risponde agli indirizzi I2C 0x50/0x54 (né fallback UART)
  GPS_STATUS_NO_DATA     = 1, // Il modulo risponde via I2C ma dataLength == 0 / nessuna frase NMEA ricevuta
  GPS_STATUS_NMEA_NO_FIX = 2, // Flusso NMEA valido ricevuto, ma nessun fix satellitare valido (es. indoor / cold start)
  GPS_STATUS_VALID_FIX   = 3  // Fix satellitare valido con coordinate reali non-(0.0, 0.0)
};

// ============================================================================
// 2. FASI DELLA MACCHINA A STATI I2C NON BLOCCANTE (09_LC76G_I2C.ino)
// ============================================================================
enum GpsProtocolPhase : uint8_t {
  GPS_PHASE_IDLE              = 0, // Attesa intervallo tra cicli di polling
  GPS_PHASE_1_QUERY_LEN       = 1, // Invio a 0x50 di {0x08, 0x00, 0x51, 0xAA, 0x04, 0x00, 0x00, 0x00}
  GPS_PHASE_2_READ_LEN        = 2, // Dopo >= 80 ms: lettura 4 byte readData[0..3] da 0x54 -> dataLength
  GPS_PHASE_3_REQUEST_PAYLOAD = 3, // Dopo >= 80 ms: se 0 < dataLength <= 2048, invio {0x00, 0x20, 0x51, 0xAA, len[0..3]} a 0x50
  GPS_PHASE_4_READ_PAYLOAD    = 4  // Dopo >= 80 ms: lettura payload NMEA da 0x54 e parsing carattere per carattere
};

// Costanti buffer diagnostico NMEA
#define GPS_LAST_LINES_COUNT 4
#define GPS_NMEA_LINE_MAX    96
#define GPS_RAW_PREVIEW_MAX  128

// ============================================================================
// 3. STRUTTURA DI STATO E TELEMETRIA GNSS COMPLETA
// ============================================================================
struct GpsStatusSnapshot {
  // --- Stato principale (distinzione rigorosa dei 4 stati) ---
  GpsStatus status;               // GPS_STATUS_UNREACHABLE .. GPS_STATUS_VALID_FIX
  const char *status_str;         // Nome esteso ("GPS_STATUS_UNREACHABLE", ...)
  const char *status_short_str;   // Etichetta UI ("UNREACH", "NO DATA", "NO FIX", "3D FIX")

  // --- Stato hardware I2C e Reset TCA9554 EXIO7 ---
  bool initialized;               // True dopo gpsInit()
  bool i2c_reachable;             // True se LC76G risponde al protocollo I2C su 0x50/0x54
  bool ack_0x50;                  // Esito ultima scrittura comando su 0x50 (HW_ADDR_LC76G_W)
  bool ack_0x54;                  // Esito ultima lettura dati su 0x54 (HW_ADDR_LC76G_R)
  bool probe_0x50_before_rst;     // Risposta 0x50 prima della sequenza EXIO7
  bool probe_0x54_before_rst;     // Risposta 0x54 prima della sequenza EXIO7
  bool probe_0x50_after_rst;      // Risposta 0x50 dopo configurazione/impulso EXIO7
  bool probe_0x54_after_rst;      // Risposta 0x54 dopo configurazione/impulso EXIO7
  int exio7_initial_level;        // Livello letto su TCA9554 EXIO7 prima del reset (-1 se non letto)
  uint8_t exio7_active_level;     // Livello operativo attivo di EXIO7 (1 = HIGH rilascio reset)
  uint32_t reset_count;           // Numero di sequenze reset eseguite su EXIO7

  // --- Stato macchina a stati protocollo I2C ---
  GpsProtocolPhase protocol_phase;
  uint32_t last_data_length;      // Ultimo valore dataLength (32-bit LE) letto in Fase 2 da 0x54
  uint32_t last_chunk_requested;  // Ultimo blocco richiesto in Fase 3 / letto in Fase 4
  uint32_t max_data_length_seen;  // Massimo valore dataLength valido (<= 2048) osservato

  // --- Stato Fix e Posizione ---
  bool fix_valid;                 // True SOLO se (fix_quality > 0 || rmc_status == 'A') E coord != (0,0) E non scaduto
  bool ever_had_valid_fix;        // True se almeno un fix valido non-(0,0) è stato acquisito dall'avvio
  uint8_t fix_quality;            // Da GGA: 0=Invalid, 1=GPS SPS, 2=DGPS, 4=RTK Fix, 5=RTK Float
  uint8_t fix_dimension;          // Da GSA: 1=No Fix, 2=2D Fix, 3=3D Fix
  char rmc_status;                // Da RMC: 'A'=Active (valido), 'V'=Void (non valido)

  uint8_t satellites_in_use;      // Numero satelliti usati nella soluzione (da GGA campo 7)
  uint8_t satellites_in_view;     // Numero totale satelliti in vista (somma costellazioni da GSV)
  uint8_t sats_in_use;            // Alias di satellites_in_use per comodità di integrazione
  uint8_t sats_in_view;           // Alias di satellites_in_view per comodità di integrazione
  uint8_t sats_view_gps;          // Satelliti in vista da $GPGSV
  uint8_t sats_view_glonass;      // Satelliti in vista da $GLGSV
  uint8_t sats_view_galileo;      // Satelliti in vista da $GAGSV
  uint8_t sats_view_beidou;       // Satelliti in vista da $GBGSV / $BDGSV

  double latitude_deg;            // Latitudine in gradi decimali (+N / -S)
  double longitude_deg;           // Longitudine in gradi decimali (+E / -W)
  char lat_hemisphere;            // 'N', 'S' oppure '-'
  char lon_hemisphere;            // 'E', 'W' oppure '-'
  float altitude_m;               // Altitudine sul livello del mare (m, da GGA)
  float geoid_sep_m;              // Separazione geoidica (m, da GGA)
  float hdop;                     // Horizontal Dilution of Precision (da GGA/GSA, 99.9 se N/D)
  float pdop;                     // Position Dilution of Precision (da GSA, 99.9 se N/D)
  float vdop;                     // Vertical Dilution of Precision (da GSA, 99.9 se N/D)
  float speed_knots;              // Velocità al suolo in nodi (da RMC)
  float speed_kmh;                // Velocità al suolo in km/h (da RMC/VTG)
  float course_deg;               // Rotta / Course Over Ground in gradi (da RMC/VTG)

  // --- Data e Ora UTC + Età del Fix ---
  bool time_valid;                // True se l'ora UTC è stata ricevuta da GGA/RMC
  bool date_valid;                // True se la data UTC è stata ricevuta da RMC
  uint8_t utc_hour;               // 0..23
  uint8_t utc_minute;             // 0..59
  uint8_t utc_second;             // 0..59
  uint16_t utc_year;              // es. 2026
  uint8_t utc_month;              // 1..12
  uint8_t utc_day;                // 1..31
  char utc_time_str[16];          // "HH:MM:SS" (o "--:--:--" se non ancora ricevuto)
  char utc_date_str[16];          // "YYYY-MM-DD" (o "----/--/--" se non ancora ricevuto)
  uint32_t last_valid_fix_ms;     // Timestamp millis() dell'ultimo fix valido non-(0,0)
  uint32_t fix_age_ms;            // Età dell'ultimo fix valido in ms (0 se nessun fix ancora ottenuto)
  uint32_t last_nmea_rx_ms;       // Timestamp millis() dell'ultima frase NMEA valida

  // --- Contatori Diagnostici I2C e NMEA ---
  uint32_t i2c_tx_ok;             // Transazioni I2C riuscite verso 0x50 / 0x54
  uint32_t i2c_tx_fail;           // Transazioni I2C fallite verso 0x50 / 0x54
  uint32_t i2c_consecutive_fails; // Fallimenti I2C consecutivi correnti
  uint32_t poll_cycles;           // Cicli completi di polling I2C eseguiti
  uint32_t non_empty_polls;       // Cicli in cui dataLength > 0
  uint32_t total_bytes_read;      // Byte totali letti dal modulo GNSS
  uint32_t nmea_sentences_valid;  // Frasi NMEA complete con checksum *XX verificato
  uint32_t nmea_sentences_crc_err;// Frasi NMEA scartate per errore checksum *XX
  uint32_t gga_count;             // Contatore frasi $xxGGA valide
  uint32_t rmc_count;             // Contatore frasi $xxRMC valide
  uint32_t gsa_count;             // Contatore frasi $xxGSA valide
  uint32_t gsv_count;             // Contatore frasi $xxGSV valide
  uint32_t txt_count;             // Contatore frasi $xxTXT valide
  uint32_t zero_coord_rejected_count; // Coordinate (0.0, 0.0) esplicitamente rifiutate come fix

  // --- Ultime 4 righe NMEA ricevute & Stream Grezzo ---
  char last_nmea_lines[GPS_LAST_LINES_COUNT][GPS_NMEA_LINE_MAX]; // Ultime 4 righe NMEA (ordine cronologico)
  uint8_t last_nmea_lines_count;  // Numero di righe popolate (0..4)
  char last_raw_preview[GPS_RAW_PREVIEW_MAX]; // Anteprima ASCII degli ultimi byte grezzi letti
  bool raw_stream_enabled;        // Se true, stampa in tempo reale su Serial il flusso NMEA grezzo

  // --- Diagnostica Fallback UART Opzionale (GPIO17/GPIO18) ---
  bool uart_fallback_tested;      // True se gpsTryUartFallback() è stato invocato
  bool uart_fallback_active;      // True se il flusso NMEA è stato rilevato via UART
  uint32_t uart_fallback_baud;    // Baudrate attivo in fallback UART (9600 o 115200)
  uint32_t uart_bytes_read;       // Byte ricevuti tramite fallback UART
};

typedef GpsStatusSnapshot GpsData;

// ============================================================================
// 4. API PUBBLICA DEL MODULO GPS (TEST 7)
// ============================================================================

/**
 * @brief Inizializza il modulo GNSS Quectel LC76G:
 *        1. Verifica la risposta preliminare su 0x50/0x54.
 *        2. Configura EXIO7 (GPS_RST su TCA9554 0x20) come OUTPUT ed esegue la
 *           sequenza di rilascio/impulso reset verificando l'ACK su 0x50/0x54.
 *        3. Avvia la macchina a stati non bloccante per il protocollo I2C Waveshare.
 * @return true se il modulo LC76G risponde sul bus I2C condiviso (0x50/0x54).
 */
bool gpsInit();

/**
 * @brief Esegue la sequenza di reset hardware del modulo LC76G tramite il pin
 *        EXIO7 (P7) del TCA9554 (0x20) e verifica la risposta I2C su 0x50/0x54.
 * @return true se il modulo risponde correttamente dopo il reset.
 */
bool gpsHardwareReset();

/**
 * @brief Aggiornamento ciclico NON bloccante da chiamare nel loop() principale.
 *        Avanza la macchina a stati a 4 fasi del protocollo I2C Waveshare LC76G
 *        usando millis() (zero delay bloccanti) e alimenta il parser NMEA.
 */
void gpsUpdate();

/**
 * @brief Restituisce lo snapshot costante e aggiornato dello stato GNSS.
 */
const GpsStatusSnapshot &gpsGetStatus();

/**
 * @brief Abilita o disabilita lo stream diagnostico del flusso NMEA grezzo su Serial.
 */
void gpsSetRawStreamEnabled(bool enabled);

/**
 * @brief Restituisce true se lo stream NMEA grezzo su Serial è attivo.
 */
bool gpsIsRawStreamEnabled();

/**
 * @brief Inverte lo stato dello stream NMEA grezzo su Serial.
 * @return Nuovo stato (true = abilitato, false = disabilitato).
 */
bool gpsToggleRawStream();

/**
 * @brief Copia le ultime fino a 4 righe NMEA ricevute nel buffer fornito.
 * @param out_lines Array [4][96] di destinazione.
 * @return Numero di righe effettivamente disponibili (0..4).
 */
uint8_t gpsGetLastNmeaLines(char out_lines[GPS_LAST_LINES_COUNT][GPS_NMEA_LINE_MAX]);

/**
 * @brief Restituisce il puntatore a una delle ultime 4 righe NMEA ricevute (0..3).
 */
const char *gpsGetLastNmeaLine(uint8_t index);

/**
 * @brief Passa manualmente un singolo carattere al parser NMEA-0183 con verifica checksum.
 * @return true se il carattere ha completato una frase NMEA con checksum *XX valido.
 */
bool gpsFeedNmeaChar(char c);

/**
 * @brief Funzione diagnostica opzionale di fallback UART su GPIO17 (TX) / GPIO18 (RX),
 *        invocabile su richiesta se il modulo non risponde via I2C (verifica R15/R16).
 * @param tx_pin Pin MCU TX verso LC76G RXD1 (default HW_GPS_UART_TX_MCU = 17).
 * @param rx_pin Pin MCU RX da LC76G TXD1 (default HW_GPS_UART_RX_MCU = 18).
 * @param baud Baudrate da testare (9600 o 115200; se 0 testa automaticamente 115200 e 9600).
 * @param listen_ms Durata massima di ascolto per ciascun baudrate (default 1200 ms).
 * @return true se vengono ricevuti dati NMEA validi su UART.
 */
bool gpsTryUartFallback(int tx_pin = HW_GPS_UART_TX_MCU,
                        int rx_pin = HW_GPS_UART_RX_MCU,
                        uint32_t baud = 115200,
                        uint32_t listen_ms = 1200);

/**
 * @brief Esegue una sessione di acquisizione e verifica diagnostica deterministica per il TEST 7,
 *        pompando la macchina a stati per `duration_ms` e stampando il report completo su `out`.
 * @return true se il modulo LC76G è almeno raggiungibile via I2C (o UART) senza guasti bus.
 */
bool gpsRunDiagnosticTest(uint32_t duration_ms = 3500, Stream &out = Serial);

/**
 * @brief Stampa il report diagnostico dettagliato del modulo GPS (TEST 7) sullo stream indicato.
 */
void gpsPrintDiagnostics(Stream &out = Serial);

/**
 * @brief Converte GpsStatus nella stringa simbolica ufficiale ("GPS_STATUS_...").
 */
const char *gpsStatusToString(GpsStatus status);

/**
 * @brief Converte GpsStatus nella stringa sintetica per la UI Dragon Radar ("UNREACH", "NO DATA", "NO FIX", "3D FIX").
 */
const char *gpsStatusToShortString(GpsStatus status);
