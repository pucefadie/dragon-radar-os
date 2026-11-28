#include <algorithm>
#include "ota_service.h"
#include <WiFi.h>
#include "display_module.h"
#include "app_radar.h"
#include "radar_ui_geometry.h"
#include "beacon_service.h"
#include "app_settings.h"
#include "app_manager.h"
#include "sd_service.h"
#include "settings_manager.h"
#include "wifi_service.h"
#include "audiobookshelf_client.h"
#include "sensors_module.h"

#include <Arduino_GFX_Library.h>
#include <TouchDrvCSTXXX.hpp>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

const char *secureLinkLabel(SecureLinkStatus status) {
  switch (status) {
    case SecureLinkStatus::Paired: return "SFERA AUTENTICATA";
    case SecureLinkStatus::Lost: return "SFERA LINK PERSO";
    case SecureLinkStatus::AwaitingHandshake: return "SFERA ATTESA HANDSHAKE";
    case SecureLinkStatus::ChannelMismatch: return "CANALE RF NON ALLINEATO";
    case SecureLinkStatus::Unavailable: return "RADIO SICURA NON ATTIVA";
  }
  return "";
}

const char *signalQualityLabel(SignalQuality quality) {
  switch (quality) {
    case SignalQuality::Weak: return "DEBOLE";
    case SignalQuality::Fair: return "MEDIO";
    case SignalQuality::Strong: return "FORTE";
    case SignalQuality::Unavailable: return "--";
  }
  return "--";
}

const char *proximityLabel(const ProximitySnapshot &signal, SecureLinkStatus link) {
  if (signal.state == ProximityState::Found) return "SFERA TROVATA";
  if (link != SecureLinkStatus::Paired) return "SFERA NON COLLEGATA";
  switch (signal.state) {
    case ProximityState::Searching: return "ATTESA MISURA RSSI";
    case ProximityState::Lost: return "MISURA RSSI SCADUTA";
    case ProximityState::Weak: return "SEGNALE DEBOLE";
    case ProximityState::Approaching: return "IN AVVICINAMENTO";
    case ProximityState::MovingAway: return "IN ALLONTANAMENTO";
    case ProximityState::Near: return "VICINANZA ELEVATA";
    case ProximityState::Stable: return "SEGNALE STABILE";
    case ProximityState::Found: return "SFERA TROVATA";
  }
  return "";
}

constexpr int16_t kCenterX = HW_LCD_WIDTH / 2;  // 233
constexpr int16_t kCenterY = HW_LCD_HEIGHT / 2; // 233
constexpr float kDegToRad = 0.017453292519943295f;

/**
 * @brief Sottoclasse di Arduino_Canvas che garantisce l'allocazione del framebuffer
 *        466x466 RGB565 (~434 KB) nella PSRAM OPI da 8 MB prima del fallback in RAM interna.
 */
class PsramCanvas : public Arduino_Canvas {
public:
  PsramCanvas(int16_t w, int16_t h, Arduino_G *output, int16_t output_x = 0, int16_t output_y = 0, uint8_t r = 0)
      : Arduino_Canvas(w, h, output, output_x, output_y, r) {}

  bool allocatePsramBuffer() {
    if (_framebuffer != nullptr) {
      return true;
    }
    const size_t bytes = (size_t)_width * (size_t)_height * sizeof(uint16_t);
    if (psramFound()) {
      _framebuffer = (uint16_t *)heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (_framebuffer == nullptr) {
        _framebuffer = (uint16_t *)ps_malloc(bytes);
      }
    }
    if (_framebuffer == nullptr) {
      _framebuffer = (uint16_t *)heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_8BIT);
    }
    if (_framebuffer == nullptr) {
      _framebuffer = (uint16_t *)malloc(bytes);
    }
    if (_framebuffer != nullptr) {
      memset(_framebuffer, 0, bytes);
      return true;
    }
    return false;
  }
};

// Oggetti driver QSPI CO5300 + Framebuffer PSRAM + Touch CST9217
Arduino_DataBus *s_bus = nullptr;
Arduino_CO5300 *s_gfx = nullptr;
PsramCanvas *s_canvas = nullptr;
TouchDrvCST92xx s_touch;

bool s_display_initialized = false;
bool s_canvas_ready = false;
bool s_touch_online = false;
uint8_t s_brightness = 220;

volatile bool s_touch_irq_pending = false;
bool s_touch_pressed_state = false;
int16_t s_last_touch_x = kCenterX;
int16_t s_last_touch_y = kCenterY;
uint32_t s_last_touch_poll_ms = 0;
uint32_t s_last_tap_ms = 0;
uint32_t s_tap_count = 0;

void IRAM_ATTR onTouchInterrupt() {
  s_touch_irq_pending = true;
}

bool consumeTouchInterrupt() {
  noInterrupts();
  const bool pending = s_touch_irq_pending;
  s_touch_irq_pending = false;
  interrupts();
  return pending;
}

// Callback I2C custom per SensorLib TouchDrvCST92xx:
// opera sul bus Wire condiviso tramite il mutex di i2c_bus senza MAI richiamare Wire.begin()
bool touchI2cCallback(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len, bool writeReg, bool isWrite) {
  if (!i2cBusLock(150)) {
    return false;
  }
  bool ok = false;
  if (isWrite) {
    if (writeReg) {
      ok = i2cWriteBytes(addr, reg, buf, len);
    } else {
      ok = i2cRawWrite(addr, buf, len);
    }
  } else {
    if (writeReg) {
      ok = i2cReadBytes(addr, reg, buf, len);
    } else {
      ok = i2cRawRead(addr, buf, len);
    }
  }
  i2cBusUnlock();
  return ok;
}

uint32_t touchHalCallback(SensorCommCustomHal::Operation op, void *param1, void *param2) {
  switch (op) {
    case SensorCommCustomHal::OP_PINMODE: {
      uint8_t pin = (uint8_t)reinterpret_cast<uintptr_t>(param1);
      uint8_t mode = (uint8_t)reinterpret_cast<uintptr_t>(param2);
      pinMode(pin, mode);
      break;
    }
    case SensorCommCustomHal::OP_DIGITALWRITE: {
      uint8_t pin = (uint8_t)reinterpret_cast<uintptr_t>(param1);
      uint8_t level = (uint8_t)reinterpret_cast<uintptr_t>(param2);
      digitalWrite(pin, level);
      break;
    }
    case SensorCommCustomHal::OP_DIGITALREAD: {
      uint8_t pin = (uint8_t)reinterpret_cast<uintptr_t>(param1);
      return (uint32_t)digitalRead(pin);
    }
    case SensorCommCustomHal::OP_MILLIS:
      return millis();
    case SensorCommCustomHal::OP_DELAY: {
      uint32_t ms = (uint32_t)reinterpret_cast<uintptr_t>(param1);
      delay(ms);
      break;
    }
    case SensorCommCustomHal::OP_DELAYMICROSECONDS: {
      uint32_t us = (uint32_t)reinterpret_cast<uintptr_t>(param1);
      delayMicroseconds(us);
      break;
    }
    default:
      break;
  }
  return 0;
}

void touchDigitalWriteCb(uint8_t gpio, uint8_t level) {
  digitalWrite(gpio, level);
}

uint8_t touchDigitalReadCb(uint8_t gpio) {
  return (uint8_t)digitalRead(gpio);
}

void touchPinModeCb(uint8_t gpio, uint8_t mode) {
  pinMode(gpio, mode);
}

// Restituisce il target di disegno (framebuffer PSRAM se attivo, altrimenti display diretto)
Arduino_GFX *getDrawTarget() {
  if (s_canvas_ready && s_canvas != nullptr) {
    return s_canvas;
  }
  return s_gfx;
}

// Trasferisce il framebuffer PSRAM al pannello CO5300 in un singolo burst QSPI zero-flicker
void flushDisplay() {
  if (s_canvas_ready && s_canvas != nullptr && s_gfx != nullptr) {
    s_gfx->draw16bitRGBBitmap(0, 0, s_canvas->getFramebuffer(), HW_LCD_WIDTH, HW_LCD_HEIGHT);
  }
}

float safeFloat(float v, float fallback = 0.0f) {
  if (isnan(v) || isinf(v)) {
    return fallback;
  }
  return v;
}

float normalizeHeading(float deg) {
  deg = safeFloat(deg, 0.0f);
  while (deg < 0.0f) {
    deg += 360.0f;
  }
  while (deg >= 360.0f) {
    deg -= 360.0f;
  }
  return deg;
}

