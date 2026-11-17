#include "gps_module.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

// Comandi ufficiali protocollo I2C Waveshare LC76G (da 09_LC76G_I2C.ino)
// Fase 1: Query numero di byte NMEA disponibili nel FIFO del modulo (registro 0xAA510008, 4 byte)
static const uint8_t kCmdQueryLength[8] = {
    0x08, 0x00, 0x51, 0xAA, 0x04, 0x00, 0x00, 0x00};

// Fase 3: Prefisso comando di richiesta lettura blocco NMEA (registro 0xAA512000 + 4 byte lunghezza)
static const uint8_t kCmdReadPayloadPrefix[4] = {
    0x00, 0x20, 0x51, 0xAA};

// Tempistiche non bloccanti della macchina a stati I2C (in ms)
static constexpr uint32_t kPhaseWaitMs          = 65;    // >= 60..100 ms tra scrittura 0x50 e lettura 0x54
static constexpr uint32_t kIdleAfterDataMs      = 150;   // Intervallo dopo un ciclo con dati NMEA
static constexpr uint32_t kIdleMorePendingMs    = 15;    // Intervallo rapido se nel FIFO restano altri byte da scaricare
static constexpr uint32_t kIdleNoDataMs         = 250;   // Intervallo quando dataLength == 0
static constexpr uint32_t kIdleAfterErrorMs     = 400;   // Backoff dopo un errore I2C
static constexpr uint32_t kMaxValidDataLength   = 65536; // Limite 64 KB per gestire accumulo FIFO durante i test lunghi
static constexpr uint32_t kFixStaleTimeoutMs    = 10000; // Timeout validità fix senza nuovi aggiornamenti

// Stato interno del modulo GPS
static GpsStatusSnapshot s_status;
static uint32_t s_phase_ts_ms = 0;
static uint32_t s_idle_wait_ms = kIdleNoDataMs;
static uint8_t s_len_raw_bytes[4] = {0, 0, 0, 0};
static bool s_more_fifo_pending = false;

// Buffer statico per lettura blocchi NMEA da 0x54 (dimensionato su HW_I2C_BUF_SIZE = 512)
static constexpr size_t kPayloadBufCap = (HW_I2C_BUF_SIZE >= 64) ? (size_t)HW_I2C_BUF_SIZE : 512;
static uint8_t s_payload_buf[kPayloadBufCap];

// Buffer di accumulo per la singola riga NMEA in corso di ricezione
static char s_line_buf[GPS_NMEA_LINE_MAX];
static size_t s_line_len = 0;
static bool s_in_sentence = false;

// Contatori satelliti in vista per costellazione (per aggregare $GPGSV, $GLGSV, $GAGSV, $GBGSV)
static uint8_t s_gsv_gn_total = 0;
static uint32_t s_last_gsv_ms = 0;

//Verifica se una coppia (lat, lon) è troppo vicina a (0.0, 0.0) e va rifiutata
static bool isZeroCoordinate(double lat, double lon) {
  return (fabs(lat) < 1e-4 && fabs(lon) < 1e-4);
}

static int hexCharToInt(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static void pushLastNmeaLine(const char *line) {
  if (line == nullptr || line[0] == '\0') {
    return;
  }
  if (s_status.last_nmea_lines_count < GPS_LAST_LINES_COUNT) {
    uint8_t idx = s_status.last_nmea_lines_count++;
    strncpy(s_status.last_nmea_lines[idx], line, GPS_NMEA_LINE_MAX - 1);
    s_status.last_nmea_lines[idx][GPS_NMEA_LINE_MAX - 1] = '\0';
  } else {
    for (uint8_t i = 0; i < GPS_LAST_LINES_COUNT - 1; ++i) {
      memcpy(s_status.last_nmea_lines[i], s_status.last_nmea_lines[i + 1], GPS_NMEA_LINE_MAX);
    }
    strncpy(s_status.last_nmea_lines[GPS_LAST_LINES_COUNT - 1], line, GPS_NMEA_LINE_MAX - 1);
    s_status.last_nmea_lines[GPS_LAST_LINES_COUNT - 1][GPS_NMEA_LINE_MAX - 1] = '\0';
  }
}

static void appendRawPreviewByte(char c) {
  // Mantiene una finestra scorrevole leggibile degli ultimi caratteri NMEA grezzi
  char printable = c;
  if (c == '\r') {
    return;
  }
  if (c == '\n') {
    printable = ' ';
  } else if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) {
    return;
  }

  size_t cur_len = strlen(s_status.last_raw_preview);
  if (cur_len + 1 < GPS_RAW_PREVIEW_MAX) {
    s_status.last_raw_preview[cur_len] = printable;
    s_status.last_raw_preview[cur_len + 1] = '\0';
  } else {
    // Scorri a sinistra di 32 byte per fare spazio
    const size_t shift = 32;
    memmove(s_status.last_raw_preview,
            s_status.last_raw_preview + shift,
            GPS_RAW_PREVIEW_MAX - shift);
    cur_len = strlen(s_status.last_raw_preview);
    if (cur_len + 1 < GPS_RAW_PREVIEW_MAX) {
      s_status.last_raw_preview[cur_len] = printable;
      s_status.last_raw_preview[cur_len + 1] = '\0';
    }
  }
}

static void updateGpsStatusState() {
  uint32_t now = millis();

  if (s_status.ever_had_valid_fix) {
    s_status.fix_age_ms = now - s_status.last_valid_fix_ms;
    if (s_status.fix_valid && s_status.fix_age_ms > kFixStaleTimeoutMs) {
      s_status.fix_valid = false;
    }
  } else {
    s_status.fix_age_ms = 0;
  }

  // Aggiorna alias satelliti
  s_status.sats_in_use = s_status.satellites_in_use;
  s_status.sats_in_view = s_status.satellites_in_view;

  // Distinzione rigorosa dei 4 stati richiesti dal TEST 7:
  // 1. GPS_STATUS_UNREACHABLE: il modulo non risponde agli indirizzi I2C 0x50/0x54 (né fallback UART)
  // 2. GPS_STATUS_NO_DATA: il modulo risponde via I2C ma dataLength == 0 / nessuna frase NMEA ricevuta
  // 3. GPS_STATUS_NMEA_NO_FIX: flusso NMEA valido ricevuto, ma nessun fix satellitare valido
  // 4. GPS_STATUS_VALID_FIX: fix satellitare valido con coordinate reali non-(0,0)
  bool comm_ok = (s_status.i2c_reachable && s_status.i2c_consecutive_fails < 6) ||
                 s_status.uart_fallback_active;

  if (!comm_ok) {
    s_status.status = GPS_STATUS_UNREACHABLE;
  } else if (s_status.nmea_sentences_valid == 0) {
    s_status.status = GPS_STATUS_NO_DATA;
  } else if (s_status.fix_valid &&
             (s_status.fix_quality > 0 || s_status.rmc_status == 'A') &&
             !isZeroCoordinate(s_status.latitude_deg, s_status.longitude_deg)) {
    s_status.status = GPS_STATUS_VALID_FIX;
  } else {
    s_status.status = GPS_STATUS_NMEA_NO_FIX;
  }

  s_status.status_str = gpsStatusToString(s_status.status);
  s_status.status_short_str = gpsStatusToShortString(s_status.status);
}