const char *headingToCardinal(float deg) {
  static const char *const kDirs[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  float norm = normalizeHeading(deg);
  int idx = (int)((norm + 22.5f) / 45.0f) & 7;
  return kDirs[idx];
}

void drawCenteredText(Arduino_GFX *d, int16_t y, const char *text, uint16_t fg, uint8_t size = 1) {
  if (!d || !text) {
    return;
  }
  int16_t len = (int16_t)strlen(text);
  int16_t w = len * 6 * (int16_t)size;
  int16_t x = kCenterX - (w / 2);
  if (x < 4) {
    x = 4;
  }
  d->setTextSize(size);
  d->setTextColor(fg);
  d->setCursor(x, y);
  d->print(text);
}

void drawBadge(Arduino_GFX *d,
               int16_t x,
               int16_t y,
               int16_t w,
               int16_t h,
               const char *label,
               uint16_t border_color,
               uint16_t fill_color,
               uint16_t text_color,
               uint8_t text_size = 1) {
  if (!d) {
    return;
  }
  d->fillRoundRect(x, y, w, h, 4, fill_color);
  d->drawRoundRect(x, y, w, h, 4, border_color);
  if (label && label[0] != '\0') {
    int16_t tw = (int16_t)strlen(label) * 6 * (int16_t)text_size;
    int16_t th = 8 * (int16_t)text_size;
    int16_t tx = x + (w - tw) / 2;
    int16_t ty = y + (h - th) / 2;
    if (tx < x + 2) {
      tx = x + 2;
    }
    d->setTextSize(text_size);
    d->setTextColor(text_color);
    d->setCursor(tx, ty);
    d->print(label);
  }
}

void drawOuterCircularFrame(Arduino_GFX *d, uint8_t active_page, bool has_error, bool cal_mode) {
  uint16_t ring_color = cal_mode ? RADAR_COLOR_AMBER : (has_error ? RADAR_COLOR_ORANGE : RADAR_COLOR_GRID_BRIGHT);
  d->drawCircle(kCenterX, kCenterY, 232, RADAR_COLOR_GRID_DARK);
  d->drawCircle(kCenterX, kCenterY, 229, RADAR_COLOR_GRID_MID);
  d->drawCircle(kCenterX, kCenterY, 228, ring_color);

  // Indicatori pagina sul bordo inferiore (ben rientrati a y=434, dx <= 45 px)
  const int16_t base_y = 434;
  for (uint8_t p = 0; p < RADAR_PAGE_COUNT; ++p) {
    int16_t bx = kCenterX - 54 + (int16_t)p * 28;
    bool sel = (!cal_mode && (p == (active_page % RADAR_PAGE_COUNT)));
    uint16_t fill = sel ? RADAR_COLOR_GRID_BRIGHT : 0x1082;
    uint16_t fg = sel ? RADAR_COLOR_BG : RADAR_COLOR_GRAY;
    uint16_t brd = sel ? RADAR_COLOR_WHITE : RADAR_COLOR_GRID_MID;
    char lbl[4];
    snprintf(lbl, sizeof(lbl), "P%u", (unsigned)p);
    drawBadge(d, bx, base_y, 24, 14, lbl, brd, fill, fg, 1);
  }
}

const char *resolveButtonStateStr(const RadarSystemSnapshot &snap) {
  if (snap.btn_state_str[0] != '\0') {
    return snap.btn_state_str;
  }
  if (snap.btn_pressed) {
    return "PRESS";
  }
  switch (snap.btn_state) {
    case RADAR_BTN_PRESS:
      return "PRESS";
    case RADAR_BTN_SHORT:
      return "SHORT";
    case RADAR_BTN_LONG:
      return "LONG";
    default:
      return "IDLE";
  }
}

const char *resolveGpsStatusShort(uint8_t gps_status) {
  switch (gps_status) {
    case RADAR_GPS_VALID_FIX:
      return "3D FIX";
    case RADAR_GPS_NMEA_NO_FIX:
      return "NO FIX";
    case RADAR_GPS_NO_DATA:
      return "NO DATA";
    case RADAR_GPS_UNREACHABLE:
    default:
      return "UNREACH";
  }
}

uint16_t resolveGpsStatusColor(uint8_t gps_status) {
  switch (gps_status) {
    case RADAR_GPS_VALID_FIX:
      return RADAR_COLOR_GRID_BRIGHT;
    case RADAR_GPS_NMEA_NO_FIX:
      return RADAR_COLOR_YELLOW;
    case RADAR_GPS_NO_DATA:
      return RADAR_COLOR_ORANGE;
    case RADAR_GPS_UNREACHABLE:
    default:
      return RADAR_COLOR_RED;
  }
}

// ============================================================================
// PAGINA 0: DRAGON RADAR PRINCIPALE (Griglia + Bussola + Telemetria Circolare)
// ============================================================================
void renderPage0MainCompass(Arduino_GFX *d, const RadarSystemSnapshot &snap) {
  // 1. Griglia cartesiana verde Dragon Radar all'interno di r=228
  for (int16_t offset = -200; offset <= 200; offset += 50) {
    if (offset == 0) {
      continue;
    }
    int16_t span = (int16_t)sqrtf((float)(226 * 226 - offset * offset));
    d->drawFastHLine(kCenterX - span, kCenterY + offset, span * 2, 0x0160);
    d->drawFastVLine(kCenterX + offset, kCenterY - span, span * 2, 0x0160);
  }
  d->drawFastHLine(kCenterX - 226, kCenterY, 452, RADAR_COLOR_GRID_MID);
  d->drawFastVLine(kCenterX, kCenterY - 226, 452, RADAR_COLOR_GRID_MID);

  // 2. Cerchi concentrici richiesti: r = 50, 100, 150, 200, 228
  static const int16_t kRadii[5] = {50, 100, 150, 200, 228};
  for (uint8_t i = 0; i < 5; ++i) {
    uint16_t col = (i == 4) ? RADAR_COLOR_GRID_BRIGHT : ((i % 2 == 1) ? RADAR_COLOR_GRID_MID : RADAR_COLOR_GRID_DARK);
    d->drawCircle(kCenterX, kCenterY, kRadii[i], col);
  }

  // 3. Tacche graduate ogni 30° sul perimetro circolare + punti cardinali N, E, S, W
  for (int deg = 0; deg < 360; deg += 30) {
    float rad = (float)(deg - 90) * kDegToRad;
    float cs = cosf(rad);
    float sn = sinf(rad);
    bool is_cardinal = (deg % 90 == 0);
    int16_t r_in = is_cardinal ? 210 : 216;
    int16_t r_out = 227;
    int16_t x0 = kCenterX + (int16_t)roundf(cs * (float)r_in);
    int16_t y0 = kCenterY + (int16_t)roundf(sn * (float)r_in);
    int16_t x1 = kCenterX + (int16_t)roundf(cs * (float)r_out);
    int16_t y1 = kCenterY + (int16_t)roundf(sn * (float)r_out);
    uint16_t tcol = (deg == 0) ? RADAR_COLOR_RED : (is_cardinal ? RADAR_COLOR_YELLOW : RADAR_COLOR_GRID_BRIGHT);
    d->drawLine(x0, y0, x1, y1, tcol);

    if (!is_cardinal) {
      char dstr[6];
      snprintf(dstr, sizeof(dstr), "%d", deg);
      int16_t tx = kCenterX + (int16_t)roundf(cs * 198.0f) - (int16_t)strlen(dstr) * 3;
      int16_t ty = kCenterY + (int16_t)roundf(sn * 198.0f) - 4;
      d->setTextSize(1);
      d->setTextColor(RADAR_COLOR_GRID_BRIGHT);
      d->setCursor(tx, ty);
      d->print(dstr);
    }
  }

  // Etichette cardinali N, E, S, W ben visibili dentro il bordo
  d->setTextSize(2);
  d->setTextColor(RADAR_COLOR_RED);
  d->setCursor(kCenterX - 5, 14);
  d->print("N");
  d->setTextColor(RADAR_COLOR_YELLOW);
  d->setCursor(434, kCenterY - 7);
  d->print("E");
  d->setCursor(kCenterX - 5, 412);
  d->print("S");
  d->setCursor(20, kCenterY - 7);
  d->print("W");

  // 4. Banner errori hardware + Stato Batteria AXP2101 (Quadrante Superiore, y = 42..98)
  bool hw_ok = (snap.error_flags == 0) &&
               (snap.error_summary[0] == '\0' || strcmp(snap.error_summary, "OK") == 0);
  char err_buf[44];
  if (hw_ok) {
    snprintf(err_buf, sizeof(err_buf), "DRAGON RADAR | HW: OK");
  } else {
    snprintf(err_buf, sizeof(err_buf), "HW ERR: %.28s", snap.error_summary);
  }
  drawBadge(d,
            kCenterX - 105,
            40,
            210,
            18,
            err_buf,
            hw_ok ? RADAR_COLOR_GRID_BRIGHT : RADAR_COLOR_RED,
            hw_ok ? 0x0180 : 0x4000,
            hw_ok ? RADAR_COLOR_WHITE : RADAR_COLOR_YELLOW,
            1);

  // Riga stato alimentazione / batteria AXP2101 (y = 62, larghezza 260 px, max_dx=148)
  char pwr_buf[48];
  const char *chg_tag = snap.is_charging ? "CHG" : (snap.is_discharging ? "DSC" : "STB");
  if (!snap.pmic_online) {
    snprintf(pwr_buf, sizeof(pwr_buf), "PMIC: OFFLINE (0x34)");
  } else if (snap.battery_present && snap.batt_percent >= 0) {
    snprintf(pwr_buf,
             sizeof(pwr_buf),
             "%s %umV | BAT:%umV %d%% %s",
             snap.vbus_present ? "USB" : "NO-USB",
             (unsigned)snap.vbus_mv,
             (unsigned)snap.batt_mv,
             (int)snap.batt_percent,
             chg_tag);
  } else {
    snprintf(pwr_buf,
             sizeof(pwr_buf),
             "%s:%umV VSYS:%umV BAT:NONE",
             snap.vbus_present ? "USB" : "VBUS",
             (unsigned)snap.vbus_mv,
             (unsigned)snap.vsys_mv);
  }
  drawBadge(d,
            kCenterX - 132,
            62,
            264,
            18,
            pwr_buf,
            snap.pmic_online ? RADAR_COLOR_CYAN : RADAR_COLOR_RED,
            0x0842,
            RADAR_COLOR_CYAN,
            1);

  // 5. Freccia dinamica della bussola al centro (233, 233)
  float hdg = normalizeHeading(snap.heading_deg);
  // L'angolo della freccia che indica il Nord magnetico rispetto alla cima del radar:
  float arrow_rad = (-hdg - 90.0f) * kDegToRad;
  float cs = cosf(arrow_rad);
  float sn = sinf(arrow_rad);
  float px = -sn;
  float py = cs;

  const float tip_len = 86.0f;
  const float tail_len = 62.0f;
  const float half_w = 13.0f;

  int16_t nx = kCenterX + (int16_t)roundf(cs * tip_len);
  int16_t ny = kCenterY + (int16_t)roundf(sn * tip_len);
  int16_t sx = kCenterX - (int16_t)roundf(cs * tail_len);
  int16_t sy = kCenterY - (int16_t)roundf(sn * tail_len);
  int16_t lx = kCenterX + (int16_t)roundf(px * half_w);
  int16_t ly = kCenterY + (int16_t)roundf(py * half_w);
  int16_t rx = kCenterX - (int16_t)roundf(px * half_w);
  int16_t ry = kCenterY - (int16_t)roundf(py * half_w);

  // Coda Sud (verde/ciano scuro) e Punta Nord (rosso/arancio Dragon Radar)
  d->fillTriangle(sx, sy, lx, ly, rx, ry, 0x03EF);
  d->drawTriangle(sx, sy, lx, ly, rx, ry, RADAR_COLOR_WHITE);
  d->fillTriangle(nx, ny, lx, ly, rx, ry, snap.bmm_online ? RADAR_COLOR_RED : RADAR_COLOR_DARKGRAY);
  d->drawTriangle(nx, ny, lx, ly, rx, ry, RADAR_COLOR_YELLOW);

  // Mirino centrale Dragon Ball (ambra + punto rosso)
  d->fillCircle(kCenterX, kCenterY, 9, RADAR_COLOR_AMBER);
  d->drawCircle(kCenterX, kCenterY, 10, RADAR_COLOR_WHITE);
  d->fillCircle(kCenterX, kCenterY, 3, RADAR_COLOR_RED);

  // 6. Indicatore numerico grande Heading (0-359°) + Badge [CAL]/[RAW] e [TILT] (y = 88..134)
  char hdg_str[20];
  if (snap.bmm_online) {
    snprintf(hdg_str, sizeof(hdg_str), "%03d deg %s", (int)roundf(hdg) % 360, headingToCardinal(hdg));
  } else {
    snprintf(hdg_str, sizeof(hdg_str), "--- deg --");
  }
  drawBadge(d,
            kCenterX - 88,
            86,
            176,
            26,
            hdg_str,
            RADAR_COLOR_AMBER,
            0x1082,
            RADAR_COLOR_YELLOW,
            2);

  // Badge [CAL]/[RAW] a sinistra e [TILT] a destra del box heading
  const char *cal_lbl = !snap.bmm_online ? "[NO MAG]" : (snap.mag_calibrated ? "[CAL]" : "[RAW]");
  uint16_t cal_col = !snap.bmm_online ? RADAR_COLOR_RED
                                      : (snap.mag_calibrated ? RADAR_COLOR_GRID_BRIGHT : RADAR_COLOR_ORANGE);
  drawBadge(d, kCenterX - 150, 90, 58, 18, cal_lbl, cal_col, 0x0841, cal_col, 1);

  const char *tilt_lbl = snap.tilt_compensated ? "[TILT]" : (snap.imu_online ? "[IMU]" : "[NO IMU]");
  uint16_t tilt_col = snap.tilt_compensated ? RADAR_COLOR_CYAN
                                            : (snap.imu_online ? RADAR_COLOR_YELLOW : RADAR_COLOR_RED);
  drawBadge(d, kCenterX + 92, 90, 58, 18, tilt_lbl, tilt_col, 0x0841, tilt_col, 1);

  // 7. Indicatori quadrante inferiore (y = 318..398, ben dentro il cerchio!)
  // Riga GPS (y = 320, larghezza 310 px, max_dx(320)=204 px):
  uint16_t gps_col = resolveGpsStatusColor(snap.gps_status);
  char gps_line[52];
  if (snap.gps_hdop < 50.0f) {
    snprintf(gps_line,
             sizeof(gps_line),
             "GPS:%s | SAT:%02u/%02u HDOP:%.1f",
             resolveGpsStatusShort(snap.gps_status),
             (unsigned)snap.gps_sats_in_use,
             (unsigned)snap.gps_sats_in_view,
             (double)safeFloat(snap.gps_hdop, 99.9f));
  } else {
    snprintf(gps_line,
             sizeof(gps_line),
             "GPS:%s | SAT:%02u/%02u RX:%luB",
             resolveGpsStatusShort(snap.gps_status),
             (unsigned)snap.gps_sats_in_use,
             (unsigned)snap.gps_sats_in_view,
             (unsigned long)snap.gps_bytes_rx);
  }
  drawBadge(d, kCenterX - 155, 320, 310, 20, gps_line, gps_col, 0x0841, gps_col, 1);

  // Riga Pulsante GPIO44 + Motorino GPIO16 (y = 346, larghezza 310 px, max_dx(346)=191 px)
  char btn_line[28];
  snprintf(btn_line,
           sizeof(btn_line),
           "BTN44:%s #%lu",
           resolveButtonStateStr(snap),
           (unsigned long)snap.btn_press_count);
  uint16_t btn_col = snap.btn_pressed ? RADAR_COLOR_YELLOW : RADAR_COLOR_GRID_BRIGHT;
  drawBadge(d,
            kCenterX - 155,
            346,
            158,
            20,
            btn_line,
            btn_col,
            snap.btn_pressed ? 0x3180 : 0x0841,
            btn_col,
            1);

  bool mot_on = snap.motor_active || snap.motor_pattern_running;
  char mot_line[28];
  snprintf(mot_line,
           sizeof(mot_line),
           "MOT16:%s #%lu",
           mot_on ? "ON!" : "OFF",
           (unsigned long)snap.motor_activations);
  uint16_t mot_col = mot_on ? RADAR_COLOR_ORANGE : RADAR_COLOR_CYAN;
  drawBadge(d,
            kCenterX + 7,
            346,
            148,
            20,
            mot_line,
            mot_col,
            mot_on ? 0x4100 : 0x0841,
            mot_col,
            1);

  // Riga sintetica Campo Magnetico |B| + Roll/Pitch IMU (y = 372, larghezza 270 px, max_dx(372)=173 px)
  char mag_line[48];
  snprintf(mag_line,
           sizeof(mag_line),
           "|B|:%.1fuT | R:%+.0f P:%+.0f deg",
           (double)safeFloat(snap.mag_b_ut),
           (double)safeFloat(snap.roll_deg),
           (double)safeFloat(snap.pitch_deg));
  drawBadge(d, kCenterX - 135, 372, 270, 18, mag_line, RADAR_COLOR_GRID_MID, 0x0120, RADAR_COLOR_WHITE, 1);
}

// ============================================================================
// PAGINA 1: DIAGNOSTICA BUS I2C, TCA9554, PMIC AXP2101 & RTC PCF85063
// ============================================================================
void renderPage1I2cPmic(Arduino_GFX *d, const RadarSystemSnapshot &snap) {
  drawBadge(d, kCenterX - 110, 34, 220, 20, "P1: I2C BUS & PMIC AXP2101", RADAR_COLOR_CYAN, 0x0842, RADAR_COLOR_CYAN, 1);

  // Griglia 3x3 degli indirizzi I2C attesi (y = 60..146, x = 68..398, ben dentro il cerchio!)
  struct I2cCell {
    const char *label;
    bool ok;
    bool partial;
  };

  const bool bmm_ok = snap.i2c_present[HW_ADDR_BMM150] || snap.i2c_present[HW_ADDR_BMM150_ALT] || snap.bmm_online;
  const bool gps_w = snap.i2c_present[HW_ADDR_LC76G_W];
  const bool gps_r = snap.i2c_present[HW_ADDR_LC76G_R];
  const bool gps_any = gps_w || gps_r || snap.gps_i2c_online;
  const bool gps_both = (gps_w && gps_r) || snap.gps_i2c_online;

  const I2cCell cells[9] = {
      {snap.i2c_present[HW_ADDR_BMM150_ALT] ? "0x11 BMM150" : "0x13 BMM150", bmm_ok, false},
      {"0x18 ES8311", snap.i2c_present[HW_ADDR_ES8311], false},
      {"0x20 TCA9554", snap.i2c_present[HW_ADDR_TCA9554] || snap.tca_online, false},
      {"0x34 AXP2101", snap.i2c_present[HW_ADDR_AXP2101] || snap.pmic_online, false},
      {"0x40 ES7210", snap.i2c_present[HW_ADDR_ES7210], false},
      {"0x50/54 GPS", gps_both, gps_any && !gps_both},
      {"0x51 PCF85063", snap.i2c_present[HW_ADDR_PCF85063] || snap.rtc_online, false},
      {"0x5A CST9217", snap.i2c_present[HW_ADDR_CST9217] || snap.touch_online || s_touch_online, false},
      {"0x6B QMI8658", snap.i2c_present[HW_ADDR_QMI8658] || snap.imu_online, false},
  };

  for (int idx = 0; idx < 9; ++idx) {
    int row = idx / 3;
    int col = idx % 3;
    int16_t cx = 71 + col * 110;
    int16_t cy = 60 + row * 28;
    uint16_t brd = cells[idx].ok ? RADAR_COLOR_GRID_BRIGHT
                                 : (cells[idx].partial ? RADAR_COLOR_YELLOW : RADAR_COLOR_RED);
    uint16_t bg = cells[idx].ok ? 0x0200 : (cells[idx].partial ? 0x3180 : 0x3000);
    uint16_t fg = cells[idx].ok ? RADAR_COLOR_WHITE
                                 : (cells[idx].partial ? RADAR_COLOR_YELLOW : RADAR_COLOR_ORANGE);
    drawBadge(d, cx, cy, 104, 24, cells[idx].label, brd, bg, fg, 1);
  }

  // Contatori bus I2C (y = 148)
  char bus_line[56];
  snprintf(bus_line,
           sizeof(bus_line),
           "I2C SDA15/SCL14 100k | Dev:%u TX:%lu Err:%lu",
           (unsigned)snap.i2c_found_count,
           (unsigned long)snap.i2c_tx_count,
           (unsigned long)snap.i2c_err_count);
  drawCenteredText(d, 150, bus_line, snap.i2c_err_count == 0 ? RADAR_COLOR_GRID_BRIGHT : RADAR_COLOR_ORANGE, 1);

  // Sezione PMIC AXP2101 (y = 166..296, x = 44..422)
  d->drawRoundRect(44, 166, 378, 132, 6, RADAR_COLOR_GRID_MID);
  char line[60];

  snprintf(line,
           sizeof(line),
           "AXP2101 (0x34): %s | ChipID:0x%02X | T:%.1fC",
           snap.pmic_online ? "ONLINE" : "OFFLINE",
           (unsigned)snap.pmic_chip_id,
           (double)safeFloat(snap.pmic_temp_c));
  drawCenteredText(d, 174, line, snap.pmic_online ? RADAR_COLOR_YELLOW : RADAR_COLOR_RED, 1);

  snprintf(line,
           sizeof(line),
           "VBUS: %4u mV (%s) | VSYS: %4u mV",
           (unsigned)snap.vbus_mv,
           snap.vbus_present ? (snap.vbus_good ? "GOOD" : "IN") : "NONE",
           (unsigned)snap.vsys_mv);
  drawCenteredText(d, 194, line, RADAR_COLOR_WHITE, 1);

  if (snap.battery_present && snap.batt_percent >= 0) {
    snprintf(line,
             sizeof(line),
             "VBAT: %4u mV (%3d%%) | State: %s",
             (unsigned)snap.batt_mv,
             (int)snap.batt_percent,
             snap.charger_status_str);
  } else {
    snprintf(line,
             sizeof(line),
             "VBAT: %4u mV (NO BAT) | State: %s",
             (unsigned)snap.batt_mv,
             snap.charger_status_str);
  }
  drawCenteredText(d, 214, line, RADAR_COLOR_CYAN, 1);

  snprintf(line,
           sizeof(line),
           "DC1:%umV DC3:%umV ALDO1:%u ALDO2:%u",
           (unsigned)snap.dc1_mv,
           (unsigned)snap.dc3_mv,
           (unsigned)snap.aldo1_mv,
           (unsigned)snap.aldo2_mv);
  drawCenteredText(d, 236, line, RADAR_COLOR_GRAY, 1);

  snprintf(line,
           sizeof(line),
           "ALDO3:%umV ALDO4:%umV BLDO1:%u BLDO2:%u",
           (unsigned)snap.aldo3_mv,
           (unsigned)snap.aldo4_mv,
           (unsigned)snap.bldo1_mv,
           (unsigned)snap.bldo2_mv);
  drawCenteredText(d, 254, line, RADAR_COLOR_GRAY, 1);

  snprintf(line,
           sizeof(line),
           "Motor VSYS min:%umV (drop:%dmV) I2C dErr:%lu",
           (unsigned)snap.motor_min_vsys_mv,
           (int)snap.motor_max_drop_mv,
           (unsigned long)snap.motor_i2c_err_delta);
  drawCenteredText(d, 274, line, snap.motor_power_anomaly ? RADAR_COLOR_RED : RADAR_COLOR_GRID_BRIGHT, 1);

  // Sezione TCA9554 (0x20) + RTC PCF85063 (0x51) + Touch (y = 306..402, x = 72..394)
  d->drawRoundRect(72, 306, 322, 94, 6, RADAR_COLOR_GRID_DARK);
  snprintf(line,
           sizeof(line),
           "TCA9554(0x20): %s IN:0x%02X OUT:0x%02X CFG:0x%02X",
           snap.tca_online ? "OK" : "ERR",
           (unsigned)snap.tca_inputs,
           (unsigned)snap.tca_outputs,
           (unsigned)snap.tca_config);
  drawCenteredText(d, 316, line, snap.tca_online ? RADAR_COLOR_WHITE : RADAR_COLOR_RED, 1);

  int pwr_btn = (snap.tca_inputs >> HW_EXIO_PWR_BTN) & 1;
  int gps_rst = (snap.tca_outputs >> HW_EXIO_GPS_RST) & 1;
  snprintf(line,
           sizeof(line),
           "EXIO4(PWR):%d EXIO7(GPS_RST):%d | BOOT(IO0):%d",
           pwr_btn,
           gps_rst,
           snap.boot_btn_pressed ? 1 : 0);
  drawCenteredText(d, 338, line, RADAR_COLOR_YELLOW, 1);

  snprintf(line,
           sizeof(line),
           "RTC(0x51): %s %s",
           snap.rtc_online ? "OK" : "N/A",
           snap.rtc_datetime[0] ? snap.rtc_datetime : "--");
  drawCenteredText(d, 360, line, RADAR_COLOR_CYAN, 1);

  snprintf(line,
           sizeof(line),
           "Touch(0x5A): %s (%d,%d) Taps:%lu",
           (snap.touch_online || s_touch_online) ? "CST9217 OK" : "OFFLINE",
           (int)snap.touch_x,
           (int)snap.touch_y,
           (unsigned long)snap.touch_tap_count);
  drawCenteredText(d, 380, line, RADAR_COLOR_GRID_BRIGHT, 1);
}

// ============================================================================
// PAGINA 2: DIAGNOSTICA BUSSOLA BMM150, IMU QMI8658 & INTERFERENZA MOTORINO
// ============================================================================
void renderPage2SensorsImu(Arduino_GFX *d, const RadarSystemSnapshot &snap) {
  drawBadge(d, kCenterX - 118, 34, 236, 20, "P2: BMM150, QMI8658 & MOTOR", RADAR_COLOR_AMBER, 0x1082, RADAR_COLOR_YELLOW, 1);

  char line[60];

  // Box 1: Bussola BMM150 (y = 60..190, x = 58..408)
  d->drawRoundRect(58, 60, 350, 128, 6, snap.bmm_online ? RADAR_COLOR_GRID_BRIGHT : RADAR_COLOR_RED);
  snprintf(line,
           sizeof(line),
           "BMM150 (0x%02X): %s | ID:0x%02X | %.1f Hz",
           (unsigned)snap.bmm_addr,
           snap.bmm_online ? "ONLINE" : "OFFLINE",
           (unsigned)snap.bmm_chip_id,
           (double)safeFloat(snap.mag_rate_hz));
  drawCenteredText(d, 68, line, snap.bmm_online ? RADAR_COLOR_GRID_BRIGHT : RADAR_COLOR_RED, 1);

  snprintf(line,
           sizeof(line),
           "X:%+6.1f uT   Y:%+6.1f uT   Z:%+6.1f uT",
           (double)safeFloat(snap.mag_x_ut),
           (double)safeFloat(snap.mag_y_ut),
           (double)safeFloat(snap.mag_z_ut));
  drawCenteredText(d, 88, line, RADAR_COLOR_WHITE, 1);

  snprintf(line,
           sizeof(line),
           "|B|: %5.1f uT | Noise: %.2f uT | %s",
           (double)safeFloat(snap.mag_b_ut),
           (double)safeFloat(snap.mag_noise_ut),
           snap.mag_calibrated ? "[CAL]" : "[RAW]");
  drawCenteredText(d, 108, line, RADAR_COLOR_CYAN, 1);

  snprintf(line,
           sizeof(line),
           "Hdg RAW:%5.1f | CAL:%5.1f | TILT:%5.1f deg",
           (double)normalizeHeading(snap.heading_raw_deg),
           (double)normalizeHeading(snap.heading_cal_deg),
           (double)normalizeHeading(snap.heading_tilt_deg));
  drawCenteredText(d, 128, line, RADAR_COLOR_YELLOW, 1);

  snprintf(line,
           sizeof(line),
           "Off: X%+5.1f Y%+5.1f Z%+5.1f | Scl:%.2f/%.2f/%.2f",
           (double)safeFloat(snap.cal_offset_x),
           (double)safeFloat(snap.cal_offset_y),
           (double)safeFloat(snap.cal_offset_z),
           (double)safeFloat(snap.cal_scale_x, 1.0f),
           (double)safeFloat(snap.cal_scale_y, 1.0f),
           (double)safeFloat(snap.cal_scale_z, 1.0f));
  drawCenteredText(d, 148, line, RADAR_COLOR_GRAY, 1);

  snprintf(line,
           sizeof(line),
           "Active Heading: %03d deg (%s)",
           (int)roundf(normalizeHeading(snap.heading_deg)) % 360,
           headingToCardinal(snap.heading_deg));
  drawCenteredText(d, 168, line, RADAR_COLOR_AMBER, 1);

  // Box 2: IMU 6 assi QMI8658 (y = 194..288, x = 46..420)
  d->drawRoundRect(46, 194, 374, 92, 6, snap.imu_online ? RADAR_COLOR_CYAN : RADAR_COLOR_RED);
  snprintf(line,
           sizeof(line),
           "QMI8658 IMU (0x6B): %s | TiltComp: %s",
           snap.imu_online ? "ONLINE" : "OFFLINE",
           snap.tilt_compensated ? "ON" : "OFF");
  drawCenteredText(d, 202, line, snap.imu_online ? RADAR_COLOR_CYAN : RADAR_COLOR_RED, 1);

  snprintf(line,
           sizeof(line),
           "Roll: %+6.1f deg    Pitch: %+6.1f deg",
           (double)safeFloat(snap.roll_deg),
           (double)safeFloat(snap.pitch_deg));
  drawCenteredText(d, 222, line, RADAR_COLOR_YELLOW, 1);

  snprintf(line,
           sizeof(line),
           "Acc(g):  X:%+5.2f   Y:%+5.2f   Z:%+5.2f",
           (double)safeFloat(snap.acc_x_g),
           (double)safeFloat(snap.acc_y_g),
           (double)safeFloat(snap.acc_z_g));
  drawCenteredText(d, 244, line, RADAR_COLOR_WHITE, 1);

  snprintf(line,
           sizeof(line),
           "Gyr(d/s): X:%+5.1f  Y:%+5.1f   Z:%+5.1f",
           (double)safeFloat(snap.gyr_x_dps),
           (double)safeFloat(snap.gyr_y_dps),
           (double)safeFloat(snap.gyr_z_dps));
  drawCenteredText(d, 264, line, RADAR_COLOR_GRAY, 1);

  // Box 3: Report Interferenza Magnetica Motorino (TEST 6, y = 294..402, x = 72..394)
  d->drawRoundRect(72, 294, 322, 106, 6, RADAR_COLOR_ORANGE);
  snprintf(line,
           sizeof(line),
           "TEST 6 MOTOR INTERFERENCE (%s)",
           snap.interference_tested ? snap.interf_distance_label : "PENDING");
  drawCenteredText(d, 304, line, RADAR_COLOR_ORANGE, 1);

  snprintf(line,
           sizeof(line),
           "Base |B|: %.1f uT -> Motor ON |B|: %.1f uT",
           (double)safeFloat(snap.interf_base_b_ut),
           (double)safeFloat(snap.interf_on_b_ut));
  drawCenteredText(d, 326, line, RADAR_COLOR_WHITE, 1);

  snprintf(line,
           sizeof(line),
           "Max d|B|: %.1f uT  | Max dHdg: %.1f deg",
           (double)safeFloat(snap.interf_delta_b_ut),
           (double)safeFloat(snap.interf_delta_heading_deg));
  uint16_t ic = (snap.interf_delta_heading_deg > 5.0f) ? RADAR_COLOR_YELLOW : RADAR_COLOR_GRID_BRIGHT;
  drawCenteredText(d, 348, line, ic, 1);

  snprintf(line,
           sizeof(line),
           "Post-OFF d|B|: %.2f uT | Mot:%s",
           (double)safeFloat(snap.interf_post_delta_b_ut),
           snap.motor_active ? "ACTIVE" : "OFF");
  drawCenteredText(d, 370, line, RADAR_COLOR_CYAN, 1);
}

// ============================================================================
// PAGINA 3: DIAGNOSTICA GNSS QUECTEL LC76G (0x50/0x54 & NMEA)
// ============================================================================
void renderPage3GnssLc76g(Arduino_GFX *d, const RadarSystemSnapshot &snap) {
  drawBadge(d, kCenterX - 112, 34, 224, 20, "P3: GNSS QUECTEL LC76G", RADAR_COLOR_GRID_BRIGHT, 0x0180, RADAR_COLOR_WHITE, 1);

  // Stato a 4 livelli chiaramente distinto (y = 60..88)
  const char *state_desc = "0: UNREACHABLE (0x50/0x54 NACK)";
  if (snap.gps_status == RADAR_GPS_NO_DATA) {
    state_desc = "1: NO DATA (I2C ACK, 0 BYTES)";
  } else if (snap.gps_status == RADAR_GPS_NMEA_NO_FIX) {
    state_desc = "2: NMEA RX - NO FIX (SEARCHING)";
  } else if (snap.gps_status == RADAR_GPS_VALID_FIX) {
    state_desc = "3: VALID 3D SATELLITE FIX";
  }
  uint16_t scol = resolveGpsStatusColor(snap.gps_status);
  drawBadge(d, kCenterX - 142, 62, 284, 24, state_desc, scol, 0x0841, scol, 1);

  char line[60];

  // Box Protocollo I2C 0x50/0x54 & Parser NMEA (y = 94..184, x = 52..414)
  d->drawRoundRect(52, 94, 362, 88, 6, RADAR_COLOR_GRID_MID);
  snprintf(line,
           sizeof(line),
           "I2C 0x50/0x54: %s | OK:%lu FAIL:%lu",
           snap.gps_i2c_online ? "ONLINE" : "OFFLINE",
           (unsigned long)snap.gps_i2c_ok,
           (unsigned long)snap.gps_i2c_fail);
  drawCenteredText(d, 104, line, snap.gps_i2c_online ? RADAR_COLOR_GRID_BRIGHT : RADAR_COLOR_RED, 1);

  snprintf(line,
           sizeof(line),
           "NMEA Bytes RX: %lu | Valid:%lu CsErr:%lu",
           (unsigned long)snap.gps_bytes_rx,
           (unsigned long)snap.gps_nmea_valid,
           (unsigned long)snap.gps_nmea_cs_err);
  drawCenteredText(d, 126, line, RADAR_COLOR_WHITE, 1);

  snprintf(line,
           sizeof(line),
           "Sats InUse: %2u | InView: %2u | FixQual: %u",
           (unsigned)snap.gps_sats_in_use,
           (unsigned)snap.gps_sats_in_view,
           (unsigned)snap.gps_fix_quality);
  drawCenteredText(d, 148, line, RADAR_COLOR_CYAN, 1);

  // Box Coordinate, Altitudine, HDOP, UTC & Fix Age (y = 190..316, x = 46..420)
  d->drawRoundRect(46, 190, 374, 124, 6, scol);
  if (snap.gps_fix_valid && snap.gps_status == RADAR_GPS_VALID_FIX) {
    snprintf(line, sizeof(line), "LAT: %+11.6f deg", snap.gps_lat);
    drawCenteredText(d, 200, line, RADAR_COLOR_GRID_BRIGHT, 1);
    snprintf(line, sizeof(line), "LON: %+11.6f deg", snap.gps_lon);
    drawCenteredText(d, 220, line, RADAR_COLOR_GRID_BRIGHT, 1);
  } else {
    drawCenteredText(d, 200, "LAT:   ---.------ deg (NO VALID FIX)", RADAR_COLOR_YELLOW, 1);
    drawCenteredText(d, 220, "LON:   ---.------ deg (NO VALID FIX)", RADAR_COLOR_YELLOW, 1);
  }

  snprintf(line,
           sizeof(line),
           "ALT: %6.1f m   |   HDOP: %5.2f",
           (double)safeFloat(snap.gps_alt_m),
           (double)safeFloat(snap.gps_hdop, 99.9f));
  drawCenteredText(d, 244, line, RADAR_COLOR_WHITE, 1);

  snprintf(line,
           sizeof(line),
           "UTC: %.12s  %.10s",
           snap.gps_utc_date[0] ? snap.gps_utc_date : "----/--/--",
           snap.gps_utc_time[0] ? snap.gps_utc_time : "--:--:--");
  drawCenteredText(d, 266, line, RADAR_COLOR_CYAN, 1);

  if (snap.gps_fix_valid) {
    snprintf(line, sizeof(line), "Last Valid Fix Age: %lu ms", (unsigned long)snap.gps_fix_age_ms);
  } else {
    snprintf(line, sizeof(line), "Last Valid Fix Age: N/A (Waiting sky view)");
  }
  drawCenteredText(d, 288, line, RADAR_COLOR_GRAY, 1);

  // Box Ultime frasi NMEA ricevute (y = 322..402, x = 74..392)
  d->drawRoundRect(74, 322, 318, 78, 6, RADAR_COLOR_GRID_DARK);
  drawCenteredText(d, 330, "LAST NMEA SENTENCES (0x54):", RADAR_COLOR_AMBER, 1);

  char nmea0[46] = "--";
  char nmea1[46] = "--";
  if (snap.gps_last_nmea[0][0] != '\0') {
    snprintf(nmea0, sizeof(nmea0), "%.42s", snap.gps_last_nmea[0]);
  }
  if (snap.gps_last_nmea[1][0] != '\0') {
    snprintf(nmea1, sizeof(nmea1), "%.42s", snap.gps_last_nmea[1]);
  }
  drawCenteredText(d, 352, nmea0, RADAR_COLOR_GRID_BRIGHT, 1);
  drawCenteredText(d, 372, nmea1, RADAR_COLOR_GRID_BRIGHT, 1);
}

// ============================================================================
// OVERLAY MODALITÀ CALIBRAZIONE BUSSOLA (Min/Max X,Y,Z + Istruzioni Figura a 8)
// ============================================================================
void renderCalibrationOverlay(Arduino_GFX *d, const RadarSystemSnapshot &snap) {
  drawBadge(d,
            kCenterX - 116,
            36,
            232,
            22,
            "COMPASS CALIBRATION MODE",
            RADAR_COLOR_AMBER,
            0x3180,
            RADAR_COLOR_YELLOW,
            1);

  // Disegna simbolo grafico "Figura a 8" (∞) al centro superiore (y = 84)
  d->drawCircle(kCenterX - 24, 84, 18, RADAR_COLOR_CYAN);
  d->drawCircle(kCenterX - 24, 84, 17, RADAR_COLOR_CYAN);
  d->drawCircle(kCenterX + 24, 84, 18, RADAR_COLOR_CYAN);
  d->drawCircle(kCenterX + 24, 84, 17, RADAR_COLOR_CYAN);
  // Pallino animato lungo l'8 in base a uptime_ms
  float t = (float)(snap.uptime_ms % 2400) / 2400.0f * 6.2831853f;
  int16_t px = kCenterX + (int16_t)roundf(38.0f * sinf(t));
  int16_t py = 84 + (int16_t)roundf(15.0f * sinf(2.0f * t));
  d->fillCircle(px, py, 4, RADAR_COLOR_YELLOW);

  drawCenteredText(d, 112, "Ruotare il radar a 8 su tutti gli assi!", RADAR_COLOR_WHITE, 1);
  drawCenteredText(d, 128, "(Hard-Iron + Soft-Iron Min/Max Capture)", RADAR_COLOR_GRAY, 1);

  // Tabella real-time Min / Cur / Max / Span per gli assi X, Y, Z (y = 148..304, x = 48..418)
  d->drawRoundRect(48, 148, 370, 158, 6, RADAR_COLOR_GRID_BRIGHT);
  drawCenteredText(d, 158, "AXIS    MIN(uT)   CUR(uT)   MAX(uT)   SPAN", RADAR_COLOR_AMBER, 1);

  const char axes[3] = {'X', 'Y', 'Z'};
  const float mins[3] = {safeFloat(snap.cal_min_x), safeFloat(snap.cal_min_y), safeFloat(snap.cal_min_z)};
  const float curs[3] = {safeFloat(snap.mag_x_ut), safeFloat(snap.mag_y_ut), safeFloat(snap.mag_z_ut)};
  const float maxs[3] = {safeFloat(snap.cal_max_x), safeFloat(snap.cal_max_y), safeFloat(snap.cal_max_z)};

  for (int i = 0; i < 3; ++i) {
    int16_t ry = 180 + i * 40;
    float span = maxs[i] - mins[i];
    if (span < 0.0f) {
      span = 0.0f;
    }
    char row[56];
    snprintf(row,
             sizeof(row),
             " %c:    %+6.1f    %+6.1f    %+6.1f   %5.1f",
             axes[i],
             (double)mins[i],
             (double)curs[i],
             (double)maxs[i],
             (double)span);
    drawCenteredText(d, ry, row, RADAR_COLOR_WHITE, 1);

    // Barra progresso escursione magnetica (target >= 60 uT per buon fit sferico)
    int16_t bar_x = 84;
    int16_t bar_y = ry + 14;
    int16_t bar_w = 298;
    d->drawRect(bar_x, bar_y, bar_w, 10, RADAR_COLOR_GRID_MID);
    float ratio = span / 70.0f;
    if (ratio > 1.0f) {
      ratio = 1.0f;
    }
    int16_t fill_w = (int16_t)roundf(ratio * (float)(bar_w - 2));
    uint16_t bcol = (span >= 45.0f) ? RADAR_COLOR_GRID_BRIGHT
                                    : ((span >= 20.0f) ? RADAR_COLOR_YELLOW : RADAR_COLOR_ORANGE);
    if (fill_w > 0) {
      d->fillRect(bar_x + 1, bar_y + 1, fill_w, 8, bcol);
    }
  }

  char stats[56];
  snprintf(stats,
           sizeof(stats),
           "Campioni: %lu | Tempo: %.1f s | Hdg: %03d deg",
           (unsigned long)snap.cal_samples,
           (double)snap.cal_elapsed_ms / 1000.0,
           (int)roundf(normalizeHeading(snap.heading_deg)) % 360);
  drawCenteredText(d, 322, stats, RADAR_COLOR_CYAN, 1);

  drawBadge(d,
            kCenterX - 148,
            348,
            296,
            24,
            "Long-Press GPIO44 o 'cal_stop' = SALVA NVS",
            RADAR_COLOR_GRID_BRIGHT,
            0x0200,
            RADAR_COLOR_WHITE,
            1);
}

} // namespace

// ============================================================================
// IMPLEMENTAZIONE API PUBBLICA
// ============================================================================

bool displayInit(bool init_touch) {
  if (!s_display_initialized) {
    // Costruttore ufficiale Waveshare 01_HelloWorld.ino (466x466, col_offset1 = 6)
    s_bus = new Arduino_ESP32QSPI(
        HW_LCD_CS, HW_LCD_SCLK, HW_LCD_SDIO0, HW_LCD_SDIO1, HW_LCD_SDIO2, HW_LCD_SDIO3);
    s_gfx = new Arduino_CO5300(
        s_bus, HW_LCD_RESET, 0 /* rotation */, HW_LCD_WIDTH /* 466 */, HW_LCD_HEIGHT /* 466 */,
        HW_LCD_COL_OFFSET1 /* 6 */, 0, 0, 0);

    if (!s_gfx->begin()) {
      return false;
    }

    s_gfx->fillScreen(RADAR_COLOR_BG);
    s_gfx->setBrightness(s_brightness);

    // Allocazione framebuffer 466x466 RGB565 in PSRAM OPI per aggiornamenti zero-flicker
    s_canvas = new PsramCanvas(HW_LCD_WIDTH, HW_LCD_HEIGHT, s_gfx, 0, 0, 0);
    if (s_canvas->allocatePsramBuffer() && s_canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) {
      s_canvas_ready = true;
      s_canvas->fillScreen(RADAR_COLOR_BG);
      flushDisplay();
    } else {
      delete s_canvas;
      s_canvas = nullptr;
      s_canvas_ready = false;
    }

    s_display_initialized = true;
  }

  if (init_touch && !s_touch_online) {
    if (!i2cBusIsInitialized()) {
      i2cBusInit();
    }

    pinMode(HW_TP_RESET, OUTPUT);
    pinMode(HW_TP_INT, INPUT_PULLUP);

    if (i2cBusLock(300)) {
      // Impulso di reset hardware su TP_RESET (GPIO40, separato da LCD_RESET GPIO39)
      digitalWrite(HW_TP_RESET, LOW);
      delay(20);
      digitalWrite(HW_TP_RESET, HIGH);
      delay(50);

      s_touch.setPins(HW_TP_RESET, HW_TP_INT);
      s_touch.setGpioCallback(touchPinModeCb, touchDigitalWriteCb, touchDigitalReadCb);

      // Inizializzazione tramite callback custom su i2c_bus (evita chiamate ridondanti a Wire.begin)
      bool ok = s_touch.begin(touchI2cCallback, touchHalCallback, HW_ADDR_CST9217);
      if (!ok) {
        // Fallback diretto su Wire mantenendo il clock a 100 kHz
        ok = s_touch.begin(Wire, HW_ADDR_CST9217, HW_I2C_SDA, HW_I2C_SCL);
        Wire.setClock(HW_I2C_FREQ_HZ);
        Wire.setTimeOut(50);
      }

      if (ok) {
        s_touch.setMaxCoordinates(HW_LCD_WIDTH, HW_LCD_HEIGHT);
        s_touch.setMirrorXY(true, true);
        attachInterrupt(digitalPinToInterrupt(HW_TP_INT), onTouchInterrupt, FALLING);
        s_touch_online = true;
      }
      i2cBusUnlock();
    }
  }

  return s_display_initialized;
}