// Converte coordinata NMEA (ddmm.mmmm o dddmm.mmmm) + emisfero (N/S/E/W) in gradi decimali double
static bool parseNmeaCoordinate(const char *coord_field,
                                const char *hemi_field,
                                bool is_longitude,
                                double &out_deg,
                                char &out_hemi) {
  if (coord_field == nullptr || hemi_field == nullptr) {
    return false;
  }
  if (coord_field[0] == '\0' || hemi_field[0] == '\0') {
    return false;
  }

  char hemi = (char)toupper((unsigned char)hemi_field[0]);
  if (is_longitude) {
    if (hemi != 'E' && hemi != 'W') return false;
  } else {
    if (hemi != 'N' && hemi != 'S') return false;
  }

  const char *dot = strchr(coord_field, '.');
  if (dot == nullptr || (dot - coord_field) < 3) {
    return false;
  }

  char *endptr = nullptr;
  double raw = strtod(coord_field, &endptr);
  if (endptr == coord_field || !isfinite(raw) || raw < 0.0) {
    return false;
  }

  int degrees = (int)(raw / 100.0);
  double minutes = raw - ((double)degrees * 100.0);
  if (minutes < 0.0 || minutes >= 60.0) {
    return false;
  }

  double dec = (double)degrees + (minutes / 60.0);
  if (hemi == 'S' || hemi == 'W') {
    dec = -dec;
  }

  double max_abs = is_longitude ? 180.0 : 90.0;
  if (fabs(dec) > max_abs) {
    return false;
  }

  out_deg = dec;
  out_hemi = hemi;
  return true;
}

static void parseUtcTimeField(const char *time_field) {
  if (time_field == nullptr || strlen(time_field) < 6) {
    return;
  }
  if (!isdigit((unsigned char)time_field[0]) ||
      !isdigit((unsigned char)time_field[1]) ||
      !isdigit((unsigned char)time_field[2]) ||
      !isdigit((unsigned char)time_field[3]) ||
      !isdigit((unsigned char)time_field[4]) ||
      !isdigit((unsigned char)time_field[5])) {
    return;
  }

  uint8_t hh = (uint8_t)((time_field[0] - '0') * 10 + (time_field[1] - '0'));
  uint8_t mm = (uint8_t)((time_field[2] - '0') * 10 + (time_field[3] - '0'));
  uint8_t ss = (uint8_t)((time_field[4] - '0') * 10 + (time_field[5] - '0'));
  if (hh > 23 || mm > 59 || ss > 60) {
    return;
  }

  s_status.utc_hour = hh;
  s_status.utc_minute = mm;
  s_status.utc_second = ss;
  s_status.time_valid = true;
  snprintf(s_status.utc_time_str, sizeof(s_status.utc_time_str), "%02u:%02u:%02u", hh, mm, ss);
}

static void parseUtcDateField(const char *date_field) {
  if (date_field == nullptr || strlen(date_field) < 6) {
    return;
  }
  if (!isdigit((unsigned char)date_field[0]) ||
      !isdigit((unsigned char)date_field[1]) ||
      !isdigit((unsigned char)date_field[2]) ||
      !isdigit((unsigned char)date_field[3]) ||
      !isdigit((unsigned char)date_field[4]) ||
      !isdigit((unsigned char)date_field[5])) {
    return;
  }

  uint8_t dd = (uint8_t)((date_field[0] - '0') * 10 + (date_field[1] - '0'));
  uint8_t mo = (uint8_t)((date_field[2] - '0') * 10 + (date_field[3] - '0'));
  uint16_t yy = (uint16_t)((date_field[4] - '0') * 10 + (date_field[5] - '0'));
  if (dd < 1 || dd > 31 || mo < 1 || mo > 12) {
    return;
  }

  uint16_t full_year = (uint16_t)(2000U + yy);
  s_status.utc_day = dd;
  s_status.utc_month = mo;
  s_status.utc_year = full_year;
  s_status.date_valid = true;
  snprintf(s_status.utc_date_str, sizeof(s_status.utc_date_str), "%04u-%02u-%02u", full_year, mo, dd);
}

// Suddivide una frase NMEA (senza '$ iniziale e senza '*XX' finale) in campi separati da ','
static int splitNmeaFields(char *work_buf, const char **fields, int max_fields) {
  int count = 0;
  char *p = work_buf;
  fields[count++] = p;
  while (*p != '\0' && count < max_fields) {
    if (*p == ',') {
      *p = '\0';
      fields[count++] = p + 1;
    }
    ++p;
  }
  return count;
}

static void applyCandidateFix(double lat, char lat_hemi, double lon, char lon_hemi, bool indicator_valid) {
  if (!indicator_valid) {
    s_status.fix_valid = false;
    return;
  }

  // Rifiuto esplicito della coordinata (0.0, 0.0) come fix valido (requisito TEST 7)
  if (!isfinite(lat) || !isfinite(lon) || fabs(lat) > 90.0 || fabs(lon) > 180.0 || isZeroCoordinate(lat, lon)) {
    s_status.zero_coord_rejected_count++;
    s_status.fix_valid = false;
    return;
  }

  s_status.latitude_deg = lat;
  s_status.longitude_deg = lon;
  s_status.lat_hemisphere = lat_hemi;
  s_status.lon_hemisphere = lon_hemi;
  s_status.fix_valid = true;
  s_status.ever_had_valid_fix = true;
  s_status.last_valid_fix_ms = millis();
  s_status.fix_age_ms = 0;
}