void displaySetBrightness(uint8_t level) {
  s_brightness = level;
  if (s_gfx != nullptr && s_display_initialized) {
    s_gfx->setBrightness(level);
  }
}

bool displayRunColorPatternTest(uint32_t step_ms) {
  if (!s_display_initialized && !displayInit(false)) {
    return false;
  }

  Arduino_GFX *d = getDrawTarget();
  struct ColorStep {
    const char *name;
    uint16_t bg;
    uint16_t fg;
    uint16_t ring;
  };

  static const ColorStep kSteps[5] = {
      {"RED (0xF800)", 0xF800, 0xFFFF, 0xFFE0},
      {"GREEN (0x07E0)", 0x07E0, 0x0000, 0x0000},
      {"BLUE (0x001F)", 0x001F, 0xFFFF, 0x07FF},
      {"WHITE (0xFFFF)", 0xFFFF, 0x0000, 0xF800},
      {"BLACK + GEOMETRY", 0x0000, 0x07E0, 0x07E0},
  };

  displaySetBrightness(220);

  for (uint8_t i = 0; i < 5; ++i) {
    d->fillScreen(kSteps[i].bg);
    d->drawCircle(kCenterX, kCenterY, 232, kSteps[i].ring);
    d->drawCircle(kCenterX, kCenterY, 228, kSteps[i].ring);
    d->drawCircle(kCenterX, kCenterY, 150, kSteps[i].ring);

    if (i == 4) {
      // Nel passo NERO disegna la geometria completa di allineamento 466x466
      d->drawCircle(kCenterX, kCenterY, 200, RADAR_COLOR_GRID_MID);
      d->drawCircle(kCenterX, kCenterY, 100, RADAR_COLOR_GRID_MID);
      d->drawCircle(kCenterX, kCenterY, 50, RADAR_COLOR_AMBER);
      d->drawFastHLine(kCenterX - 228, kCenterY, 456, RADAR_COLOR_GRID_MID);
      d->drawFastVLine(kCenterX, kCenterY - 228, 456, RADAR_COLOR_GRID_MID);

      // Barre colore RGBCMYW
      const uint16_t bars[7] = {0xF800, 0x07E0, 0x001F, 0x07FF, 0xF81F, 0xFFE0, 0xFFFF};
      for (int b = 0; b < 7; ++b) {
        d->fillRect(93 + b * 40, 275, 36, 22, bars[b]);
        d->drawRect(93 + b * 40, 275, 36, 22, RADAR_COLOR_WHITE);
      }
    }

    drawCenteredText(d, 150, "TEST 2: CO5300 466x466", kSteps[i].fg, 2);
    drawCenteredText(d, 190, kSteps[i].name, kSteps[i].fg, 2);
    drawCenteredText(d, 238, "QSPI COL_OFFSET1=6 | CIRCULAR SAFE AREA", kSteps[i].fg, 1);

    flushDisplay();
    if (step_ms > 0) {
      delay(step_ms);
    }
  }

  // Test rampa di luminosità hardware (DCS 0x51)
  static const uint8_t kRampLevels[6] = {32, 80, 140, 200, 255, 220};
  uint32_t ramp_delay = (step_ms >= 180) ? (step_ms / 3) : 120;
  for (uint8_t r = 0; r < 6; ++r) {
    displaySetBrightness(kRampLevels[r]);
    char bline[40];
    snprintf(bline, sizeof(bline), "BRIGHTNESS RAMP: %u / 255", (unsigned)kRampLevels[r]);
    drawBadge(d, kCenterX - 115, 315, 230, 22, bline, RADAR_COLOR_AMBER, 0x1082, RADAR_COLOR_YELLOW, 1);
    flushDisplay();
    delay(ramp_delay);
  }

  return true;
}