static void parseVerifiedNmeaSentence(const char *sentence_with_dollar, size_t star_pos) {
  // Copia il corpo tra '$' e '*' per tokenizzazione
  if (star_pos <= 1 || star_pos >= GPS_NMEA_LINE_MAX) {
    return;
  }

  char body[GPS_NMEA_LINE_MAX];
  size_t body_len = star_pos - 1;
  memcpy(body, sentence_with_dollar + 1, body_len);
  body[body_len] = '\0';

  const char *fields[24] = {nullptr};
  int nf = splitNmeaFields(body, fields, 24);
  if (nf < 1 || fields[0] == nullptr) {
    return;
  }

  const char *type = fields[0];
  size_t type_len = strlen(type);
  if (type_len < 5) {
    return;
  }

  // Gli ultimi 3 caratteri identificano il messaggio NMEA (GGA, RMC, GSA, GSV, VTG, GLL, TXT)
  const char *msg = type + (type_len - 3);

  // 1. $GNGGA / $GPGGA / $GAGGA / $GBGGA
  if (strcmp(msg, "GGA") == 0) {
    s_status.gga_count++;
    if (nf > 1) parseUtcTimeField(fields[1]);

    uint8_t qual = 0;
    if (nf > 6 && fields[6][0] != '\0') {
      qual = (uint8_t)atoi(fields[6]);
    }
    s_status.fix_quality = qual;

    if (nf > 7 && fields[7][0] != '\0') {
      int num_sv = atoi(fields[7]);
      if (num_sv >= 0 && num_sv <= 99) {
        s_status.satellites_in_use = (uint8_t)num_sv;
        s_status.sats_in_use = (uint8_t)num_sv;
      }
    }

    if (nf > 8 && fields[8][0] != '\0') {
      float hdop_val = strtof(fields[8], nullptr);
      if (isfinite(hdop_val) && hdop_val > 0.0f && hdop_val < 100.0f) {
        s_status.hdop = hdop_val;
      }
    }

    if (nf > 9 && fields[9][0] != '\0') {
      float alt_val = strtof(fields[9], nullptr);
      if (isfinite(alt_val)) {
        s_status.altitude_m = alt_val;
      }
    }

    if (nf > 11 && fields[11][0] != '\0') {
      float sep_val = strtof(fields[11], nullptr);
      if (isfinite(sep_val)) {
        s_status.geoid_sep_m = sep_val;
      }
    }

    if (qual > 0 && nf > 5) {
      double lat = 0.0, lon = 0.0;
      char lat_h = '-', lon_h = '-';
      if (parseNmeaCoordinate(fields[2], fields[3], false, lat, lat_h) &&
          parseNmeaCoordinate(fields[4], fields[5], true, lon, lon_h)) {
        // Verifica che RMC non stia esplicitamente dichiarando stato 'V' nello stesso istante
        applyCandidateFix(lat, lat_h, lon, lon_h, true);
      } else {
        s_status.fix_valid = false;
      }
    } else if (qual == 0) {
      s_status.fix_valid = false;
    }
  }
  // 2. $GNRMC / $GPRMC / $GARMC / $GBRMC
  else if (strcmp(msg, "RMC") == 0) {
    s_status.rmc_count++;
    if (nf > 1) parseUtcTimeField(fields[1]);

    char st = 'V';
    if (nf > 2 && fields[2][0] != '\0') {
      st = (char)toupper((unsigned char)fields[2][0]);
    }
    s_status.rmc_status = st;

    if (nf > 7 && fields[7][0] != '\0') {
      float knots = strtof(fields[7], nullptr);
      if (isfinite(knots) && knots >= 0.0f) {
        s_status.speed_knots = knots;
        s_status.speed_kmh = knots * 1.852f;
      }
    }

    if (nf > 8 && fields[8][0] != '\0') {
      float cog = strtof(fields[8], nullptr);
      if (isfinite(cog) && cog >= 0.0f && cog <= 360.0f) {
        s_status.course_deg = cog;
      }
    }

    if (nf > 9) parseUtcDateField(fields[9]);

    if (st == 'A' && nf > 6) {
      double lat = 0.0, lon = 0.0;
      char lat_h = '-', lon_h = '-';
      if (parseNmeaCoordinate(fields[3], fields[4], false, lat, lat_h) &&
          parseNmeaCoordinate(fields[5], fields[6], true, lon, lon_h)) {
        if (s_status.fix_quality == 0) {
          s_status.fix_quality = 1; // RMC Active indica fix valido anche prima della GGA successiva
        }
        applyCandidateFix(lat, lat_h, lon, lon_h, true);
      } else {
        s_status.fix_valid = false;
      }
    } else if (st == 'V') {
      s_status.fix_valid = false;
    }
  }
  // 3. $GNGSA / $GPGSA
  else if (strcmp(msg, "GSA") == 0) {
    s_status.gsa_count++;
    if (nf > 2 && fields[2][0] != '\0') {
      int dim = atoi(fields[2]);
      if (dim >= 1 && dim <= 3) {
        s_status.fix_dimension = (uint8_t)dim;
      }
    }
    if (nf > 15 && fields[15][0] != '\0') {
      float pdop = strtof(fields[15], nullptr);
      if (isfinite(pdop) && pdop > 0.0f && pdop < 100.0f) {
        s_status.pdop = pdop;
      }
    }
    if (nf > 16 && fields[16][0] != '\0') {
      float hdop = strtof(fields[16], nullptr);
      if (isfinite(hdop) && hdop > 0.0f && hdop < 100.0f) {
        s_status.hdop = hdop;
      }
    }
    if (nf > 17 && fields[17][0] != '\0') {
      float vdop = strtof(fields[17], nullptr);
      if (isfinite(vdop) && vdop > 0.0f && vdop < 100.0f) {
        s_status.vdop = vdop;
      }
    }
  }
  // 4. $GPGSV / $GLGSV / $GAGSV / $GBGSV / $BDGSV / $GNGSV
  else if (strcmp(msg, "GSV") == 0) {
    s_status.gsv_count++;
    uint32_t now = millis();
    if (now - s_last_gsv_ms > 5000) {
      // Azzera contatori costellazioni se il ciclo GSV precedente è vecchio
      s_status.sats_view_gps = 0;
      s_status.sats_view_glonass = 0;
      s_status.sats_view_galileo = 0;
      s_status.sats_view_beidou = 0;
      s_gsv_gn_total = 0;
    }
    s_last_gsv_ms = now;

    if (nf > 3 && fields[3][0] != '\0') {
      int in_view = atoi(fields[3]);
      if (in_view >= 0 && in_view <= 99) {
        uint8_t sv = (uint8_t)in_view;
        if (strncmp(type, "GP", 2) == 0) {
          s_status.sats_view_gps = sv;
        } else if (strncmp(type, "GL", 2) == 0) {
          s_status.sats_view_glonass = sv;
        } else if (strncmp(type, "GA", 2) == 0) {
          s_status.sats_view_galileo = sv;
        } else if (strncmp(type, "GB", 2) == 0 || strncmp(type, "BD", 2) == 0) {
          s_status.sats_view_beidou = sv;
        } else {
          s_gsv_gn_total = sv;
        }

        uint16_t sum_constellations =
            (uint16_t)s_status.sats_view_gps +
            (uint16_t)s_status.sats_view_glonass +
            (uint16_t)s_status.sats_view_galileo +
            (uint16_t)s_status.sats_view_beidou;
        if (sum_constellations > 0) {
          s_status.satellites_in_view = (sum_constellations > 99) ? 99 : (uint8_t)sum_constellations;
        } else {
          s_status.satellites_in_view = s_gsv_gn_total;
        }
        s_status.sats_in_view = s_status.satellites_in_view;
      }
    }
  }
  // 5. $GNGLL / $GPGLL
  else if (strcmp(msg, "GLL") == 0) {
    if (nf > 5) parseUtcTimeField(fields[5]);
    if (nf > 6 && fields[6][0] == 'A') {
      double lat = 0.0, lon = 0.0;
      char lat_h = '-', lon_h = '-';
      if (parseNmeaCoordinate(fields[1], fields[2], false, lat, lat_h) &&
          parseNmeaCoordinate(fields[3], fields[4], true, lon, lon_h)) {
        applyCandidateFix(lat, lat_h, lon, lon_h, true);
      }
    }
  }
  // 6. $GPTXT / $GNTXT
  else if (strcmp(msg, "TXT") == 0) {
    s_status.txt_count++;
  }
}