void displayShowTestBanner(const char *title,
                           const char *subtitle,
                           const char *status_line,
                           uint16_t accent_color) {
  if (!s_display_initialized && !displayInit(false)) {
    return;
  }

  Arduino_GFX *d = getDrawTarget();
  d->fillScreen(RADAR_COLOR_BG);

  // Cornice circolare Dragon Radar
  d->drawCircle(kCenterX, kCenterY, 232, RADAR_COLOR_GRID_DARK);
  d->drawCircle(kCenterX, kCenterY, 228, accent_color);
  d->drawCircle(kCenterX, kCenterY, 190, RADAR_COLOR_GRID_DARK);
  d->drawFastHLine(kCenterX - 220, kCenterY, 440, 0x0160);
  d->drawFastVLine(kCenterX, kCenterY - 220, 440, 0x0160);

  drawBadge(d,
            kCenterX - 110,
            66,
            220,
            22,
            "DRAGON RADAR HARDWARE TEST",
            RADAR_COLOR_AMBER,
            0x1082,
            RADAR_COLOR_YELLOW,
            1);

  if (title && title[0] != '\0') {
    drawBadge(d, kCenterX - 155, 136, 310, 42, title, accent_color, 0x0841, accent_color, 2);
  }

  if (subtitle && subtitle[0] != '\0') {
    drawCenteredText(d, 212, subtitle, RADAR_COLOR_WHITE, 1);
  }

  if (status_line && status_line[0] != '\0') {
    drawBadge(d, kCenterX - 150, 268, 300, 28, status_line, RADAR_COLOR_CYAN, 0x0842, RADAR_COLOR_CYAN, 1);
  }

  drawCenteredText(d, 356, "Waveshare ESP32-S3-Touch-AMOLED-1.75-G", RADAR_COLOR_GRAY, 1);
  drawCenteredText(d, 376, "466x466 QSPI CO5300 | Shared I2C 100kHz", RADAR_COLOR_GRID_MID, 1);

  flushDisplay();
}

bool displayPollTouch(int16_t &x, int16_t &y, bool &tap_detected) {
  tap_detected = false;
  x = s_last_touch_x;
  y = s_last_touch_y;

  if (!s_touch_online) {
    return false;
  }

  const uint32_t now = millis();
  const bool irq = consumeTouchInterrupt() || (digitalRead(HW_TP_INT) == LOW);

  // Interroga il CST9217 su fronte di interrupt o mentre è premuto (max ogni 25 ms per non saturare I2C)
  if (!irq && !s_touch_pressed_state && (now - s_last_touch_poll_ms < 120)) {
    return false;
  }
  if (!irq && s_touch_pressed_state && (now - s_last_touch_poll_ms < 25)) {
    x = s_last_touch_x;
    y = s_last_touch_y;
    return true;
  }

  s_last_touch_poll_ms = now;

  if (!i2cBusLock(40)) {
    return s_touch_pressed_state;
  }

  int16_t tx[2] = {0, 0};
  int16_t ty[2] = {0, 0};
  uint8_t pts = s_touch.getPoint(tx, ty, 2);
  i2cBusUnlock();

  if (pts > 0) {
    int16_t cx = tx[0];
    int16_t cy = ty[0];
    if (cx < 0) cx = 0;
    if (cx >= HW_LCD_WIDTH) cx = HW_LCD_WIDTH - 1;
    if (cy < 0) cy = 0;
    if (cy >= HW_LCD_HEIGHT) cy = HW_LCD_HEIGHT - 1;

    s_last_touch_x = cx;
    s_last_touch_y = cy;
    x = cx;
    y = cy;

    if (!s_touch_pressed_state && (now - s_last_tap_ms >= 160)) {
      tap_detected = true;
      s_last_tap_ms = now;
      s_tap_count++;
    }
    s_touch_pressed_state = true;
    return true;
  }

  s_touch_pressed_state = false;
  return false;
}

void displayRenderDragonRadar(const RadarSystemSnapshot &snap, uint8_t active_page) {
  if (!s_display_initialized && !displayInit(false)) {
    return;
  }

  Arduino_GFX *d = getDrawTarget();
  d->fillScreen(RADAR_COLOR_BG);

  const bool has_error = (snap.error_flags != 0) ||
                         (snap.error_summary[0] != '\0' && strcmp(snap.error_summary, "OK") != 0);

  drawOuterCircularFrame(d, active_page, has_error, snap.cal_in_progress);

  if (snap.cal_in_progress) {
    renderCalibrationOverlay(d, snap);
  } else {
    switch (active_page % RADAR_PAGE_COUNT) {
      case RADAR_PAGE_MAIN_COMPASS:
        renderPage0MainCompass(d, snap);
        break;
      case RADAR_PAGE_I2C_PMIC:
        renderPage1I2cPmic(d, snap);
        break;
      case RADAR_PAGE_SENSORS_IMU:
        renderPage2SensorsImu(d, snap);
        break;
      case RADAR_PAGE_GNSS_LC76G:
        renderPage3GnssLc76g(d, snap);
        break;
      default:
        renderPage0MainCompass(d, snap);
        break;
    }
  }

  // Indicatore visivo di tocco in tempo reale (piccolo mirino ciano sul punto toccato)
  if (snap.touch_pressed || s_touch_pressed_state) {
    int16_t tx = snap.touch_pressed ? snap.touch_x : s_last_touch_x;
    int16_t ty = snap.touch_pressed ? snap.touch_y : s_last_touch_y;
    d->drawCircle(tx, ty, 10, RADAR_COLOR_CYAN);
    d->fillCircle(tx, ty, 3, RADAR_COLOR_WHITE);
  }

  flushDisplay();
}

bool displayIsInitialized() {
  return s_display_initialized;
}

bool displayIsTouchOnline() {
  return s_touch_online;
}

bool displayHasPsramCanvas() {
  return s_canvas_ready;
}

uint8_t displayGetBrightness() {
  return s_brightness;
}

const char *displayGetTouchModel() {
  if (!s_touch_online) {
    return "OFFLINE";
  }
  const char *name = s_touch.getModelName();
  return (name != nullptr) ? name : "CST9217";
}

uint32_t displayGetTouchTapCount() {
  return s_tap_count;
}

void displayPrintDiagnostics(Stream &out) {
  out.println(F("=== [DISPLAY & TOUCH DIAGNOSTICS] ==="));
  out.printf("  CO5300 QSPI (466x466, col_offset1=%d): %s\n",
             HW_LCD_COL_OFFSET1,
             s_display_initialized ? "INITIALIZED" : "NOT_INIT");
  out.printf("  PSRAM Canvas Framebuffer (434 KB):    %s\n",
             s_canvas_ready ? "ACTIVE (Zero-Flicker)" : "DIRECT_DRAW");
  out.printf("  Brightness Level (DCS 0x51):          %u / 255\n", (unsigned)s_brightness);
  out.printf("  Touch CST9217 (0x%02X, RST=%d, INT=%d): %s (Model: %s)\n",
             HW_ADDR_CST9217,
             HW_TP_RESET,
             HW_TP_INT,
             s_touch_online ? "ONLINE" : "OFFLINE",
             displayGetTouchModel());
  out.printf("  Last Touch XY: (%d, %d) | Pressed: %s | Taps: %lu\n",
             (int)s_last_touch_x,
             (int)s_last_touch_y,
             s_touch_pressed_state ? "YES" : "NO",
             (unsigned long)s_tap_count);
}

void displayRenderOsScreen(uint8_t app, bool launcher, uint8_t selected) {
  if (!s_display_initialized) return;
  Arduino_GFX *d = getDrawTarget();
  d->fillScreen(RADAR_COLOR_BG);
  d->drawCircle(233, 233, 222, RADAR_COLOR_GRID_MID);
  if (launcher) {
    drawCenteredText(d, 58, "APPLICAZIONI", RADAR_COLOR_GRID_BRIGHT, 2);
    const char *line1[]={"DRAGON","AUDIOBOOK","IMPOSTAZ.","DEBUG"};
    const char *line2[]={"RADAR","SHELF","",""};
    for (uint8_t i=0;i<4;++i) {
      const int x=launcherTileX(i), y=launcherTileY(i);
      if (i==selected)
        d->fillRoundRect(x,y,LAUNCHER_TILE_WIDTH,LAUNCHER_TILE_HEIGHT,14,RADAR_COLOR_GRID_DARK);
      d->drawRoundRect(x,y,LAUNCHER_TILE_WIDTH,LAUNCHER_TILE_HEIGHT,14,
                       i==selected?RADAR_COLOR_YELLOW:RADAR_COLOR_GRID_BRIGHT);
      if (i==selected) d->drawRoundRect(x+2,y+2,LAUNCHER_TILE_WIDTH-4,LAUNCHER_TILE_HEIGHT-4,12,RADAR_COLOR_YELLOW);
      d->setTextColor(RADAR_COLOR_WHITE); d->setTextSize(2);
      d->setCursor(x+(LAUNCHER_TILE_WIDTH-int(strlen(line1[i]))*12)/2,y+(line2[i][0]?37:48));
      d->print(line1[i]);
      if (line2[i][0]) {
        d->setCursor(x+(LAUNCHER_TILE_WIDTH-int(strlen(line2[i]))*12)/2,y+59);
        d->print(line2[i]);
      }
    }
    drawCenteredText(d, 376, "TOCCA PER APRIRE", RADAR_COLOR_WHITE, 2);
    drawCenteredText(d, 402, "BREVE: SCEGLI", RADAR_COLOR_GRID_BRIGHT, 1);
    drawCenteredText(d, 414, "LUNGO: APRI", RADAR_COLOR_GRID_BRIGHT, 1);
  } else if (app == 1) {
    drawCenteredText(d, 75, "AUDIOBOOKSHELF", RADAR_COLOR_GRID_BRIGHT, 2);
    drawCenteredText(d, 112, audiobookshelfStatusText(), RADAR_COLOR_WHITE, 2);
    static AbsCatalog list;
    if (audiobookshelfCopyCatalog(list) && list.count) {
      unsigned selected = audiobookshelfSelectedIndex() % list.count;
      char line[80]; snprintf(line,sizeof(line),"%u/%u %.20s",selected+1,(unsigned)list.count,list.items[selected].title[0]?list.items[selected].title:"(senza titolo)");
      drawCenteredText(d, 157, line, RADAR_COLOR_WHITE, 2);
      snprintf(line,sizeof(line),"%.24s",list.items[selected].author);
      drawCenteredText(d, 185, line, RADAR_COLOR_GRAY, 2);
      drawCenteredText(d, 215, "TOCCA: ALTRO LIBRO", RADAR_COLOR_GRID_MID, 2);
    } else {
      drawCenteredText(d, 167, audiobookshelfIsAuthenticated()?"dragon-user COLLEGATA":"dragon-user NON COLLEGATA", RADAR_COLOR_WHITE, 2);
      drawCenteredText(d, 200, "PLAY NON DISPONIBILE", RADAR_COLOR_GRAY, 2);
    }
    d->drawRoundRect(78,245,145,68,8,RADAR_COLOR_GRID_BRIGHT);
    d->drawRoundRect(243,245,145,68,8,RADAR_COLOR_GRID_BRIGHT);
    d->setTextSize(2); d->setTextColor(RADAR_COLOR_WHITE);
    d->setCursor(102,272); d->print(audiobookshelfIsAuthenticated()?"CATALOGO":"SERVER");
    d->setCursor(255,272); d->print("TEST AUDIO");
    char volume[24]; snprintf(volume,sizeof(volume),"VOL %u",settingsGet().volume);
    d->drawRoundRect(108,330,65,55,7,RADAR_COLOR_GRID_BRIGHT);
    d->drawRoundRect(293,330,65,55,7,RADAR_COLOR_GRID_BRIGHT);
    d->setCursor(134,348); d->print("-"); d->setCursor(319,348); d->print("+");
    drawCenteredText(d,348,volume,RADAR_COLOR_WHITE,2);
  } else if (app == 2) {
    drawCenteredText(d, 160, "IMPOSTAZIONI", RADAR_COLOR_GRID_BRIGHT, 2);
    drawCenteredText(d, 225, "Non ancora disponibili", RADAR_COLOR_WHITE, 1);
  }
  d->fillRoundRect(207, 432, 52, 5, 2, RADAR_COLOR_GRID_MID);
  flushDisplay();
}

namespace {
constexpr uint16_t rgb565(uint8_t r,uint8_t g,uint8_t b) {
  return uint16_t(((r&0xf8u)<<8)|((g&0xfcu)<<3)|(b>>3));
}
constexpr uint16_t kClosedGreen=rgb565(36,168,74), kClosedGrid=rgb565(20,85,43);
constexpr uint16_t kClosedInk=rgb565(9,41,21), kClosedAmber=rgb565(255,176,32);
constexpr uint16_t kClosedRed=rgb565(190,35,27), kClosedLight=rgb565(255,236,163);

void drawClosedStar(Arduino_GFX *d,int x,int y,int r) {
  // Five-point star, ten alternating vertices, integer fixed-point geometry.
  static const int8_t px[10]={0,26,95,42,59,0,-59,-42,-95,-26};
  static const int8_t py[10]={-100,-36,-31,14,81,44,81,14,-31,-36};
  for (int i=0;i<10;++i) {
    const int j=(i+1)%10;
    d->fillTriangle(x,y,x+px[i]*r/100,y+py[i]*r/100,
                    x+px[j]*r/100,y+py[j]*r/100,kClosedRed);
  }
}

void drawClosedRadar(Arduino_GFX *d,const RadarView &v) {
  const auto game=beaconServiceSphereGame();
  const auto link=beaconServiceSecureLinkStatus();
  const auto ball=closedBallVisual(game.selected,link==SecureLinkStatus::Paired,
                                   link==SecureLinkStatus::Lost,v.proximity);
  const bool available=ball.state==ClosedBallState::Signal;
  d->fillScreen(kClosedInk);
  d->fillCircle(233,233,222,kClosedGreen);
  d->drawCircle(233,233,221,kClosedGrid);
  d->drawCircle(233,233,kClosedGridRadius,kClosedGrid);
  // Clip rotated grid lines to the circular instrument face.
  const auto rx=[](int x,int y){return int(lroundf(.5f*x-.8660254f*y));};
  const auto ry=[](int x,int y){return int(lroundf(.8660254f*x+.5f*y));};
  for (int i=-180;i<=180;i+=45) {
    const int h=int(sqrtf(float(kClosedGridRadius*kClosedGridRadius-i*i)));
    d->drawLine(233+rx(-h,i),233+ry(-h,i),233+rx(h,i),233+ry(h,i),kClosedGrid);
    d->drawLine(233+rx(i,-h),233+ry(i,-h),233+rx(i,h),233+ry(i,h),kClosedGrid);
  }
  const float a=(millis()%RADAR_SCAN_PERIOD_MS)*(2.0f*PI/RADAR_SCAN_PERIOD_MS);
  for (int i=0;i<6;++i) {
    const float t=a-i*.035f;
    d->drawLine(233,233,233+int(187*cosf(t)),233+int(187*sinf(t)),
                i==0?kClosedInk:kClosedGrid);
  }
  if (ball.state==ClosedBallState::Signal || ball.state==ClosedBallState::Found) {
    const int r=ball.radius;
    d->fillCircle(233,233,r,kClosedInk);
    d->fillCircle(233,233,r-2,kClosedAmber);
    d->fillCircle(233-r/3,233-r/2,std::max(2,r/9),kClosedLight);
    if (ball.state==ClosedBallState::Found) {
      d->drawLine(218,234,229,245,kClosedInk);
      d->drawLine(229,245,249,222,kClosedInk);
      d->drawLine(218,235,229,246,kClosedInk);
      d->drawLine(229,246,249,223,kClosedInk);
    } else for (uint8_t i=0;i<game.selected;++i) {
      const auto s=closedStar(game.selected,i,ball.radius);
      if (s.r) drawClosedStar(d,233+s.x,233+s.y,s.r);
    }
  } else if (ball.state!=ClosedBallState::None) {
    for (int i=0;i<8;++i) {
      const float t=i*PI/4;
      d->fillCircle(233+int(12*cosf(t)),233+int(12*sinf(t)),1,kClosedInk);
    }
  }
  char text[48];
  if (game.selected) snprintf(text,sizeof(text),"SFERA %u  -  %u/7",unsigned(game.selected),unsigned(game.foundCount));
  else snprintf(text,sizeof(text),"TROVATE %u/7",unsigned(game.foundCount));
  d->fillRoundRect(104,57,258,26,6,kClosedInk);
  drawCenteredText(d,62,text,RADAR_COLOR_WHITE,2);
  d->fillRoundRect(177,88,112,25,5,kClosedGreen);
  drawCenteredText(d,91,"CHIUSO",kClosedInk,2);
  for (uint8_t id=1;id<=SphereSearch::count;++id) {
    const uint8_t bit=1u<<(id-1);
    const bool found=game.found&bit, selected=game.selected==id;
    const bool associated=game.associated&bit;
    const int x=RADAR_SPHERE_X+(id-1)*RADAR_SPHERE_PITCH;
    d->fillRoundRect(x,RADAR_SPHERE_Y,RADAR_SPHERE_WIDTH,RADAR_SPHERE_HEIGHT,5,
                     selected?kClosedAmber:kClosedInk);
    d->drawRoundRect(x,RADAR_SPHERE_Y,RADAR_SPHERE_WIDTH,RADAR_SPHERE_HEIGHT,5,
                     selected?kClosedInk:associated?kClosedLight:kClosedGrid);
    d->setTextSize(2); d->setTextColor(selected?kClosedInk:associated?RADAR_COLOR_WHITE:RADAR_COLOR_GRAY);
    d->setCursor(x+14,RADAR_SPHERE_Y+6); d->print(unsigned(id));
    if (found) {
      d->drawLine(x+27,RADAR_SPHERE_Y+19,x+30,RADAR_SPHERE_Y+22,kClosedLight);
      d->drawLine(x+30,RADAR_SPHERE_Y+22,x+36,RADAR_SPHERE_Y+14,kClosedLight);
    }
  }
  const char *state="RICERCA";
  switch (ball.state) {
    case ClosedBallState::None: state=game.foundCount==7?"TUTTE TROVATE":"NESSUNA SFERA"; break;
    case ClosedBallState::Lost: state="SEGNALE PERSO"; break;
    case ClosedBallState::Found: state=game.foundCount==7?"TUTTE TROVATE":"TROVATA"; break;
    case ClosedBallState::Searching: break;
    case ClosedBallState::Signal:
      switch(v.proximity.state) {
        case ProximityState::Weak: state="DEBOLE"; break;
        case ProximityState::Approaching: state="TI AVVICINI"; break;
        case ProximityState::MovingAway: state="TI ALLONTANI"; break;
        case ProximityState::Near: state="VICINO"; break;
        default: state="STABILE"; break;
      }
      break;
  }
  d->fillRoundRect(113,304,240,24,5,kClosedInk);
  drawCenteredText(d,308,state,ball.state==ClosedBallState::Lost?kClosedLight:RADAR_COLOR_WHITE,2);
  d->fillRoundRect(125,330,216,23,5,kClosedGreen);
  if (available) {
    snprintf(text,sizeof(text),"SEGNALE %u%%",unsigned(v.proximity.level)*100u/kProximityMaxLevel);
    drawCenteredText(d,334,text,kClosedInk,2);
    d->fillRoundRect(RADAR_FOUND_X,RADAR_FOUND_Y,RADAR_FOUND_WIDTH,RADAR_FOUND_HEIGHT,8,kClosedInk);
    drawCenteredText(d,366,"TROVATA? CONFERMA",RADAR_COLOR_WHITE,2);
  } else if (game.selected && ball.state!=ClosedBallState::Found)
    drawCenteredText(d,334,link==SecureLinkStatus::Paired?"ATTESA RSSI":"NON CONNESSO",kClosedInk,2);
  if (game.foundCount) {
    d->fillRoundRect(RADAR_GAME_X,RADAR_GAME_Y,RADAR_GAME_WIDTH,RADAR_GAME_HEIGHT,6,kClosedInk);
    drawCenteredText(d,402,"NUOVA PARTITA",RADAR_COLOR_WHITE,2);
  }
  d->fillRoundRect(207,432,52,5,2,kClosedInk);
}
}