// Esegue un test sincrono di handshake 2-fasi su 0x50/0x54 (usato solo in gpsInit / gpsHardwareReset)
static bool performSyncHandshake(bool &out_ack50, bool &out_ack54, uint32_t &out_len) {
  out_ack50 = false;
  out_ack54 = false;
  out_len = 0;

  // 1. Invia comando query lunghezza (8 byte) a 0x50
  if (!i2cRawWrite(HW_ADDR_LC76G_W, kCmdQueryLength, sizeof(kCmdQueryLength))) {
    s_status.i2c_tx_fail++;
    s_status.i2c_consecutive_fails++;
    return false;
  }
  out_ack50 = true;
  s_status.i2c_tx_ok++;
  s_status.i2c_consecutive_fails = 0;

  // Attendi che il controller I2C del LC76G prepari i 4 byte di lunghezza su 0x54
  delay(90);

  uint8_t len_bytes[4] = {0, 0, 0, 0};
  if (!i2cRawRead(HW_ADDR_LC76G_R, len_bytes, sizeof(len_bytes))) {
    s_status.i2c_tx_fail++;
    s_status.i2c_consecutive_fails++;
    return false;
  }
  out_ack54 = true;
  s_status.i2c_tx_ok++;
  s_status.i2c_consecutive_fails = 0;

  out_len = (uint32_t)len_bytes[0] |
            ((uint32_t)len_bytes[1] << 8) |
            ((uint32_t)len_bytes[2] << 16) |
            ((uint32_t)len_bytes[3] << 24);
  s_status.last_data_length = out_len;
  if (out_len > 0 && out_len <= kMaxValidDataLength && out_len > s_status.max_data_length_seen) {
    s_status.max_data_length_seen = out_len;
  }

  return true;
}

static void processPayloadBytes(const uint8_t *buf, size_t len, bool from_uart) {
  if (buf == nullptr || len == 0) {
    return;
  }
  s_status.total_bytes_read += (uint32_t)len;
  if (from_uart) {
    s_status.uart_bytes_read += (uint32_t)len;
  }

  for (size_t i = 0; i < len; ++i) {
    uint8_t b = buf[i];
    if (b == 0x00 || b == 0xFF) {
      continue;
    }
    char c = (char)b;
    if (c == '\r' || c == '\n' || (b >= 0x20 && b <= 0x7E)) {
      if (s_status.raw_stream_enabled) {
        Serial.write((uint8_t)c);
      }
      appendRawPreviewByte(c);
      gpsFeedNmeaChar(c);
    }
  }
}

} // namespace

// ============================================================================
// IMPLEMENTAZIONE API PUBBLICA
// ============================================================================

const char *gpsStatusToString(GpsStatus status) {
  switch (status) {
    case GPS_STATUS_UNREACHABLE: return "GPS_STATUS_UNREACHABLE";
    case GPS_STATUS_NO_DATA:     return "GPS_STATUS_NO_DATA";
    case GPS_STATUS_NMEA_NO_FIX: return "GPS_STATUS_NMEA_NO_FIX";
    case GPS_STATUS_VALID_FIX:   return "GPS_STATUS_VALID_FIX";
    default:                     return "GPS_STATUS_UNREACHABLE";
  }
}

const char *gpsStatusToShortString(GpsStatus status) {
  switch (status) {
    case GPS_STATUS_UNREACHABLE: return "UNREACH";
    case GPS_STATUS_NO_DATA:     return "NO DATA";
    case GPS_STATUS_NMEA_NO_FIX: return "NO FIX";
    case GPS_STATUS_VALID_FIX:   return "3D FIX";
    default:                     return "UNREACH";
  }
}

bool gpsFeedNmeaChar(char c) {
  if (c == '$') {
    s_in_sentence = true;
    s_line_len = 0;
    s_line_buf[s_line_len++] = '$';
    s_line_buf[s_line_len] = '\0';
    return false;
  }

  if (!s_in_sentence) {
    return false;
  }

  if (c == '\r' || c == '\n') {
    if (s_line_len < 6) {
      s_in_sentence = false;
      s_line_len = 0;
      return false;
    }
    s_line_buf[s_line_len] = '\0';
    s_in_sentence = false;

    // Verifica presenza e validità del checksum *XX
    const char *star = strrchr(s_line_buf, '*');
    if (star == nullptr || (star - s_line_buf) < 4 || strlen(star) < 3) {
      s_status.nmea_sentences_crc_err++;
      s_line_len = 0;
      updateGpsStatusState();
      return false;
    }

    int hi = hexCharToInt(star[1]);
    int lo = hexCharToInt(star[2]);
    if (hi < 0 || lo < 0) {
      s_status.nmea_sentences_crc_err++;
      s_line_len = 0;
      updateGpsStatusState();
      return false;
    }

    uint8_t expected_crc = (uint8_t)((hi << 4) | lo);
    uint8_t computed_crc = 0;
    for (const char *p = s_line_buf + 1; p < star; ++p) {
      computed_crc ^= (uint8_t)(*p);
    }

    if (computed_crc != expected_crc) {
      s_status.nmea_sentences_crc_err++;
      s_line_len = 0;
      updateGpsStatusState();
      return false;
    }

    // Tronca eventuali caratteri spuri dopo *XX
    size_t star_idx = (size_t)(star - s_line_buf);
    if (star_idx + 3 < GPS_NMEA_LINE_MAX) {
      s_line_buf[star_idx + 3] = '\0';
    }

    s_status.nmea_sentences_valid++;
    s_status.last_nmea_rx_ms = millis();
    pushLastNmeaLine(s_line_buf);
    parseVerifiedNmeaSentence(s_line_buf, star_idx);
    s_line_len = 0;
    updateGpsStatusState();
    return true;
  }

  if (s_line_len + 1 < GPS_NMEA_LINE_MAX) {
    s_line_buf[s_line_len++] = c;
    s_line_buf[s_line_len] = '\0';
  } else {
    // Overflow riga: scarta frase malformata
    s_in_sentence = false;
    s_line_len = 0;
    s_status.nmea_sentences_crc_err++;
  }
  return false;
}

bool gpsHardwareReset() {
  if (!i2cBusIsInitialized()) {
    i2cBusInit();
  }

  // 1. Leggi lo stato iniziale di EXIO7 sul TCA9554 (0x20) e verifica 0x50/0x54 prima del reset
  tca9554Init();
  s_status.exio7_initial_level = tca9554ReadPin(HW_EXIO_GPS_RST);

  bool ack50_pre = false, ack54_pre = false;
  uint32_t len_pre = 0;
  bool pre_ok = performSyncHandshake(ack50_pre, ack54_pre, len_pre);
  s_status.probe_0x50_before_rst = ack50_pre || i2cProbeAddress(HW_ADDR_LC76G_W);
  s_status.probe_0x54_before_rst = ack54_pre || i2cProbeAddress(HW_ADDR_LC76G_R);

  // 2. Configura EXIO7 (P7 del TCA9554 = GPS_RST) come OUTPUT ed esegui impulso di reset
  tca9554SetPinMode(HW_EXIO_GPS_RST, OUTPUT);
  s_status.reset_count++;

  // Sequenza standard: impulso LOW (50 ms) -> rilascio HIGH (attesa boot 180 ms)
  tca9554WritePin(HW_EXIO_GPS_RST, 0);
  delay(50);
  tca9554WritePin(HW_EXIO_GPS_RST, 1);
  s_status.exio7_active_level = 1;
  delay(180);

  bool ack50_post = false, ack54_post = false;
  uint32_t len_post = 0;
  bool post_ok = false;

  for (int attempt = 0; attempt < 3; ++attempt) {
    if (performSyncHandshake(ack50_post, ack54_post, len_post)) {
      post_ok = true;
      break;
    }
    delay(80);
  }

  // Se non risponde con EXIO7=HIGH, verifica anche polarità opposta (EXIO7=LOW)
  if (!post_ok && !ack50_post) {
    tca9554WritePin(HW_EXIO_GPS_RST, 0);
    delay(180);
    for (int attempt = 0; attempt < 2; ++attempt) {
      if (performSyncHandshake(ack50_post, ack54_post, len_post)) {
        post_ok = true;
        s_status.exio7_active_level = 0;
        break;
      }
      delay(80);
    }
    if (!post_ok) {
      // Ripristina stato di default HIGH (rilascio reset attivo-basso)
      tca9554WritePin(HW_EXIO_GPS_RST, 1);
      s_status.exio7_active_level = 1;
    }
  }

  s_status.probe_0x50_after_rst = ack50_post || i2cProbeAddress(HW_ADDR_LC76G_W);
  s_status.probe_0x54_after_rst = ack54_post || i2cProbeAddress(HW_ADDR_LC76G_R);
  s_status.ack_0x50 = s_status.probe_0x50_after_rst || pre_ok;
  s_status.ack_0x54 = s_status.probe_0x54_after_rst || ack54_pre;
  s_status.i2c_reachable = (post_ok || pre_ok || s_status.ack_0x50 || s_status.ack_0x54);
  if (s_status.i2c_reachable) {
    s_status.i2c_consecutive_fails = 0;
  }

  // Se durante l'handshake post-reset ci sono già byte pronti (len_post > 0),
  // prepara subito la macchina a stati per scaricarli in Fase 3
  if (post_ok && len_post > 0 && len_post <= kMaxValidDataLength) {
    s_len_raw_bytes[0] = (uint8_t)(len_post & 0xFF);
    s_len_raw_bytes[1] = (uint8_t)((len_post >> 8) & 0xFF);
    s_len_raw_bytes[2] = (uint8_t)((len_post >> 16) & 0xFF);
    s_len_raw_bytes[3] = (uint8_t)((len_post >> 24) & 0xFF);
    s_status.protocol_phase = GPS_PHASE_3_REQUEST_PAYLOAD;
    s_phase_ts_ms = millis();
  } else {
    s_status.protocol_phase = GPS_PHASE_1_QUERY_LEN;
    s_phase_ts_ms = millis();
  }

  updateGpsStatusState();
  return s_status.i2c_reachable;
}