void displayRenderRadarApp(const RadarView &v) {
  if (!s_display_initialized) return;
  if (!v.demo && !v.open) { drawClosedRadar(getDrawTarget(),v); flushDisplay(); return; }
  Arduino_GFX *d = getDrawTarget(); d->fillScreen(0x04A0);
  // Rotation is clockwise in screen coordinates (Y increases downward).
  const auto rx = [](int x, int y) { return (int16_t)lroundf(0.5f*x - 0.8660254f*y); };
  const auto ry = [](int x, int y) { return (int16_t)lroundf(0.8660254f*x + 0.5f*y); };
  for (int i = -180; i <= 180; i += 45) {
    d->drawLine(233+rx(-180,i), 233+ry(-180,i), 233+rx(180,i), 233+ry(180,i), RADAR_COLOR_BG);
    d->drawLine(233+rx(i,-180), 233+ry(i,-180), 233+rx(i,180), 233+ry(i,180), RADAR_COLOR_BG);
  }
  d->drawCircle(233, 233, 190, RADAR_COLOR_BG);
  // Scansione circolare oraria con scia, calcolata dal tempo senza blocchi.
  const float scanAngle = (millis() % RADAR_SCAN_PERIOD_MS) * (2.0f * PI / RADAR_SCAN_PERIOD_MS) - PI / 2.0f + PI / 3.0f;
  const uint16_t trailColors[] = {0x0480,0x0440,0x0400,0x03C0,0x0380,0x0340,0x02E0,0x0280,0x0220,0x0180};
  for (int i=0;i<10;++i) {
    const float angle=scanAngle-(9-i)*0.035f;
    const float dx=cosf(angle),dy=sinf(angle);
    for (int thickness=-1;thickness<=1;++thickness) {
      const int16_t ox=(int16_t)lroundf(-dy*thickness), oy=(int16_t)lroundf(dx*thickness);
      d->drawLine(233+(int16_t)(dx*15)+ox,233+(int16_t)(dy*15)+oy,
                  233+(int16_t)(dx*187)+ox,233+(int16_t)(dy*187)+oy,trailColors[i]);
    }
  }
  if (v.open) d->fillTriangle(233,211,218,246,248,246,RADAR_COLOR_RED);
  else d->fillTriangle(233+rx(0,-22),233+ry(0,-22),233+rx(-15,13),233+ry(-15,13),233+rx(15,13),233+ry(15,13),RADAR_COLOR_RED);
  const auto game=beaconServiceSphereGame();
  if (v.open || (millis() / 450) % 2 == 0) for (unsigned i=0; i<v.count; ++i) {
    const auto &ball=v.balls[i];
    const int x=233+(v.open ? ball.x : rx(ball.x,ball.y)), y=233+(v.open ? ball.y : ry(ball.x,ball.y));
    const uint16_t color=v.open && ball.id!=game.selected ? RADAR_COLOR_WHITE : RADAR_COLOR_YELLOW;
    if (ball.offscreen) d->drawCircle(x,y,5,color);
    else { d->fillCircle(x,y,7,color); d->drawCircle(x,y,10,RADAR_COLOR_AMBER); }
    if (v.open) {
      d->setTextSize(1);
      d->setTextColor(color);
      d->setCursor(x+9,y-5);
      d->print(unsigned(ball.id));
    }
  }
  char text[48];
  if (v.demo) snprintf(text,sizeof(text),"DEMO  %um",v.range_m);
  if (!v.demo) {
    if (game.selected) snprintf(text,sizeof(text),"SFERA %u  TROVATE %u/7",unsigned(game.selected),unsigned(game.foundCount));
    else snprintf(text,sizeof(text),"TROVATE %u/7",unsigned(game.foundCount));
  }
  drawCenteredText(d, 62, text, RADAR_COLOR_BG, 2);
  if (v.open) {
    if (v.heading_up) snprintf(text,sizeof(text),"APERTO  %03.0f  ZOOM %um",v.heading,v.range_m);
    else snprintf(text,sizeof(text),"APERTO  NORD FISSO  ZOOM %um",v.range_m);
  } else {
    snprintf(text,sizeof(text),"%s %03.0f %s",v.heading_up?"HEAD":"NORD",v.heading,v.compass_calibrated?"CAL":"RAW");
  }
  drawCenteredText(d, 88, text, RADAR_COLOR_BG, 1);
  if (!v.compass_online) drawCenteredText(d, 108, "BUSSOLA OFFLINE", RADAR_COLOR_RED, 1);
  else if (v.heading_held) drawCenteredText(d, 108, "BUSSOLA PAUSA MOTORE", RADAR_COLOR_YELLOW, 1);
  const auto link = beaconServiceSecureLinkStatus();
  if (v.demo) {
    drawCenteredText(d, 333, secureLinkLabel(link),
                     link == SecureLinkStatus::Paired ? RADAR_COLOR_BG : RADAR_COLOR_YELLOW, 2);
    snprintf(text,sizeof(text),"SIMULATO: %.0f m",v.nearest_m);
    drawCenteredText(d, 373,text,RADAR_COLOR_BG,1);
  } else {
    const auto &signal = v.proximity;
    const bool found = signal.state == ProximityState::Found;
    const bool available = !found && link == SecureLinkStatus::Paired && signal.fresh;
    for (uint8_t id=1; id<=SphereSearch::count; ++id) {
      const uint8_t bit=1u<<(id-1);
      const bool associated=game.associated & bit;
      const bool completed=game.found & bit;
      const int x=RADAR_SPHERE_X+(id-1)*RADAR_SPHERE_PITCH;
      const uint16_t color=completed ? RADAR_COLOR_GRID_BRIGHT : associated ? RADAR_COLOR_WHITE : RADAR_COLOR_GRAY;
      d->fillRoundRect(x,RADAR_SPHERE_Y,RADAR_SPHERE_WIDTH,RADAR_SPHERE_HEIGHT,5,RADAR_COLOR_BG);
      d->drawRoundRect(x,RADAR_SPHERE_Y,RADAR_SPHERE_WIDTH,RADAR_SPHERE_HEIGHT,5,
                       game.selected==id ? RADAR_COLOR_YELLOW : color);
      d->setTextSize(2);
      d->setTextColor(color);
      d->setCursor(x+14,RADAR_SPHERE_Y+6);
      d->print(unsigned(id));
    }
    constexpr int ringCount = 3;
    constexpr int stepsPerRing = kProximityMaxLevel / ringCount;
    const int level = available ? signal.level : 0;
    for (int i = 0; i < ringCount; ++i) {
      const int radius = 34 + 20*i;
      d->drawCircle(233,233,radius+2*stepsPerRing,RADAR_COLOR_GRID_DARK);
      const int steps = std::min(stepsPerRing,std::max(0,level-i*stepsPerRing));
      for (int j = 0; j < 2*steps; ++j)
        d->drawCircle(233,233,radius+j,RADAR_COLOR_YELLOW);
    }
    if (v.open) {
      const char *gpsText=nullptr;
      if (v.gps_save==GpsSaveStatus::Saved) gpsText="POSIZIONE SALVATA";
      else if (v.gps_save==GpsSaveStatus::Failed) gpsText="SALVATAGGIO GPS FALLITO";
      else if (!v.gps_valid) gpsText="GPS SENZA FIX  |  RSSI";
      else if (!v.selected_position) gpsText="POSIZIONE GPS ASSENTE  |  RSSI";
      else if (v.gps_distance_m>=1.0)
        snprintf(text,sizeof(text),"GPS %.0f m  %03u gradi",v.gps_distance_m,
                 unsigned(lround(v.gps_bearing_deg))%360);
      else if (v.gps_distance_m>=0.0) gpsText="GPS MENO DI 1 m";
      else gpsText="POSIZIONE GPS SALVATA";
      drawCenteredText(d,278,gpsText?gpsText:text,RADAR_COLOR_WHITE,1);
    }
    const char *proximityText=game.foundCount==SphereSearch::count ? "TUTTE LE SFERE TROVATE" :
                              game.selected ? proximityLabel(signal,link) : "NESSUNA SFERA ASSOCIATA";
    drawCenteredText(d, 308, proximityText, RADAR_COLOR_YELLOW, 2);
    drawCenteredText(d, 337,secureLinkLabel(link),RADAR_COLOR_BG,1);
    if (available) {
      d->fillRoundRect(RADAR_FOUND_X,RADAR_FOUND_Y,RADAR_FOUND_WIDTH,RADAR_FOUND_HEIGHT,8,RADAR_COLOR_BG);
      drawCenteredText(d, 367,"CONFERMA RITROVAMENTO",RADAR_COLOR_WHITE,2);
    }
    if (v.open) {
      d->fillRoundRect(RADAR_GPS_X,RADAR_GAME_Y,RADAR_OPEN_ACTION_WIDTH,RADAR_GAME_HEIGHT,6,RADAR_COLOR_BG);
      d->setTextSize(1);
      d->setTextColor(v.gps_can_save ? RADAR_COLOR_WHITE : RADAR_COLOR_GRAY);
      d->setCursor(RADAR_GPS_X+35,405);
      d->print("SALVA GPS");
      if (game.foundCount) {
        d->fillRoundRect(RADAR_OPEN_GAME_X,RADAR_GAME_Y,RADAR_OPEN_ACTION_WIDTH,RADAR_GAME_HEIGHT,6,RADAR_COLOR_BG);
        d->setTextColor(RADAR_COLOR_WHITE);
        d->setCursor(RADAR_OPEN_GAME_X+23,405);
        d->print("NUOVA PARTITA");
      }
    } else if (game.foundCount) {
      d->fillRoundRect(RADAR_GAME_X,RADAR_GAME_Y,RADAR_GAME_WIDTH,RADAR_GAME_HEIGHT,6,RADAR_COLOR_BG);
      drawCenteredText(d, 404,"NUOVA PARTITA",RADAR_COLOR_WHITE,2);
    }
  }
  d->fillRoundRect(207, 432, 52, 5, 2, RADAR_COLOR_GRID_BRIGHT); flushDisplay();
}

void displayRenderSettings(uint8_t selection, const RadarSystemSnapshot &snap) {
  if (!s_display_initialized) return;
  Arduino_GFX *d = getDrawTarget();
  const OsSettings &s = settingsGet();
  d->fillScreen(RADAR_COLOR_BG);
  d->drawCircle(233, 233, 222, RADAR_COLOR_GRID_MID);
  drawCenteredText(d, 62, "IMPOSTAZIONI", RADAR_COLOR_GRID_BRIGHT, 2);
  char page[24]; snprintf(page, sizeof(page), "%u / %u", selection + 1, SETTINGS_PAGE_COUNT);
  drawCenteredText(d, 94, page, RADAR_COLOR_GRAY, 2);

  const char *title = "";
  char value[48] = "", detail1[48] = "", detail2[48] = "", detail3[48] = "";
  switch (selection) {
    case 0: title="LUMINOSITA"; snprintf(value,sizeof(value),"%u / 255",s.brightness); break;
    case 1: title="VOLUME"; snprintf(value,sizeof(value),"%u %%",s.volume); break;
    case 2: title="VIBRAZIONE"; snprintf(value,sizeof(value),"%s",s.vibration?"ON":"OFF"); break;
    case 3:
      title="RADAR";
      snprintf(value,sizeof(value),"%s",s.radar_mode==RadarMode::Demo ? "DEMO" :
               s.radar_mode==RadarMode::Closed ? "REALE CHIUSO" : "REALE APERTO");
      if (s.radar_mode==RadarMode::Open) snprintf(detail1,sizeof(detail1),"GPS + ESP-NOW");
      else if (s.radar_mode==RadarMode::Closed) snprintf(detail1,sizeof(detail1),"ESP-NOW / RSSI");
      break;
    case 4: title="ORIENTAMENTO"; snprintf(value,sizeof(value),"%s",s.heading_up?"BUSSOLA":"NORD FISSO"); break;
    case 5: title="CALIBRAZIONE"; snprintf(value,sizeof(value),"%s",magCalIsInProgress()?"STOP / SALVA":"AVVIA"); break;
    case 6:
      title="WI-FI";
      if (wifiServicePortalActive()) {
        snprintf(value,sizeof(value),"CONFIGURA");
        snprintf(detail1,sizeof(detail1),"CHIAVE: %s",wifiServicePortalPassword());
        snprintf(detail2,sizeof(detail2),"192.168.4.1");
        snprintf(detail3,sizeof(detail3),"DragonRadar-Setup");
      } else {
        snprintf(value,sizeof(value),"%s",wifiServiceStatusText());
        snprintf(detail1,sizeof(detail1),"TOCCA: CONFIGURA");
      }
      break;
    case 7: title="OFFSET SENSORE"; snprintf(value,sizeof(value),"%.0f gradi",s.mount_offset); break;
    case 8: title="DECLINAZIONE"; snprintf(value,sizeof(value),"%.0f gradi",s.declination); break;
    case 9:
      title="AGGIORNA OTA";
      snprintf(value,sizeof(value),"%s",otaServiceStatus());
      snprintf(detail1,sizeof(detail1),"DEBUG > AGGIORNAMENTI OTA");
      break;
    case 12: title="POWER OFF"; snprintf(value,sizeof(value),"STANDBY"); snprintf(detail1,sizeof(detail1),"%s",otaServiceActive()?"OTA ATTIVO":"TASTO: ACCENDI"); break;
    case 13: title="RIAVVIO"; snprintf(value,sizeof(value),"%s",appSettingsRestartConfirm()?"CONFERMA?":"RIAVVIA"); break;
    case 15: title="MICROSD"; snprintf(value,sizeof(value),"%s",sdServiceStatusText()); if(sdServiceState()==SdServiceState::Mounted) snprintf(detail1,sizeof(detail1),"%lu MB LIBERI",(unsigned long)sdServiceFreeMB()); else snprintf(detail1,sizeof(detail1),"NO FORMATTAZIONE"); break;
    case 14: title="BATTERIA"; if (snap.battery_present && snap.batt_percent >= 0) { snprintf(value,sizeof(value),"%d %%",snap.batt_percent); snprintf(detail1,sizeof(detail1),"%u mV %s",snap.batt_mv,snap.is_charging?"IN CARICA":""); } else snprintf(value,sizeof(value),"NON RILEVATA"); break;
    case 11: title="SUONI"; snprintf(value,sizeof(value),"%s",s.sound?"ON":"OFF"); break;
    case 10: title="RIPRISTINO"; snprintf(value,sizeof(value),"%s",appSettingsResetConfirm()?"CONFERMA?":"IMPOSTAZIONI"); break;
  }
  drawCenteredText(d, 145, title, RADAR_COLOR_GRID_BRIGHT, 2);
  d->drawRoundRect(113, 181, 240, 60, 8, RADAR_COLOR_GRID_MID);
  drawCenteredText(d, 201, value, RADAR_COLOR_WHITE, strlen(value) <= 12 ? 3 : 2);
  if (detail1[0]) drawCenteredText(d, 258, detail1, RADAR_COLOR_YELLOW, 2);
  if (detail2[0]) drawCenteredText(d, 285, detail2, RADAR_COLOR_WHITE, 2);
  if (detail3[0]) drawCenteredText(d, 312, detail3, RADAR_COLOR_WHITE, 2);
  d->fillTriangle(43, 211, 88, 187, 88, 235, RADAR_COLOR_GRID_BRIGHT);
  d->fillTriangle(423, 211, 378, 187, 378, 235, RADAR_COLOR_GRID_BRIGHT);
  drawCenteredText(d, 340, "TOCCA IL CENTRO", RADAR_COLOR_GRAY, 2);
  drawCenteredText(d, 365, "FRECCE: PAGINA", RADAR_COLOR_GRAY, 2);
  char info[40]; snprintf(info,sizeof(info),"BAT %d%%  %u mV",snap.batt_percent,snap.batt_mv);
  drawCenteredText(d, 398, info, RADAR_COLOR_GRAY, 1);
  d->fillRoundRect(207,432,52,5,2,RADAR_COLOR_GRID_MID);
  flushDisplay();
}