bool gpsInit() {
  bool keep_raw = s_status.raw_stream_enabled;
  memset(&s_status, 0, sizeof(s_status));

  s_status.status = GPS_STATUS_UNREACHABLE;
  s_status.status_str = gpsStatusToString(GPS_STATUS_UNREACHABLE);
  s_status.status_short_str = gpsStatusToShortString(GPS_STATUS_UNREACHABLE);
  s_status.exio7_initial_level = -1;
  s_status.exio7_active_level = 1;
  s_status.protocol_phase = GPS_PHASE_IDLE;
  s_status.fix_dimension = 1;
  s_status.rmc_status = 'V';
  s_status.lat_hemisphere = '-';
  s_status.lon_hemisphere = '-';
  s_status.hdop = 99.9f;
  s_status.pdop = 99.9f;
  s_status.vdop = 99.9f;
  strncpy(s_status.utc_time_str, "--:--:--", sizeof(s_status.utc_time_str) - 1);
  strncpy(s_status.utc_date_str, "----/--/--", sizeof(s_status.utc_date_str) - 1);
  s_status.raw_stream_enabled = keep_raw;

  s_line_len = 0;
  s_in_sentence = false;
  s_more_fifo_pending = false;

  if (!i2cBusIsInitialized()) {
    i2cBusInit();
  }

  // Start the existing non-blocking protocol without pulsing GPS reset.
  bool ok = i2cProbeAddress(HW_ADDR_LC76G_W);
  s_status.ack_0x50 = ok;
  s_status.i2c_reachable = ok;
  s_status.protocol_phase = GPS_PHASE_1_QUERY_LEN;
  s_phase_ts_ms = millis();
  s_status.initialized = true;
  updateGpsStatusState();
  return ok;
}

void gpsUpdate() {
  if (!s_status.initialized) {
    gpsInit();
  }

  // Se il fallback UART è stato attivato, leggi in modo non bloccante eventuali byte da Serial1
  if (s_status.uart_fallback_active) {
    int avail = Serial1.available();
    if (avail > 0) {
      uint8_t uart_buf[128];
      size_t to_read = (avail > (int)sizeof(uart_buf)) ? sizeof(uart_buf) : (size_t)avail;
      size_t got = Serial1.readBytes(uart_buf, to_read);
      processPayloadBytes(uart_buf, got, true);
    }
  }

  uint32_t now = millis();

  switch (s_status.protocol_phase) {
    case GPS_PHASE_IDLE: {
      if (now - s_phase_ts_ms >= s_idle_wait_ms) {
        s_status.protocol_phase = GPS_PHASE_1_QUERY_LEN;
      }
      break;
    }

    // FASE 1: Invia a 0x50 gli 8 byte {0x08, 0x00, 0x51, 0xAA, 0x04, 0x00, 0x00, 0x00}
    case GPS_PHASE_1_QUERY_LEN: {
      if (i2cRawWrite(HW_ADDR_LC76G_W, kCmdQueryLength, sizeof(kCmdQueryLength))) {
        s_status.ack_0x50 = true;
        s_status.i2c_reachable = true;
        s_status.i2c_tx_ok++;
        s_status.i2c_consecutive_fails = 0;
        s_phase_ts_ms = now;
        s_status.protocol_phase = GPS_PHASE_2_READ_LEN;
      } else {
        s_status.ack_0x50 = false;
        s_status.i2c_tx_fail++;
        s_status.i2c_consecutive_fails++;
        s_idle_wait_ms = kIdleAfterErrorMs;
        s_phase_ts_ms = now;
        s_status.protocol_phase = GPS_PHASE_IDLE;
      }
      break;
    }

    // FASE 2: Dopo >= 85 ms, leggi 4 byte readData[0..3] da 0x54 e calcola dataLength
    case GPS_PHASE_2_READ_LEN: {
      if (now - s_phase_ts_ms < kPhaseWaitMs) {
        break;
      }

      memset(s_len_raw_bytes, 0, sizeof(s_len_raw_bytes));
      if (!i2cRawRead(HW_ADDR_LC76G_R, s_len_raw_bytes, sizeof(s_len_raw_bytes))) {
        s_status.ack_0x54 = false;
        s_status.i2c_tx_fail++;
        s_status.i2c_consecutive_fails++;
        s_idle_wait_ms = kIdleAfterErrorMs;
        s_phase_ts_ms = now;
        s_status.protocol_phase = GPS_PHASE_IDLE;
        break;
      }

      s_status.ack_0x54 = true;
      s_status.i2c_reachable = true;
      s_status.i2c_tx_ok++;
      s_status.i2c_consecutive_fails = 0;
      s_status.poll_cycles++;

      uint32_t data_length = (uint32_t)s_len_raw_bytes[0] |
                             ((uint32_t)s_len_raw_bytes[1] << 8) |
                             ((uint32_t)s_len_raw_bytes[2] << 16) |
                             ((uint32_t)s_len_raw_bytes[3] << 24);
      s_status.last_data_length = data_length;

      if (data_length == 0 || data_length > kMaxValidDataLength) {
        s_more_fifo_pending = false;
        s_idle_wait_ms = kIdleNoDataMs;
        s_phase_ts_ms = now;
        s_status.protocol_phase = GPS_PHASE_IDLE;
      } else {
        s_status.non_empty_polls++;
        if (data_length > s_status.max_data_length_seen) {
          s_status.max_data_length_seen = data_length;
        }
        s_phase_ts_ms = now;
        s_status.protocol_phase = GPS_PHASE_3_REQUEST_PAYLOAD;
      }
      break;
    }

    // FASE 3: Dopo >= 85 ms, se 0 < dataLength <= 2048, invia comando {0x00, 0x20, 0x51, 0xAA, len[0..3]} a 0x50
    case GPS_PHASE_3_REQUEST_PAYLOAD: {
      if (now - s_phase_ts_ms < kPhaseWaitMs) {
        break;
      }

      uint32_t total_avail = s_status.last_data_length;
      if (total_avail == 0 || total_avail > kMaxValidDataLength) {
        s_idle_wait_ms = kIdleNoDataMs;
        s_phase_ts_ms = now;
        s_status.protocol_phase = GPS_PHASE_IDLE;
        break;
      }

      // Rispetta la dimensione massima del buffer Wire (HW_I2C_BUF_SIZE = 512) per singola transazione
      uint32_t req_len = (total_avail > (uint32_t)kPayloadBufCap)
                             ? (uint32_t)kPayloadBufCap
                             : total_avail;
      s_status.last_chunk_requested = req_len;
      s_more_fifo_pending = (total_avail > req_len);

      uint8_t cmd_payload[8] = {
          kCmdReadPayloadPrefix[0],
          kCmdReadPayloadPrefix[1],
          kCmdReadPayloadPrefix[2],
          kCmdReadPayloadPrefix[3],
          (uint8_t)(req_len & 0xFF),
          (uint8_t)((req_len >> 8) & 0xFF),
          (uint8_t)((req_len >> 16) & 0xFF),
          (uint8_t)((req_len >> 24) & 0xFF)};

      if (i2cRawWrite(HW_ADDR_LC76G_W, cmd_payload, sizeof(cmd_payload))) {
        s_status.ack_0x50 = true;
        s_status.i2c_tx_ok++;
        s_status.i2c_consecutive_fails = 0;
        s_phase_ts_ms = now;
        s_status.protocol_phase = GPS_PHASE_4_READ_PAYLOAD;
      } else {
        s_status.ack_0x50 = false;
        s_status.i2c_tx_fail++;
        s_status.i2c_consecutive_fails++;
        s_idle_wait_ms = kIdleAfterErrorMs;
        s_phase_ts_ms = now;
        s_status.protocol_phase = GPS_PHASE_IDLE;
      }
      break;
    }

    // FASE 4: Dopo >= 85 ms, leggi il blocco NMEA da 0x54 e passa ogni byte al parser NMEA
    case GPS_PHASE_4_READ_PAYLOAD: {
      if (now - s_phase_ts_ms < kPhaseWaitMs) {
        break;
      }

      uint32_t req_len = s_status.last_chunk_requested;
      if (req_len == 0 || req_len > (uint32_t)kPayloadBufCap) {
        s_idle_wait_ms = kIdleNoDataMs;
        s_phase_ts_ms = now;
        s_status.protocol_phase = GPS_PHASE_IDLE;
        break;
      }

      if (i2cRawRead(HW_ADDR_LC76G_R, s_payload_buf, (size_t)req_len)) {
        s_status.ack_0x54 = true;
        s_status.i2c_reachable = true;
        s_status.i2c_tx_ok++;
        s_status.i2c_consecutive_fails = 0;

        processPayloadBytes(s_payload_buf, (size_t)req_len, false);

        s_idle_wait_ms = s_more_fifo_pending ? kIdleMorePendingMs : kIdleAfterDataMs;
        s_phase_ts_ms = millis();
        s_status.protocol_phase = GPS_PHASE_IDLE;
      } else {
        s_status.ack_0x54 = false;
        s_status.i2c_tx_fail++;
        s_status.i2c_consecutive_fails++;
        s_idle_wait_ms = kIdleAfterErrorMs;
        s_phase_ts_ms = now;
        s_status.protocol_phase = GPS_PHASE_IDLE;
      }
      break;
    }
  }

  updateGpsStatusState();
}

const GpsStatusSnapshot &gpsGetStatus() {
  updateGpsStatusState();
  return s_status;
}

void gpsSetRawStreamEnabled(bool enabled) {
  s_status.raw_stream_enabled = enabled;
}

bool gpsIsRawStreamEnabled() {
  return s_status.raw_stream_enabled;
}

bool gpsToggleRawStream() {
  s_status.raw_stream_enabled = !s_status.raw_stream_enabled;
  return s_status.raw_stream_enabled;
}

uint8_t gpsGetLastNmeaLines(char out_lines[GPS_LAST_LINES_COUNT][GPS_NMEA_LINE_MAX]) {
  if (out_lines != nullptr) {
    for (uint8_t i = 0; i < GPS_LAST_LINES_COUNT; ++i) {
      memcpy(out_lines[i], s_status.last_nmea_lines[i], GPS_NMEA_LINE_MAX);
    }
  }
  return s_status.last_nmea_lines_count;
}

const char *gpsGetLastNmeaLine(uint8_t index) {
  if (index >= GPS_LAST_LINES_COUNT) {
    return "";
  }
  return s_status.last_nmea_lines[index];
}

bool gpsTryUartFallback(int tx_pin, int rx_pin, uint32_t baud, uint32_t listen_ms) {
  s_status.uart_fallback_tested = true;
  uint32_t bauds_to_try[2] = {115200, 9600};
  size_t num_bauds = 2;

  if (baud == 9600) {
    bauds_to_try[0] = 9600;
    bauds_to_try[1] = 115200;
  } else if (baud != 0 && baud != 115200) {
    bauds_to_try[0] = baud;
    num_bauds = 1;
  }

  uint32_t valid_before = s_status.nmea_sentences_valid;

  for (size_t b_idx = 0; b_idx < num_bauds; ++b_idx) {
    uint32_t cur_baud = bauds_to_try[b_idx];
    Serial1.end();
    Serial1.setRxBufferSize(512);
    Serial1.begin(cur_baud, SERIAL_8N1, rx_pin, tx_pin);

    uint32_t start_ms = millis();
    bool saw_dollar = false;
    while (millis() - start_ms < listen_ms) {
      int avail = Serial1.available();
      if (avail > 0) {
        uint8_t buf[64];
        size_t n = (avail > (int)sizeof(buf)) ? sizeof(buf) : (size_t)avail;
        size_t got = Serial1.readBytes(buf, n);
        for (size_t i = 0; i < got; ++i) {
          if (buf[i] == '$') saw_dollar = true;
        }
        processPayloadBytes(buf, got, true);
        if (s_status.nmea_sentences_valid > valid_before) {
          s_status.uart_fallback_active = true;
          s_status.uart_fallback_baud = cur_baud;
          updateGpsStatusState();
          return true;
        }
      }
      delay(5);
    }

    if (saw_dollar && s_status.nmea_sentences_valid > valid_before) {
      s_status.uart_fallback_active = true;
      s_status.uart_fallback_baud = cur_baud;
      updateGpsStatusState();
      return true;
    }
  }

  // Nessun flusso NMEA rilevato su GPIO17/18 (R15/R16 tipicamente NC sulla scheda)
  Serial1.end();
  s_status.uart_fallback_active = false;
  updateGpsStatusState();
  return false;
}