void displayRenderDebugLarge(const RadarSystemSnapshot &snap, uint8_t page) {
  if (!s_display_initialized) return;
  Arduino_GFX *d=getDrawTarget(); d->fillScreen(RADAR_COLOR_BG); d->drawCircle(233,233,222,RADAR_COLOR_GRID_MID);
  char heading[32]; snprintf(heading,sizeof(heading),"DEBUG %u/%u",(unsigned)(page%DEBUG_PAGE_COUNT)+1,(unsigned)DEBUG_PAGE_COUNT);
  drawCenteredText(d,65,heading,RADAR_COLOR_GRID_BRIGHT,2);
  const char *title=""; char a[48]="",b[48]="",c[48]="",e[48]="",detail[48]="";
  if (page==DEBUG_SPHERE_OTA_PAGE) {
    const auto &ota=debugSphereOta();
    const auto version=beaconServiceSphereVersion(ota.id);
    drawCenteredText(d,90,"AGGIORNAMENTI OTA",RADAR_COLOR_GRID_BRIGHT,2);
    d->fillRoundRect(82,121,302,35,8,RADAR_COLOR_GRID_DARK);
    drawCenteredText(d,130,ota.radar ? "[RADAR]      SFERA" : "RADAR      [SFERA]",RADAR_COLOR_WHITE,2);
    if (ota.radar) {
      const auto state=otaServiceState();
      snprintf(a,sizeof(a),"DragonRadar  v%lu",(unsigned long)otaServiceVersion());
      drawCenteredText(d,173,a,RADAR_COLOR_WHITE,2);
      drawCenteredText(d,203,otaServiceStatus(),RADAR_COLOR_GRID_BRIGHT,2);
      if (state==RadarOtaState::Ready) {
        snprintf(a,sizeof(a),"WI-FI  %s",otaServicePassword());
        drawCenteredText(d,229,a,RADAR_COLOR_WHITE,2);
        drawCenteredText(d,257,"CHIAVE MANIFEST",RADAR_COLOR_WHITE,2);
        snprintf(a,sizeof(a),"%.16s",otaServiceManifestKey());
        drawCenteredText(d,280,a,RADAR_COLOR_WHITE,2);
        if (strlen(otaServiceManifestKey())==32)
          drawCenteredText(d,301,otaServiceManifestKey()+16,RADAR_COLOR_WHITE,2);
        drawCenteredText(d,326,"192.168.4.1",RADAR_COLOR_WHITE,2);
      } else if (state==RadarOtaState::Receiving || state==RadarOtaState::Committing || state==RadarOtaState::Rebooting) {
        const unsigned percent=otaServiceProgress();
        snprintf(a,sizeof(a),"%u%%",percent);
        drawCenteredText(d,240,a,RADAR_COLOR_WHITE,3);
        d->drawRect(106,280,254,20,RADAR_COLOR_GRID_BRIGHT);
        d->fillRect(108,282,250*percent/100,16,RADAR_COLOR_GRID_BRIGHT);
      }
      if (state!=RadarOtaState::Committing && state!=RadarOtaState::Rebooting) {
        d->fillRoundRect(82,348,302,44,8,RADAR_COLOR_GRID_DARK);
        drawCenteredText(d,362,otaServiceActive() ? "ANNULLA OTA" :
            ota.radarConfirm ? "CONFERMA     ANNULLA" : "ATTIVA OTA",RADAR_COLOR_WHITE,2);
      }
      flushDisplay();
      return;
    }
    snprintf(a,sizeof(a),"<       SFERA %u       >",unsigned(ota.id));
    drawCenteredText(d,166,a,RADAR_COLOR_WHITE,2);
    snprintf(a,sizeof(a),"SSID DragonBall-%02u",unsigned(ota.id));
    drawCenteredText(d,191,a,RADAR_COLOR_WHITE,2);
    drawCenteredText(d,211,secureLinkLabel(beaconServiceLinkForId(ota.id)),RADAR_COLOR_WHITE,2);
    d->fillRoundRect(82,240,302,45,8,RADAR_COLOR_GRID_DARK);
    drawCenteredText(d,252,ota.state==SphereOtaUi::Confirm ? "CONFERMA      ANNULLA" : "ATTIVA OTA",RADAR_COLOR_WHITE,2);
    snprintf(a,sizeof(a),version ? "LEGGI VERSIONE  |  %lu" : "LEGGI VERSIONE  |  --",(unsigned long)version);
    drawCenteredText(d,304,a,RADAR_COLOR_WHITE,2);
    const char *message="";
    switch (ota.state) {
      case SphereOtaUi::Query: case SphereOtaUi::Arming: case SphereOtaUi::Starting:
        message="IN ATTESA DI CONFERMA"; break;
      case SphereOtaUi::Ack: message="COMANDO CONFERMATO"; break;
      case SphereOtaUi::Unknown: message="ESITO NON VERIFICABILE"; break;
      case SphereOtaUi::Refused: message="COMANDO NON DISPONIBILE"; break;
      default: break;
    }
    drawCenteredText(d,352,message,RADAR_COLOR_WHITE,2);
    if (ota.state==SphereOtaUi::Ack)
      drawCenteredText(d,378,"VERIFICA AP SUL PC",RADAR_COLOR_WHITE,2);
    flushDisplay();
    return;
  }
  if (page==DEBUG_SPHERE_RGB_PAGE) {
    const auto &rgb=debugSphereRgb();
    const auto status=beaconServiceRgbStatus(rgb.id);
    constexpr const char *colors[]={"ROSSO","ARANCIONE","ORO","VERDE","BLU","VIOLA","BIANCO"};
    drawCenteredText(d,115,"LED SFERA",RADAR_COLOR_GRID_BRIGHT,2);
    snprintf(a,sizeof(a),"<       SFERA %u       >",unsigned(rgb.id));
    drawCenteredText(d,160,a,RADAR_COLOR_WHITE,2);
    snprintf(a,sizeof(a),"LUMINOSITA %u%%",unsigned(rgb.brightness));
    drawCenteredText(d,202,a,RADAR_COLOR_WHITE,1);
    d->drawFastHLine(82,225,302,RADAR_COLOR_GRID_MID);
    d->fillCircle(82+301*rgb.brightness/100,225,5,RADAR_COLOR_YELLOW);
    snprintf(a,sizeof(a),"<       %s       >",colors[rgb.color]);
    drawCenteredText(d,256,a,RADAR_COLOR_WHITE,2);
    d->fillRoundRect(82,295,302,50,8,RADAR_COLOR_GRID_DARK);
    drawCenteredText(d,311,"ACCENDI        SPEGNI",RADAR_COLOR_WHITE,2);
    const char *result=rgb.unavailable ? "SFERA NON DISPONIBILE" :
      status==SecureMotorStatus::Pending ? "IN ATTESA" :
      status==SecureMotorStatus::Accepted ? "COMANDO ACCETTATO" :
      status==SecureMotorStatus::Unavailable ? "NON DISPONIBILE" :
      status==SecureMotorStatus::Unconfirmed ? "NESSUNA CONFERMA" : "";
    drawCenteredText(d,369,result,RADAR_COLOR_WHITE,1);
    flushDisplay();
    return;
  }
  if (page == DEBUG_SPHERE_MOTOR_PAGE) {
    const auto link = beaconServiceSecureLinkStatus();
    const auto status = beaconServiceSphereMotorStatus();
    const bool enabled = link == SecureLinkStatus::Paired && status != SecureMotorStatus::Pending;
    const auto game=beaconServiceSphereGame();
    char title[32];
    snprintf(title,sizeof(title),"VIBRAZIONE SFERA %u",unsigned(game.selected));
    drawCenteredText(d,120,game.selected?title:"VIBRAZIONE SFERA",RADAR_COLOR_GRID_BRIGHT,2);
    drawCenteredText(d,167,secureLinkLabel(link),RADAR_COLOR_WHITE,2);
    d->fillRoundRect(DEBUG_SPHERE_MOTOR_X,DEBUG_SPHERE_MOTOR_Y,DEBUG_SPHERE_MOTOR_WIDTH,
                     DEBUG_SPHERE_MOTOR_HEIGHT,8,enabled ? RADAR_COLOR_GRID_MID : RADAR_COLOR_GRID_DARK);
    drawCenteredText(d,232,"FAI VIBRARE LA SFERA",enabled ? RADAR_COLOR_WHITE : RADAR_COLOR_GRAY,2);
    const char *result = "";
    switch (status) {
      case SecureMotorStatus::Idle: break;
      case SecureMotorStatus::Pending: result="IN ATTESA DELLA SFERA"; break;
      case SecureMotorStatus::Accepted: result="COMANDO ACCETTATO"; break;
      case SecureMotorStatus::CoolingDown: result="ATTENDI PRIMA DI RIPROVARE"; break;
      case SecureMotorStatus::Unavailable: result="COMANDO NON DISPONIBILE"; break;
      case SecureMotorStatus::Unconfirmed: result="ESITO NON CONFERMATO"; break;
    }
    drawCenteredText(d,287,result,RADAR_COLOR_YELLOW,2);
    if (status == SecureMotorStatus::Accepted)
      drawCenteredText(d,315,"VIBRAZIONE DA VERIFICARE",RADAR_COLOR_WHITE,1);
    drawCenteredText(d,355,"TOCCA QUI: PAGINA",RADAR_COLOR_GRAY,2);
    d->fillRoundRect(207,432,52,5,2,RADAR_COLOR_GRID_MID);
    flushDisplay();
    return;
  }
  switch(page%DEBUG_PAGE_COUNT) {
    case 0: title="SISTEMA"; snprintf(a,sizeof(a),"BUSSOLA: %s",snap.bmm_online?"OK":"OFF"); snprintf(b,sizeof(b),"IMU: %s",snap.imu_online?"OK":"OFF"); snprintf(c,sizeof(c),"PMIC: %s",snap.pmic_online?"OK":"OFF"); break;
    case 1: title="BUSSOLA"; snprintf(a,sizeof(a),"HEADING %.0f",snap.heading_deg); snprintf(b,sizeof(b),"CAMPO %.1f uT",snap.mag_b_ut); snprintf(c,sizeof(c),"%s %s",snap.mag_calibrated?"CAL":"RAW",(snap.mag_b_ut>=15 && snap.mag_b_ut<=120)?"":"FUORI RANGE"); break;
    case 2: { title="MAG RAW"; const auto &m=sensorsGetStatus().bmm; snprintf(a,sizeof(a),"X %.1f uT",m.raw_x_ut); snprintf(b,sizeof(b),"Y %.1f uT",m.raw_y_ut); snprintf(c,sizeof(c),"Z %.1f uT",m.raw_z_ut); snprintf(e,sizeof(e),"CAMPO %.1f uT",m.raw_b_norm_ut); break; }
    case 3: title="INCLINAZIONE"; snprintf(a,sizeof(a),"ROLL %.1f",snap.roll_deg); snprintf(b,sizeof(b),"PITCH %.1f",snap.pitch_deg); snprintf(c,sizeof(c),"ACC Z %.2f g",snap.acc_z_g); break;
    case 4: {
      title="GPS";
      const bool usable=radarUsableFix(appRadarFix(snap));
      const char *source=snap.gps_uart_active ? (snap.gps_i2c_online ? "I2C+UART" : "UART") :
                          snap.gps_i2c_online ? "I2C" : "--";
      snprintf(a,sizeof(a),"%s  %s",source,
               snap.gps_status==RADAR_GPS_VALID_FIX ? "FIX" : resolveGpsStatusShort(snap.gps_status));
      snprintf(b,sizeof(b),"%s",usable ? "FIX VALIDO" : "FIX NON UTILIZZABILE");
      if (snap.gps_ever_had_fix) snprintf(c,sizeof(c),"ETA FIX %.1f s",double(snap.gps_fix_age_ms)/1000.0);
      else snprintf(c,sizeof(c),"ETA FIX --");
      if (isfinite(snap.gps_hdop) && snap.gps_hdop>0 && snap.gps_hdop<99.9f)
        snprintf(e,sizeof(e),"QUAL %u  HDOP %.1f",unsigned(snap.gps_fix_quality),double(snap.gps_hdop));
      else snprintf(e,sizeof(e),"QUAL %u  HDOP --",unsigned(snap.gps_fix_quality));
      snprintf(detail,sizeof(detail),"SAT %u / %u",unsigned(snap.gps_sats_in_use),unsigned(snap.gps_sats_in_view));
      break;
    }
    case 5: title="BATTERIA"; if(snap.battery_present && snap.batt_percent>=0) snprintf(a,sizeof(a),"%d %%",snap.batt_percent); else snprintf(a,sizeof(a),"NON RILEVATA"); snprintf(b,sizeof(b),"%u mV",snap.batt_mv); snprintf(c,sizeof(c),"%s",snap.is_charging?"IN CARICA":"NON IN CARICA"); break;
    case 6: title="RTC"; snprintf(a,sizeof(a),"%s",snap.rtc_online?"ONLINE":"OFFLINE"); snprintf(b,sizeof(b),"%.19s",snap.rtc_datetime); break;
    case 7: title="BUS I2C"; snprintf(a,sizeof(a),"DISPOSITIVI %u",snap.i2c_found_count); snprintf(b,sizeof(b),"ERRORI %lu",(unsigned long)snap.i2c_err_count); snprintf(c,sizeof(c),"BMM 0x%02X",snap.bmm_addr); break;
    case 8: title="TOUCH E TASTO"; snprintf(a,sizeof(a),"TOUCH %s",snap.touch_online?"OK":"OFF"); snprintf(b,sizeof(b),"XY %d %d",snap.touch_x,snap.touch_y); snprintf(c,sizeof(c),"TASTO %s",snap.btn_pressed?"PREMUTO":"RILASCIATO"); break;
    case 9: title="VIBRAZIONE"; snprintf(a,sizeof(a),"MOTORE %s",snap.motor_active?"ON":"OFF"); snprintf(b,sizeof(b),"IMPULSI %lu",(unsigned long)snap.motor_activations); snprintf(c,sizeof(c),"TIENI: TEST"); break;
    case 10: title="MEMORIA"; snprintf(a,sizeof(a),"RAM %lu KB",(unsigned long)(ESP.getFreeHeap()/1024)); snprintf(b,sizeof(b),"PSRAM %lu KB",(unsigned long)(ESP.getFreePsram()/1024)); snprintf(c,sizeof(c),"UPTIME %lu s",(unsigned long)(snap.uptime_ms/1000)); break;
    case 11: {
      title="WIFI E SFERE";
      snprintf(a,sizeof(a),"WIFI %s CH %u",wifiServiceStatusText(),beaconServiceChannel());
      snprintf(b,sizeof(b),"BEACON RF %u/7",beaconServiceFreshCount());
      snprintf(c,sizeof(c),"%s",secureLinkLabel(beaconServiceSecureLinkStatus()));
      const auto signal=beaconServiceProximity();
      if (signal.seen) {
        snprintf(e,sizeof(e),"RAW %d FILT %.1f dBm",int(signal.raw),double(signal.filtered));
        if (signal.fresh)
          snprintf(detail,sizeof(detail),"ETA %.1fs %s %u/%u",double(signal.age_ms)/1000.0,
                   signalQualityLabel(signal.quality),unsigned(signal.level),unsigned(kProximityMaxLevel));
        else snprintf(detail,sizeof(detail),"ETA %.1fs SCADUTO",double(signal.age_ms)/1000.0);
      } else snprintf(e,sizeof(e),"ATTESA RSSI DRA1");
      break;
    }
  }
  drawCenteredText(d,120,title,RADAR_COLOR_GRID_BRIGHT,2);
  if(a[0]) drawCenteredText(d,165,a,RADAR_COLOR_WHITE,2);
  if(b[0]) drawCenteredText(d,205,b,RADAR_COLOR_WHITE,2);
  if(c[0]) drawCenteredText(d,245,c,RADAR_COLOR_WHITE,2);
  if(e[0]) drawCenteredText(d,285,e,RADAR_COLOR_YELLOW,2);
  if(detail[0]) drawCenteredText(d,320,detail,RADAR_COLOR_WHITE,2);
  drawCenteredText(d,355,"TOCCA: PAGINA",RADAR_COLOR_GRAY,2);
  d->fillRoundRect(207,432,52,5,2,RADAR_COLOR_GRID_MID); flushDisplay();
}