bool gpsRunDiagnosticTest(uint32_t duration_ms, Stream &out) {
  if (!s_status.initialized) {
    gpsInit();
  }

  uint32_t start_ms = millis();
  while (millis() - start_ms < duration_ms) {
    gpsUpdate();
    delay(10);
  }

  // Se il modulo non ha risposto via I2C, esegui automaticamente un tentativo di fallback UART diagnostico
  if (s_status.status == GPS_STATUS_UNREACHABLE && !s_status.uart_fallback_tested) {
    out.println(F("[TEST 7][GPS] I2C 0x50/0x54 non raggiungibile, provo fallback UART opzionale su GPIO17/18..."));
    gpsTryUartFallback(HW_GPS_UART_TX_MCU, HW_GPS_UART_RX_MCU, 115200, 1000);
  }

  gpsPrintDiagnostics(out);
  return (s_status.status != GPS_STATUS_UNREACHABLE);
}

void gpsPrintDiagnostics(Stream &out) {
  updateGpsStatusState();

  out.println(F("================================================================"));
  out.println(F("[TEST 7] GNSS Quectel LC76G (I2C 0x50 Write / 0x54 Read + EXIO7)"));
  out.println(F("================================================================"));
  out.printf("  Stato Modulo        : %s (%s)\n", s_status.status_str, s_status.status_short_str);
  out.printf("  I2C Raggiungibile   : %s (0x50 ACK=%s, 0x54 ACK=%s)\n",
             s_status.i2c_reachable ? "SI" : "NO",
             s_status.ack_0x50 ? "OK" : "FAIL",
             s_status.ack_0x54 ? "OK" : "FAIL");
  out.printf("  TCA9554 EXIO7 (RST) : initial=%d, active_level=%u, reset_count=%lu\n",
             s_status.exio7_initial_level,
             (unsigned)s_status.exio7_active_level,
             (unsigned long)s_status.reset_count);
  out.printf("  Probe Pre/Post RST  : pre(0x50=%d, 0x54=%d) -> post(0x50=%d, 0x54=%d)\n",
             s_status.probe_0x50_before_rst ? 1 : 0,
             s_status.probe_0x54_before_rst ? 1 : 0,
             s_status.probe_0x50_after_rst ? 1 : 0,
             s_status.probe_0x54_after_rst ? 1 : 0);
  out.printf("  Statistiche I2C     : tx_ok=%lu, tx_fail=%lu, polls=%lu (non_empty=%lu)\n",
             (unsigned long)s_status.i2c_tx_ok,
             (unsigned long)s_status.i2c_tx_fail,
             (unsigned long)s_status.poll_cycles,
             (unsigned long)s_status.non_empty_polls);
  out.printf("  Payload NMEA I2C    : total_bytes=%lu, last_len=%lu, max_len=%lu\n",
             (unsigned long)s_status.total_bytes_read,
             (unsigned long)s_status.last_data_length,
             (unsigned long)s_status.max_data_length_seen);
  out.printf("  Parser Checksum *XX : valid=%lu, crc_err=%lu (GGA=%lu, RMC=%lu, GSA=%lu, GSV=%lu, TXT=%lu)\n",
             (unsigned long)s_status.nmea_sentences_valid,
             (unsigned long)s_status.nmea_sentences_crc_err,
             (unsigned long)s_status.gga_count,
             (unsigned long)s_status.rmc_count,
             (unsigned long)s_status.gsa_count,
             (unsigned long)s_status.gsv_count,
             (unsigned long)s_status.txt_count);
  out.printf("  Satelliti           : in_use=%u, in_view=%u (GPS=%u, GLONASS=%u, GALILEO=%u, BEIDOU=%u)\n",
             (unsigned)s_status.satellites_in_use,
             (unsigned)s_status.satellites_in_view,
             (unsigned)s_status.sats_view_gps,
             (unsigned)s_status.sats_view_glonass,
             (unsigned)s_status.sats_view_galileo,
             (unsigned)s_status.sats_view_beidou);
  out.printf("  Stato Fix           : valid=%s, GGA_qual=%u, GSA_dim=%uD, RMC_status=%c, (0,0)_rejected=%lu\n",
             s_status.fix_valid ? "VALID" : "NO_FIX",
             (unsigned)s_status.fix_quality,
             (unsigned)s_status.fix_dimension,
             s_status.rmc_status,
             (unsigned long)s_status.zero_coord_rejected_count);
  if (s_status.fix_valid) {
    out.printf("  Coordinate Fix      : Lat=%.6f (%c), Lon=%.6f (%c), Alt=%.1f m\n",
               s_status.latitude_deg,
               s_status.lat_hemisphere,
               s_status.longitude_deg,
               s_status.lon_hemisphere,
               s_status.altitude_m);
  } else {
    out.println(F("  Coordinate Fix      : N/A (In attesa di fix satellitare / cold start / indoor)"));
  }
  out.printf("  DOP & Cinematica    : HDOP=%.2f, PDOP=%.2f, VDOP=%.2f, Speed=%.1f km/h, Course=%.1f deg\n",
             s_status.hdop,
             s_status.pdop,
             s_status.vdop,
             s_status.speed_kmh,
             s_status.course_deg);
  out.printf("  UTC & Fix Age       : Date=%s, Time=%s, fix_age_ms=%lu\n",
             s_status.utc_date_str,
             s_status.utc_time_str,
             (unsigned long)s_status.fix_age_ms);
  if (s_status.uart_fallback_tested) {
    out.printf("  UART Fallback 17/18 : active=%s, baud=%lu, uart_bytes=%lu\n",
               s_status.uart_fallback_active ? "YES" : "NO",
               (unsigned long)s_status.uart_fallback_baud,
               (unsigned long)s_status.uart_bytes_read);
  }
  out.printf("  Ultime %u righe NMEA :\n", (unsigned)s_status.last_nmea_lines_count);
  for (uint8_t i = 0; i < s_status.last_nmea_lines_count; ++i) {
    out.printf("    [%u] %s\n", (unsigned)i, s_status.last_nmea_lines[i]);
  }
  out.println(F("================================================================"));
}
