#include "sensors_module.h"

#include <Preferences.h>
#include <math.h>
#include <string.h>
#include "SensorQMI8658.hpp"

#if __has_include("display_module.h")
#include "display_module.h"
#define SENSORS_HAS_DISPLAY_MODULE 1
#else
#define SENSORS_HAS_DISPLAY_MODULE 0
#endif

#if __has_include("peripherals_module.h")
#include "peripherals_module.h"
#define SENSORS_HAS_PERIPHERALS_MODULE 1
#else
#define SENSORS_HAS_PERIPHERALS_MODULE 0
#endif

namespace {

BMM150 s_bmm;
SensorQMI8658 s_qmi;
SensorsStatus s_status;

uint32_t s_last_bmm_poll_ms = 0;
uint32_t s_last_bmm_reprobe_ms = 0;
uint32_t s_last_imu_poll_ms = 0;
bool s_imu_filter_primed = false;

// Callback I2C e HAL custom per SensorQMI8658:
// operano sul bus Wire condiviso tramite i2c_bus senza MAI richiamare Wire.begin()
bool qmiI2cCallback(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len, bool writeReg, bool isWrite) {
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

uint32_t qmiHalCallback(SensorCommCustomHal::Operation op, void *param1, void *param2) {
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

// Finestra mobile per le statistiche di stabilità del BMM150 (TEST 5)
struct MagWindowSample {
  float x_ut;
  float y_ut;
  float z_ut;
  float b_ut;
  float heading_deg;
  uint32_t timestamp_ms;
};

MagWindowSample s_mag_win[SENSORS_STATS_WINDOW_SIZE];
uint8_t s_mag_win_head = 0;
uint8_t s_mag_win_count = 0;

// Finestra mobile per la stabilità di Roll/Pitch del QMI8658 (TEST 9)
float s_roll_win[SENSORS_STATS_WINDOW_SIZE];
float s_pitch_win[SENSORS_STATS_WINDOW_SIZE];
uint8_t s_imu_win_head = 0;
uint8_t s_imu_win_count = 0;

const char * const kCardinalNames[8] = {
  "N", "NE", "E", "SE", "S", "SW", "W", "NW"
};

float normalizeHeadingDeg(float deg) {
  if (isnan(deg) || isinf(deg)) {
    return 0.0f;
  }
  while (deg < 0.0f) {
    deg += 360.0f;
  }
  while (deg >= 360.0f) {
    deg -= 360.0f;
  }
  return deg;
}

float circularDiffSignedDeg(float a_deg, float b_deg) {
  float diff = a_deg - b_deg;
  while (diff > 180.0f) {
    diff -= 360.0f;
  }
  while (diff < -180.0f) {
    diff += 360.0f;
  }
  return diff;
}

float circularDiffAbsDeg(float a_deg, float b_deg) {
  return fabsf(circularDiffSignedDeg(a_deg, b_deg));
}

const char *headingToCardinal(float deg) {
  float norm = normalizeHeadingDeg(deg);
  int idx = (int)floorf((norm + 22.5f) / 45.0f) & 7;
  return kCardinalNames[idx];
}

void computeCalibrationParameters(MagCalibrationData &cal) {
  cal.span_x = cal.max_x - cal.min_x;
  cal.span_y = cal.max_y - cal.min_y;
  cal.span_z = cal.max_z - cal.min_z;

  // Hard-Iron offset: centro dell'ellissoide su ciascun asse
  cal.offset_x = (cal.max_x + cal.min_x) * 0.5f;
  cal.offset_y = (cal.max_y + cal.min_y) * 0.5f;
  cal.offset_z = (cal.max_z + cal.min_z) * 0.5f;

  // Semi-escursioni (raggi) per la compensazione Soft-Iron
  float rx = cal.span_x * 0.5f;
  float ry = cal.span_y * 0.5f;
  float rz = cal.span_z * 0.5f;

  // Soglia minima di escursione per evitare divisione per zero (almeno 10 uT picco-picco = 5 uT raggio)
  const float kMinRadiusUt = 5.0f;
  bool x_ok = (rx >= kMinRadiusUt);
  bool y_ok = (ry >= kMinRadiusUt);
  bool z_ok = (rz >= kMinRadiusUt);

  if (x_ok && y_ok && z_ok) {
    float avg_r = (rx + ry + rz) / 3.0f;
    cal.scale_x = avg_r / rx;
    cal.scale_y = avg_r / ry;
    cal.scale_z = avg_r / rz;
  } else if (x_ok && y_ok) {
    // Rotazione prevalentemente piana (2D): compensa X/Y e protegge Z da div/0
    float avg_r = (rx + ry) * 0.5f;
    cal.scale_x = avg_r / rx;
    cal.scale_y = avg_r / ry;
    cal.scale_z = 1.0f;
    cal.offset_z = 0.0f;
  } else {
    cal.scale_x = 1.0f;
    cal.scale_y = 1.0f;
    cal.scale_z = 1.0f;
  }

  // Clamp di sicurezza dei fattori di scala soft-iron in [0.4, 2.5]
  if (cal.scale_x < 0.4f) cal.scale_x = 0.4f;
  if (cal.scale_x > 2.5f) cal.scale_x = 2.5f;
  if (cal.scale_y < 0.4f) cal.scale_y = 0.4f;
  if (cal.scale_y > 2.5f) cal.scale_y = 2.5f;
  if (cal.scale_z < 0.4f) cal.scale_z = 0.4f;
  if (cal.scale_z > 2.5f) cal.scale_z = 2.5f;
}

void updateTiltCompensatedHeading() {
  if (!s_status.bmm.online) {
    return;
  }

  // Trasformazione dal frame BMM150 al body frame QMI8658 del radar:
  // Vista frontale display AMOLED: +X_body = Alto (ore 12), +Y_body = Destra (ore 3), +Z_body = Entrante (retro PCB)
  float mx = s_status.bmm.x_ut;
  float my = s_status.bmm.y_ut;
  float mz = s_status.bmm.z_ut;

  if (s_status.axis_map.bmm_swap_xy) {
    float tmp = mx;
    mx = my;
    my = tmp;
  }
  float bx = mx * (float)s_status.axis_map.bmm_to_body_sign_x;
  float by = my * (float)s_status.axis_map.bmm_to_body_sign_y;
  float bz = mz * (float)s_status.axis_map.bmm_to_body_sign_z;

  if (!s_status.imu.online) {
    s_status.bmm.tilt_compensated = false;
    s_status.bmm.heading_tilt_deg = s_status.bmm.heading_cal_deg;
    s_status.bmm.heading_deg = s_status.bmm.calibrated
                                   ? s_status.bmm.heading_cal_deg
                                   : s_status.bmm.heading_raw_deg;
    s_status.bmm.cardinal_str = headingToCardinal(s_status.bmm.heading_deg);
    return;
  }

  // Angoli di assetto in radianti da QMI8658 (0 rad quando il radar è in piano)
  const float deg2rad = (float)(M_PI / 180.0);
  const float rad2deg = (float)(180.0 / M_PI);
  float phi = s_status.imu.roll_deg * deg2rad;
  float theta = s_status.imu.pitch_deg * deg2rad;

  float cos_phi = cosf(phi);
  float sin_phi = sinf(phi);
  float cos_theta = cosf(theta);
  float sin_theta = sinf(theta);

  // Proiezione del vettore campo magnetico (bx, by, bz) sul piano orizzontale locale:
  // Quando roll = 0 e pitch = 0 -> Bx_h = bx, By_h = by, coincidendo con l'heading 2D.
  float bx_h = bx * cos_theta + bz * sin_theta;
  float by_h = bx * sin_phi * sin_theta + by * cos_phi - bz * sin_phi * cos_theta;

  float tilt_hdg = atan2f(by_h, bx_h) * rad2deg;
  s_status.bmm.heading_tilt_deg = normalizeHeadingDeg(tilt_hdg);
  s_status.bmm.tilt_compensated = true;
  s_status.bmm.heading_deg = s_status.bmm.heading_tilt_deg;
  s_status.bmm.cardinal_str = headingToCardinal(s_status.bmm.heading_deg);
}

void updateMagWindowStats() {
  if (s_mag_win_count == 0) {
    return;
  }

  float sum_x = 0.0f, sum_y = 0.0f, sum_z = 0.0f, sum_b = 0.0f;
  float sum_sin = 0.0f, sum_cos = 0.0f;
  float min_b = s_mag_win[0].b_ut;
  float max_b = s_mag_win[0].b_ut;
  const float deg2rad = (float)(M_PI / 180.0);
  const float rad2deg = (float)(180.0 / M_PI);

  for (uint8_t i = 0; i < s_mag_win_count; ++i) {
    const MagWindowSample &s = s_mag_win[i];
    sum_x += s.x_ut;
    sum_y += s.y_ut;
    sum_z += s.z_ut;
    sum_b += s.b_ut;
    if (s.b_ut < min_b) min_b = s.b_ut;
    if (s.b_ut > max_b) max_b = s.b_ut;
    float r = s.heading_deg * deg2rad;
    sum_sin += sinf(r);
    sum_cos += cosf(r);
  }

  float inv_n = 1.0f / (float)s_mag_win_count;
  float mean_x = sum_x * inv_n;
  float mean_y = sum_y * inv_n;
  float mean_z = sum_z * inv_n;
  float mean_b = sum_b * inv_n;
  float mean_hdg = normalizeHeadingDeg(atan2f(sum_sin * inv_n, sum_cos * inv_n) * rad2deg);

  float var_x = 0.0f, var_y = 0.0f, var_z = 0.0f, var_b = 0.0f, var_hdg = 0.0f;
  float min_dh = 0.0f, max_dh = 0.0f;

  for (uint8_t i = 0; i < s_mag_win_count; ++i) {
    const MagWindowSample &s = s_mag_win[i];
    float dx = s.x_ut - mean_x;
    float dy = s.y_ut - mean_y;
    float dz = s.z_ut - mean_z;
    float db = s.b_ut - mean_b;
    float dh = circularDiffSignedDeg(s.heading_deg, mean_hdg);
    var_x += dx * dx;
    var_y += dy * dy;
    var_z += dz * dz;
    var_b += db * db;
    var_hdg += dh * dh;
    if (i == 0 || dh < min_dh) min_dh = dh;
    if (i == 0 || dh > max_dh) max_dh = dh;
  }

  s_status.bmm.std_x_ut = sqrtf(var_x * inv_n);
  s_status.bmm.std_y_ut = sqrtf(var_y * inv_n);
  s_status.bmm.std_z_ut = sqrtf(var_z * inv_n);
  s_status.bmm.std_b_ut = sqrtf(var_b * inv_n);
  s_status.bmm.p2p_b_ut = max_b - min_b;
  s_status.bmm.std_heading_deg = sqrtf(var_hdg * inv_n);
  s_status.bmm.p2p_heading_deg = max_dh - min_dh;

  if (s_mag_win_count >= 2) {
    uint8_t newest_idx = (s_mag_win_head + SENSORS_STATS_WINDOW_SIZE - 1) % SENSORS_STATS_WINDOW_SIZE;
    uint8_t oldest_idx = (s_mag_win_count < SENSORS_STATS_WINDOW_SIZE) ? 0 : s_mag_win_head;
    uint32_t dt_ms = s_mag_win[newest_idx].timestamp_ms - s_mag_win[oldest_idx].timestamp_ms;
    if (dt_ms > 0) {
      s_status.bmm.update_rate_hz = ((float)(s_mag_win_count - 1) * 1000.0f) / (float)dt_ms;
    }
  }
}

void updateImuWindowStats() {
  if (s_imu_win_count == 0) {
    return;
  }
  float sum_r = 0.0f, sum_p = 0.0f;
  for (uint8_t i = 0; i < s_imu_win_count; ++i) {
    sum_r += s_roll_win[i];
    sum_p += s_pitch_win[i];
  }
  float inv_n = 1.0f / (float)s_imu_win_count;
  float mean_r = sum_r * inv_n;
  float mean_p = sum_p * inv_n;
  float var_r = 0.0f, var_p = 0.0f;
  for (uint8_t i = 0; i < s_imu_win_count; ++i) {
    float dr = s_roll_win[i] - mean_r;
    float dp = s_pitch_win[i] - mean_p;
    var_r += dr * dr;
    var_p += dp * dp;
  }
  s_status.imu.std_roll_deg = sqrtf(var_r * inv_n);
  s_status.imu.std_pitch_deg = sqrtf(var_p * inv_n);
}

void ensureDefaultsInitialized() {
  static bool s_defaults_ready = false;
  if (s_defaults_ready) {
    return;
  }
  memset(&s_status, 0, sizeof(s_status));
  s_status.bmm.i2c_addr = HW_ADDR_BMM150;
  s_status.bmm.cardinal_str = "N";
  s_status.cal.scale_x = 1.0f;
  s_status.cal.scale_y = 1.0f;
  s_status.cal.scale_z = 1.0f;
  s_status.imu.i2c_addr = HW_ADDR_QMI8658;
  s_status.axis_map.bmm_to_body_sign_x = 1;
  s_status.axis_map.bmm_to_body_sign_y = 1;
  s_status.axis_map.bmm_to_body_sign_z = 1;
  s_status.axis_map.bmm_swap_xy = false;
  s_status.interference.recommended_min_dist_cm = 10.0f;
  snprintf(s_status.interference.summary, sizeof(s_status.interference.summary), "NOT TESTED");
  snprintf(s_status.interference.latest.distance_label,
           sizeof(s_status.interference.latest.distance_label), "N/A");
  s_defaults_ready = true;
}

bool acquirePhaseSamples(uint16_t count,
                         uint32_t sample_delay_ms,
                         uint32_t max_phase_ms,
                         bool keep_motor_updated,
                         SensorsMotorInterferencePhaseStats &out_stats,
                         const SensorsMotorInterferencePhaseStats *ref_baseline = nullptr,
                         float *out_max_delta_b = nullptr,
                         float *out_max_delta_hdg = nullptr) {
  memset(&out_stats, 0, sizeof(out_stats));
  if (count == 0) {
    return false;
  }
  if (count > 64) {
    count = 64;
  }

  float xs[64] = {0};
  float ys[64] = {0};
  float zs[64] = {0};
  float bs[64] = {0};
  float hdgs[64] = {0};
  uint16_t got = 0;

  const float deg2rad = (float)(M_PI / 180.0);
  const float rad2deg = (float)(180.0 / M_PI);
  float sum_x = 0.0f, sum_y = 0.0f, sum_z = 0.0f, sum_b = 0.0f;
  float sum_sin = 0.0f, sum_cos = 0.0f;
  float max_delta_b = 0.0f;
  float max_delta_hdg = 0.0f;

  uint32_t t_start = millis();
  for (uint16_t i = 0; i < count; ++i) {
    if (max_phase_ms > 0 && (millis() - t_start) >= max_phase_ms) {
      break;
    }
#if SENSORS_HAS_PERIPHERALS_MODULE
    if (keep_motor_updated) {
      motorUpdate();
    }
#else
    (void)keep_motor_updated;
#endif
    qmi8658Update();
    if (bmm150Update() && s_status.bmm.online) {
      float x = s_status.bmm.x_ut;
      float y = s_status.bmm.y_ut;
      float z = s_status.bmm.z_ut;
      float b = s_status.bmm.b_norm_ut;
      float h = s_status.bmm.heading_deg;

      xs[got] = x;
      ys[got] = y;
      zs[got] = z;
      bs[got] = b;
      hdgs[got] = h;

      sum_x += x;
      sum_y += y;
      sum_z += z;
      sum_b += b;
      sum_sin += sinf(h * deg2rad);
      sum_cos += cosf(h * deg2rad);

      if (got == 0) {
        out_stats.min_b_ut = b;
        out_stats.max_b_ut = b;
        out_stats.peak_x_ut = x;
        out_stats.peak_y_ut = y;
        out_stats.peak_z_ut = z;
        out_stats.peak_b_ut = b;
      } else {
        if (b < out_stats.min_b_ut) out_stats.min_b_ut = b;
        if (b > out_stats.max_b_ut) out_stats.max_b_ut = b;
        if (fabsf(x) > fabsf(out_stats.peak_x_ut)) out_stats.peak_x_ut = x;
        if (fabsf(y) > fabsf(out_stats.peak_y_ut)) out_stats.peak_y_ut = y;
        if (fabsf(z) > fabsf(out_stats.peak_z_ut)) out_stats.peak_z_ut = z;
        if (b > out_stats.peak_b_ut) out_stats.peak_b_ut = b;
      }

      if (ref_baseline != nullptr && ref_baseline->samples > 0) {
        float db = fabsf(b - ref_baseline->mean_b_ut);
        float dh = circularDiffAbsDeg(h, ref_baseline->mean_heading_deg);
        if (db > max_delta_b) max_delta_b = db;
        if (dh > max_delta_hdg) max_delta_hdg = dh;
        if (fabsf(x - ref_baseline->mean_x_ut) > fabsf(out_stats.peak_x_ut - ref_baseline->mean_x_ut)) {
          out_stats.peak_x_ut = x;
        }
        if (fabsf(y - ref_baseline->mean_y_ut) > fabsf(out_stats.peak_y_ut - ref_baseline->mean_y_ut)) {
          out_stats.peak_y_ut = y;
        }
        if (fabsf(z - ref_baseline->mean_z_ut) > fabsf(out_stats.peak_z_ut - ref_baseline->mean_z_ut)) {
          out_stats.peak_z_ut = z;
        }
        if (db > fabsf(out_stats.peak_b_ut - ref_baseline->mean_b_ut)) {
          out_stats.peak_b_ut = b;
        }
      }

      got++;
    }
    if (i + 1 < count) {
      delay(sample_delay_ms);
    }
  }

  out_stats.samples = got;
  if (got == 0) {
    return false;
  }

  float inv_n = 1.0f / (float)got;
  out_stats.mean_x_ut = sum_x * inv_n;
  out_stats.mean_y_ut = sum_y * inv_n;
  out_stats.mean_z_ut = sum_z * inv_n;
  out_stats.mean_b_ut = sum_b * inv_n;
  out_stats.mean_heading_deg =
      normalizeHeadingDeg(atan2f(sum_sin * inv_n, sum_cos * inv_n) * rad2deg);

  float var_x = 0.0f, var_y = 0.0f, var_z = 0.0f, var_b = 0.0f, var_h = 0.0f;
  for (uint16_t i = 0; i < got; ++i) {
    float dx = xs[i] - out_stats.mean_x_ut;
    float dy = ys[i] - out_stats.mean_y_ut;
    float dz = zs[i] - out_stats.mean_z_ut;
    float db = bs[i] - out_stats.mean_b_ut;
    float dh = circularDiffSignedDeg(hdgs[i], out_stats.mean_heading_deg);
    var_x += dx * dx;
    var_y += dy * dy;
    var_z += dz * dz;
    var_b += db * db;
    var_h += dh * dh;
  }

  out_stats.std_x_ut = sqrtf(var_x * inv_n);
  out_stats.std_y_ut = sqrtf(var_y * inv_n);
  out_stats.std_z_ut = sqrtf(var_z * inv_n);
  out_stats.std_b_ut = sqrtf(var_b * inv_n);
  out_stats.std_heading_deg = sqrtf(var_h * inv_n);

  if (out_max_delta_b != nullptr) {
    *out_max_delta_b = max_delta_b;
  }
  if (out_max_delta_hdg != nullptr) {
    *out_max_delta_hdg = max_delta_hdg;
  }
  return true;
}

} // namespace

// ============================================================================
// IMPLEMENTAZIONE API PUBBLICA
// ============================================================================

bool sensorsInit() {
  ensureDefaultsInitialized();
  if (!i2cBusIsInitialized()) {
    i2cBusInit();
  }

  // Carica eventuale calibrazione salvata in NVS ("dr_mag_cal")
  magCalLoadFromNvs();

  bool imu_ok = qmi8658Init();
  bool bmm_ok = bmm150Init();

  if (bmm_ok) {
    bmm150Update();
  }
  if (imu_ok) {
    qmi8658Update();
  }
  return (imu_ok || bmm_ok);
}

void sensorsUpdate() {
  ensureDefaultsInitialized();
  uint32_t now = millis();

  if (now - s_last_imu_poll_ms >= SENSORS_IMU_POLL_INTERVAL_MS) {
    s_last_imu_poll_ms = now;
    qmi8658Update();
  }

  if (now - s_last_bmm_poll_ms >= SENSORS_BMM_POLL_INTERVAL_MS) {
    s_last_bmm_poll_ms = now;
    bmm150Update();
  }
}

const SensorsStatus &sensorsGetStatus() {
  ensureDefaultsInitialized();
  return s_status;
}

void sensorsFillRadarSnapshot(RadarSystemSnapshot &snap) {
#if SENSORS_HAS_DISPLAY_MODULE
  ensureDefaultsInitialized();

  // Bussola BMM150
  snap.bmm_online = s_status.bmm.online;
  snap.bmm_addr = s_status.bmm.i2c_addr;
  snap.bmm_chip_id = s_status.bmm.chip_id;
  snap.mag_x_ut = s_status.bmm.x_ut;
  snap.mag_y_ut = s_status.bmm.y_ut;
  snap.mag_z_ut = s_status.bmm.z_ut;
  snap.mag_b_ut = s_status.bmm.b_norm_ut;
  snap.mag_noise_ut = s_status.bmm.std_b_ut;
  snap.mag_rate_hz = s_status.bmm.update_rate_hz;
  snap.heading_raw_deg = s_status.bmm.heading_raw_deg;
  snap.heading_cal_deg = s_status.bmm.heading_cal_deg;
  snap.heading_tilt_deg = s_status.bmm.heading_tilt_deg;
  snap.heading_deg = s_status.bmm.heading_deg;
  snap.mag_calibrated = s_status.bmm.calibrated;
  snap.tilt_compensated = s_status.bmm.tilt_compensated;
  snap.cal_offset_x = s_status.cal.offset_x;
  snap.cal_offset_y = s_status.cal.offset_y;
  snap.cal_offset_z = s_status.cal.offset_z;
  snap.cal_scale_x = s_status.cal.scale_x;
  snap.cal_scale_y = s_status.cal.scale_y;
  snap.cal_scale_z = s_status.cal.scale_z;

  // Calibrazione in corso (Overlay)
  snap.cal_in_progress = s_status.cal.in_progress;
  snap.cal_min_x = s_status.cal.min_x;
  snap.cal_min_y = s_status.cal.min_y;
  snap.cal_min_z = s_status.cal.min_z;
  snap.cal_max_x = s_status.cal.max_x;
  snap.cal_max_y = s_status.cal.max_y;
  snap.cal_max_z = s_status.cal.max_z;
  snap.cal_samples = s_status.cal.sample_count;
  snap.cal_elapsed_ms = s_status.cal.elapsed_ms;

  // IMU QMI8658
  snap.imu_online = s_status.imu.online;
  snap.acc_x_g = s_status.imu.acc_x_g;
  snap.acc_y_g = s_status.imu.acc_y_g;
  snap.acc_z_g = s_status.imu.acc_z_g;
  snap.gyr_x_dps = s_status.imu.gyr_x_dps;
  snap.gyr_y_dps = s_status.imu.gyr_y_dps;
  snap.gyr_z_dps = s_status.imu.gyr_z_dps;
  snap.roll_deg = s_status.imu.roll_deg;
  snap.pitch_deg = s_status.imu.pitch_deg;

  // Interferenza Motorino (TEST 6)
  snap.interference_tested = s_status.interference.tested;
  snap.interf_base_b_ut = s_status.interference.latest.phase_a_off.mean_b_ut;
  snap.interf_on_b_ut = s_status.interference.latest.phase_b_on.mean_b_ut;
  snap.interf_delta_b_ut = s_status.interference.latest.delta_b_peak_ut;
  snap.interf_delta_heading_deg = s_status.interference.latest.delta_heading_peak_deg;
  snap.interf_post_delta_b_ut = s_status.interference.latest.post_residual_b_ut;
  strncpy(snap.interf_distance_label,
          s_status.interference.latest.distance_label,
          sizeof(snap.interf_distance_label) - 1);
  snap.interf_distance_label[sizeof(snap.interf_distance_label) - 1] = '\0';
#else
  (void)snap;
#endif
}

// ============================================================================
// TEST 5 - BUSSOLA BMM150 (0x13, fallback 0x11)
// ============================================================================

bool bmm150Init() {
  ensureDefaultsInitialized();
  if (!i2cBusIsInitialized() && !i2cBusInit()) {
    s_status.bmm.online = false;
    return false;
  }
  if (!i2cBusLock(250)) {
    s_status.bmm.online = false;
    s_status.bmm.error_count++;
    return false;
  }

  const uint8_t candidate_addrs[2] = {HW_ADDR_BMM150, HW_ADDR_BMM150_ALT};
  bool found = false;

  for (uint8_t i = 0; i < 2; ++i) {
    uint8_t addr = candidate_addrs[i];
    int8_t rc = s_bmm.initialize(addr);
    if (rc == BMM150_OK && s_bmm.get_chip_id() == BMM150_CHIP_ID) {
      // Configura Normal Mode + High Accuracy / Regular e ODR = 25 Hz per lettura fluida a ~20-25 Hz
      s_bmm.set_op_mode(BMM150_NORMAL_MODE);
      s_bmm.set_presetmode(BMM150_PRESETMODE_REGULAR);
      s_bmm.settings.data_rate = BMM150_DATA_RATE_25HZ;
      s_bmm.set_odr(s_bmm.settings);

      s_status.bmm.online = true;
      s_status.bmm.i2c_addr = addr;
      s_status.bmm.chip_id = s_bmm.get_chip_id();
      s_status.bmm.op_mode = BMM150_NORMAL_MODE;
      s_status.bmm.preset_mode = BMM150_PRESETMODE_REGULAR;
      s_status.bmm.odr_code = BMM150_DATA_RATE_25HZ;

      // Copia i registri di trim Bosch
      s_status.bmm.trim.dig_x1 = s_bmm.trim_data.dig_x1;
      s_status.bmm.trim.dig_y1 = s_bmm.trim_data.dig_y1;
      s_status.bmm.trim.dig_x2 = s_bmm.trim_data.dig_x2;
      s_status.bmm.trim.dig_y2 = s_bmm.trim_data.dig_y2;
      s_status.bmm.trim.dig_z1 = s_bmm.trim_data.dig_z1;
      s_status.bmm.trim.dig_z2 = s_bmm.trim_data.dig_z2;
      s_status.bmm.trim.dig_z3 = s_bmm.trim_data.dig_z3;
      s_status.bmm.trim.dig_z4 = s_bmm.trim_data.dig_z4;
      s_status.bmm.trim.dig_xy1 = s_bmm.trim_data.dig_xy1;
      s_status.bmm.trim.dig_xy2 = s_bmm.trim_data.dig_xy2;
      s_status.bmm.trim.dig_xyz1 = s_bmm.trim_data.dig_xyz1;
      s_status.bmm.trim.trim_valid =
          (s_bmm.trim_data.dig_xyz1 != 0 && s_bmm.trim_data.dig_z1 != 0 && s_bmm.trim_data.dig_z2 != 0);

      s_mag_win_head = 0;
      s_mag_win_count = 0;
      found = true;
      break;
    }
  }

  if (!found) {
    s_status.bmm.online = false;
    s_status.bmm.chip_id = s_bmm.get_chip_id();
  }

  i2cBusUnlock();
  return found;
}

bool bmm150Update() {
  ensureDefaultsInitialized();

  // Se il BMM150 non era presente al boot, riprova in modo leggero ogni 2000 ms
  // (utile se i cavi su TP2/TP3 vengono mossi o ricollegati durante il test)
  if (!s_status.bmm.online) {
    uint32_t now = millis();
    if (now - s_last_bmm_reprobe_ms < 2000) {
      return false;
    }
    s_last_bmm_reprobe_ms = now;
    if (!i2cProbeAddress(HW_ADDR_BMM150) && !i2cProbeAddress(HW_ADDR_BMM150_ALT)) {
      return false;
    }
    if (!bmm150Init()) {
      return false;
    }
  }

  if (!i2cBusLock(60)) {
    s_status.bmm.error_count++;
    return false;
  }

  s_bmm.read_mag_data();
  bool i2c_ok = s_bmm.last_i2c_ok();
  i2cBusUnlock();

  if (!i2c_ok) {
    s_status.bmm.error_count++;
    // Se falliscono 5 letture consecutive, marca offline per forzare una re-init pulita
    if (s_status.bmm.error_count >= 5 && !i2cProbeAddress(s_status.bmm.i2c_addr)) {
      s_status.bmm.online = false;
    }
    return false;
  }

  uint32_t now = millis();
  s_status.bmm.raw_x = s_bmm.raw_mag_data.raw_datax;
  s_status.bmm.raw_y = s_bmm.raw_mag_data.raw_datay;
  s_status.bmm.raw_z = s_bmm.raw_mag_data.raw_dataz;
  s_status.bmm.raw_rhall = s_bmm.raw_mag_data.raw_data_r;

  s_status.bmm.comp_x_int_ut = s_bmm.mag_data.x;
  s_status.bmm.comp_y_int_ut = s_bmm.mag_data.y;
  s_status.bmm.comp_z_int_ut = s_bmm.mag_data.z;

  float rx_ut = s_bmm.mag_data_float.x;
  float ry_ut = s_bmm.mag_data_float.y;
  float rz_ut = s_bmm.mag_data_float.z;

  bool overflow = (s_status.bmm.raw_x == BMM150_XYAXES_FLIP_OVERFLOW_ADCVAL) ||
                  (s_status.bmm.raw_y == BMM150_XYAXES_FLIP_OVERFLOW_ADCVAL) ||
                  (s_status.bmm.raw_z == BMM150_ZAXIS_HALL_OVERFLOW_ADCVAL) ||
                  (s_status.bmm.raw_rhall == 0);
  s_status.bmm.overflow_detected = overflow;
  if (overflow) {
    s_status.bmm.error_count++;
    return false;
  }

  s_status.bmm.raw_x_ut = rx_ut;
  s_status.bmm.raw_y_ut = ry_ut;
  s_status.bmm.raw_z_ut = rz_ut;
  s_status.bmm.raw_b_norm_ut = sqrtf(rx_ut * rx_ut + ry_ut * ry_ut + rz_ut * rz_ut);

  // Aggiornamento acquisizione calibrazione magnetometrica (se in corso)
  if (s_status.cal.in_progress) {
    if (s_status.cal.sample_count == 0) {
      s_status.cal.min_x = rx_ut;
      s_status.cal.max_x = rx_ut;
      s_status.cal.min_y = ry_ut;
      s_status.cal.max_y = ry_ut;
      s_status.cal.min_z = rz_ut;
      s_status.cal.max_z = rz_ut;
    } else {
      if (rx_ut < s_status.cal.min_x) s_status.cal.min_x = rx_ut;
      if (rx_ut > s_status.cal.max_x) s_status.cal.max_x = rx_ut;
      if (ry_ut < s_status.cal.min_y) s_status.cal.min_y = ry_ut;
      if (ry_ut > s_status.cal.max_y) s_status.cal.max_y = ry_ut;
      if (rz_ut < s_status.cal.min_z) s_status.cal.min_z = rz_ut;
      if (rz_ut > s_status.cal.max_z) s_status.cal.max_z = rz_ut;
    }
    s_status.cal.sample_count++;
    s_status.cal.elapsed_ms = now - s_status.cal.started_ms;
    computeCalibrationParameters(s_status.cal);
  }

  // Applica correzione Hard-Iron e Soft-Iron se la calibrazione è attiva
  s_status.bmm.calibrated = s_status.cal.valid;
  if (s_status.cal.valid) {
    s_status.bmm.x_ut = (rx_ut - s_status.cal.offset_x) * s_status.cal.scale_x;
    s_status.bmm.y_ut = (ry_ut - s_status.cal.offset_y) * s_status.cal.scale_y;
    s_status.bmm.z_ut = (rz_ut - s_status.cal.offset_z) * s_status.cal.scale_z;
  } else {
    s_status.bmm.x_ut = rx_ut;
    s_status.bmm.y_ut = ry_ut;
    s_status.bmm.z_ut = rz_ut;
  }

  float cx = s_status.bmm.x_ut;
  float cy = s_status.bmm.y_ut;
  float cz = s_status.bmm.z_ut;
  s_status.bmm.b_norm_ut = sqrtf(cx * cx + cy * cy + cz * cz);
  s_status.bmm.field_plausible =
      (s_status.bmm.b_norm_ut >= 15.0f && s_status.bmm.b_norm_ut <= 120.0f);

  const float rad2deg = (float)(180.0 / M_PI);
  s_status.bmm.heading_raw_deg = normalizeHeadingDeg(atan2f(ry_ut, rx_ut) * rad2deg);
  s_status.bmm.heading_cal_deg = normalizeHeadingDeg(atan2f(cy, cx) * rad2deg);

  // Calcola heading compensato per inclinazione tramite QMI8658
  updateTiltCompensatedHeading();

  s_status.bmm.sample_count++;
  s_status.bmm.last_update_ms = now;

  // Inserisci campione nella finestra mobile di stabilità
  MagWindowSample &slot = s_mag_win[s_mag_win_head];
  slot.x_ut = cx;
  slot.y_ut = cy;
  slot.z_ut = cz;
  slot.b_ut = s_status.bmm.b_norm_ut;
  slot.heading_deg = s_status.bmm.heading_deg;
  slot.timestamp_ms = now;
  s_mag_win_head = (s_mag_win_head + 1) % SENSORS_STATS_WINDOW_SIZE;
  if (s_mag_win_count < SENSORS_STATS_WINDOW_SIZE) {
    s_mag_win_count++;
  }
  updateMagWindowStats();

  return true;
}

const Bmm150Status &bmm150GetStatus() {
  ensureDefaultsInitialized();
  return s_status.bmm;
}

bool bmm150IsOnline() {
  ensureDefaultsInitialized();
  return s_status.bmm.online;
}

// ============================================================================
// CALIBRAZIONE MAGNETOMETRICA (Hard-Iron + Soft-Iron + NVS "dr_mag_cal")
// ============================================================================

bool magCalStart() {
  ensureDefaultsInitialized();
  s_status.cal.in_progress = true;
  s_status.cal.sample_count = 0;
  s_status.cal.started_ms = millis();
  s_status.cal.elapsed_ms = 0;
  s_status.cal.min_x = 0.0f;
  s_status.cal.max_x = 0.0f;
  s_status.cal.min_y = 0.0f;
  s_status.cal.max_y = 0.0f;
  s_status.cal.min_z = 0.0f;
  s_status.cal.max_z = 0.0f;
  s_status.cal.span_x = 0.0f;
  s_status.cal.span_y = 0.0f;
  s_status.cal.span_z = 0.0f;
  return true;
}

bool magCalStop(bool save_to_nvs) {
  ensureDefaultsInitialized();
  s_status.cal.in_progress = false;
  s_status.cal.elapsed_ms = millis() - s_status.cal.started_ms;

  computeCalibrationParameters(s_status.cal);

  // Richiede almeno 10 campioni e un'escursione X/Y >= 10 uT per considerare valida la calibrazione
  bool span_ok = (s_status.cal.sample_count >= 10) &&
                 (s_status.cal.span_x >= 10.0f) &&
                 (s_status.cal.span_y >= 10.0f);

  if (!span_ok) {
    return false;
  }

  s_status.cal.valid = true;
  s_status.bmm.calibrated = true;

  if (save_to_nvs) {
    magCalSaveToNvs();
  }
  return true;
}

bool magCalLoadFromNvs() {
  ensureDefaultsInitialized();
  Preferences prefs;
  if (!prefs.begin(SENSORS_NVS_NAMESPACE, true)) {
    return false;
  }

  bool is_valid = prefs.getBool("valid", false);
  if (!is_valid) {
    prefs.end();
    return false;
  }

  s_status.cal.valid = true;
  s_status.cal.loaded_from_nvs = true;
  s_status.cal.in_progress = false;
  s_status.cal.min_x = prefs.getFloat("min_x", -40.0f);
  s_status.cal.max_x = prefs.getFloat("max_x", 40.0f);
  s_status.cal.min_y = prefs.getFloat("min_y", -40.0f);
  s_status.cal.max_y = prefs.getFloat("max_y", 40.0f);
  s_status.cal.min_z = prefs.getFloat("min_z", -40.0f);
  s_status.cal.max_z = prefs.getFloat("max_z", 40.0f);
  s_status.cal.offset_x = prefs.getFloat("off_x", 0.0f);
  s_status.cal.offset_y = prefs.getFloat("off_y", 0.0f);
  s_status.cal.offset_z = prefs.getFloat("off_z", 0.0f);
  s_status.cal.scale_x = prefs.getFloat("scl_x", 1.0f);
  s_status.cal.scale_y = prefs.getFloat("scl_y", 1.0f);
  s_status.cal.scale_z = prefs.getFloat("scl_z", 1.0f);
  s_status.cal.sample_count = prefs.getUInt("samples", 0);
  prefs.end();

  s_status.cal.span_x = s_status.cal.max_x - s_status.cal.min_x;
  s_status.cal.span_y = s_status.cal.max_y - s_status.cal.min_y;
  s_status.cal.span_z = s_status.cal.max_z - s_status.cal.min_z;

  // Protezione su scale caricate da NVS
  if (isnan(s_status.cal.scale_x) || s_status.cal.scale_x < 0.2f || s_status.cal.scale_x > 5.0f) {
    s_status.cal.scale_x = 1.0f;
  }
  if (isnan(s_status.cal.scale_y) || s_status.cal.scale_y < 0.2f || s_status.cal.scale_y > 5.0f) {
    s_status.cal.scale_y = 1.0f;
  }
  if (isnan(s_status.cal.scale_z) || s_status.cal.scale_z < 0.2f || s_status.cal.scale_z > 5.0f) {
    s_status.cal.scale_z = 1.0f;
  }

  s_status.bmm.calibrated = true;
  return true;
}

bool magCalSaveToNvs() {
  ensureDefaultsInitialized();
  if (!s_status.cal.valid) {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(SENSORS_NVS_NAMESPACE, false)) {
    return false;
  }

  prefs.putBool("valid", true);
  prefs.putFloat("min_x", s_status.cal.min_x);
  prefs.putFloat("max_x", s_status.cal.max_x);
  prefs.putFloat("min_y", s_status.cal.min_y);
  prefs.putFloat("max_y", s_status.cal.max_y);
  prefs.putFloat("min_z", s_status.cal.min_z);
  prefs.putFloat("max_z", s_status.cal.max_z);
  prefs.putFloat("off_x", s_status.cal.offset_x);
  prefs.putFloat("off_y", s_status.cal.offset_y);
  prefs.putFloat("off_z", s_status.cal.offset_z);
  prefs.putFloat("scl_x", s_status.cal.scale_x);
  prefs.putFloat("scl_y", s_status.cal.scale_y);
  prefs.putFloat("scl_z", s_status.cal.scale_z);
  prefs.putUInt("samples", s_status.cal.sample_count);
  prefs.end();
  return true;
}

bool magCalClearNvs() {
  ensureDefaultsInitialized();
  Preferences prefs;
  if (prefs.begin(SENSORS_NVS_NAMESPACE, false)) {
    prefs.clear();
    prefs.end();
  }

  s_status.cal.valid = false;
  s_status.cal.loaded_from_nvs = false;
  s_status.cal.in_progress = false;
  s_status.cal.min_x = 0.0f;
  s_status.cal.max_x = 0.0f;
  s_status.cal.min_y = 0.0f;
  s_status.cal.max_y = 0.0f;
  s_status.cal.min_z = 0.0f;
  s_status.cal.max_z = 0.0f;
  s_status.cal.span_x = 0.0f;
  s_status.cal.span_y = 0.0f;
  s_status.cal.span_z = 0.0f;
  s_status.cal.offset_x = 0.0f;
  s_status.cal.offset_y = 0.0f;
  s_status.cal.offset_z = 0.0f;
  s_status.cal.scale_x = 1.0f;
  s_status.cal.scale_y = 1.0f;
  s_status.cal.scale_z = 1.0f;
  s_status.cal.sample_count = 0;
  s_status.bmm.calibrated = false;
  return true;
}

bool magCalIsCalibrated() {
  ensureDefaultsInitialized();
  return s_status.cal.valid;
}

bool magCalIsInProgress() {
  ensureDefaultsInitialized();
  return s_status.cal.in_progress;
}

const MagCalibrationData &magCalGetData() {
  ensureDefaultsInitialized();
  return s_status.cal;
}

// ============================================================================
// TEST 6 - PROCEDURA INTERFERENZE MAGNETICHE DEL MOTORINO
// ============================================================================

bool sensorsRunMotorInterferenceTest(const char *distance_label,
                                     float distance_cm,
                                     uint16_t baseline_samples,
                                     uint16_t motor_on_samples,
                                     uint16_t post_samples) {
  ensureDefaultsInitialized();

  // Sicurezza: assicura che il motorino sia spento prima di iniziare Fase A
#if SENSORS_HAS_PERIPHERALS_MODULE
  motorStop();
#endif
  pinMode(HW_MOTOR_PIN, OUTPUT);
  digitalWrite(HW_MOTOR_PIN, HW_MOTOR_SAFE_OFF_LEVEL);
  delay(80);

  if (!s_status.bmm.online) {
    bmm150Init();
  }

  SensorsMotorInterferenceStep step;
  memset(&step, 0, sizeof(step));
  strncpy(step.distance_label,
          (distance_label != nullptr) ? distance_label : "breadboard",
          sizeof(step.distance_label) - 1);
  step.distance_cm = distance_cm;
  step.timestamp_ms = millis();

  if (!s_status.bmm.online) {
    s_status.interference.tested = true;
    s_status.interference.bmm_was_online = false;
    s_status.interference.latest = step;
    snprintf(s_status.interference.summary,
             sizeof(s_status.interference.summary),
             "ABORTED: BMM150 (0x13/0x11) OFFLINE on I2C bus");
    return false;
  }

  // --------------------------------------------------------------------------
  // FASE A: Motor OFF Baseline (default 30 campioni @ ~25 ms = ~750 ms)
  // --------------------------------------------------------------------------
  bool ok_a = acquirePhaseSamples(
      baseline_samples, 25, 2500, false, step.phase_a_off, nullptr, nullptr, nullptr);

  // --------------------------------------------------------------------------
  // FASE B: Motor ON in vibrazione (default 20 campioni @ ~25 ms = ~500 ms,
  //         sempre entro il limite di sicurezza di 800 ms < 1000 ms watchdog!)
  // --------------------------------------------------------------------------
  uint32_t on_duration_ms = (uint32_t)motor_on_samples * 26U + 80U;
  if (on_duration_ms > 800U) {
    on_duration_ms = 800U;
  }

#if SENSORS_HAS_PERIPHERALS_MODULE
  motorPulseMs(on_duration_ms);
#else
  digitalWrite(HW_MOTOR_PIN, HW_MOTOR_ACTIVE_LEVEL);
#endif
  delay(40); // Spin-up meccanico del rotore eccentrico

  float max_db_on = 0.0f;
  float max_dhdg_on = 0.0f;
  bool ok_b = acquirePhaseSamples(
      motor_on_samples, 25, 720, true, step.phase_b_on,
      &step.phase_a_off, &max_db_on, &max_dhdg_on);

  // Spegnimento immediato e garantito del motorino a fine Fase B
#if SENSORS_HAS_PERIPHERALS_MODULE
  motorStop();
#endif
  digitalWrite(HW_MOTOR_PIN, HW_MOTOR_SAFE_OFF_LEVEL);
  delay(100); // Attesa arresto inerziale del rotore prima di Fase C

  // --------------------------------------------------------------------------
  // FASE C: Motor OFF Post-Vibrazione (verifica ritorno a baseline / isteresi)
  // --------------------------------------------------------------------------
  bool ok_c = acquirePhaseSamples(
      post_samples, 25, 2000, false, step.phase_c_post, nullptr, nullptr, nullptr);

  step.valid = (ok_a && ok_b && ok_c);
  if (!step.valid) {
    s_status.interference.tested = true;
    s_status.interference.bmm_was_online = false;
    s_status.interference.latest = step;
    snprintf(s_status.interference.summary,
             sizeof(s_status.interference.summary),
             "INCOMPLETE: I2C read error during motor interference test");
    return false;
  }

  // Calcolo metriche dinamiche (Fase B ON vs Fase A OFF)
  step.delta_b_mean_ut = fabsf(step.phase_b_on.mean_b_ut - step.phase_a_off.mean_b_ut);
  step.delta_b_peak_ut = (max_db_on >= step.delta_b_mean_ut) ? max_db_on : step.delta_b_mean_ut;
  step.delta_heading_mean_deg =
      circularDiffAbsDeg(step.phase_b_on.mean_heading_deg, step.phase_a_off.mean_heading_deg);
  step.delta_heading_peak_deg =
      (max_dhdg_on >= step.delta_heading_mean_deg) ? max_dhdg_on : step.delta_heading_mean_deg;

  // Calcolo metriche post-vibrazione (Fase C OFF vs Fase A OFF)
  step.post_residual_b_ut = fabsf(step.phase_c_post.mean_b_ut - step.phase_a_off.mean_b_ut);
  step.post_residual_heading_deg =
      circularDiffAbsDeg(step.phase_c_post.mean_heading_deg, step.phase_a_off.mean_heading_deg);
  step.returned_to_baseline =
      (step.post_residual_b_ut <= 2.5f && step.post_residual_heading_deg <= 3.5f);

  // Calcolo interferenza magnetica STATICA (a motore spento) rispetto allo step >= 20 cm
  if (distance_cm >= 19.5f || !s_status.interference.has_far_baseline) {
    if (distance_cm >= 19.5f) {
      s_status.interference.has_far_baseline = true;
      s_status.interference.far_baseline_b_ut = step.phase_a_off.mean_b_ut;
      s_status.interference.far_baseline_heading_deg = step.phase_a_off.mean_heading_deg;
    }
  }

  if (s_status.interference.has_far_baseline) {
    step.has_far_reference = true;
    step.static_delta_b_ut =
        fabsf(step.phase_a_off.mean_b_ut - s_status.interference.far_baseline_b_ut);
    step.static_delta_heading_deg =
        circularDiffAbsDeg(step.phase_a_off.mean_heading_deg,
                           s_status.interference.far_baseline_heading_deg);
  } else {
    step.has_far_reference = false;
    step.static_delta_b_ut = 0.0f;
    step.static_delta_heading_deg = 0.0f;
  }

  step.acceptable_for_case =
      (step.delta_heading_peak_deg <= 5.0f) &&
      (step.delta_b_peak_ut <= 5.0f) &&
      (!step.has_far_reference || step.static_delta_heading_deg <= 5.0f);

  // Salva o aggiorna lo step corrispondente nella tabella report
  uint8_t target_idx = s_status.interference.step_count;
  for (uint8_t i = 0; i < s_status.interference.step_count; ++i) {
    if (strcasecmp(s_status.interference.steps[i].distance_label, step.distance_label) == 0) {
      target_idx = i;
      break;
    }
  }
  if (target_idx >= SENSORS_INTERF_MAX_STEPS) {
    target_idx = SENSORS_INTERF_MAX_STEPS - 1;
  } else if (target_idx == s_status.interference.step_count) {
    s_status.interference.step_count++;
  }

  s_status.interference.steps[target_idx] = step;
  s_status.interference.latest_step_index = target_idx;
  s_status.interference.latest = step;
  s_status.interference.tested = true;
  s_status.interference.bmm_was_online = true;

  // Stima la distanza minima consigliata dai punti misurati
  float min_safe_cm = 10.0f;
  for (uint8_t i = 0; i < s_status.interference.step_count; ++i) {
    const SensorsMotorInterferenceStep &st = s_status.interference.steps[i];
    if (st.valid && st.acceptable_for_case && st.distance_cm > 0.0f && st.distance_cm < min_safe_cm) {
      min_safe_cm = st.distance_cm;
    } else if (st.valid && !st.acceptable_for_case && st.distance_cm >= min_safe_cm) {
      min_safe_cm = st.distance_cm + 5.0f;
    }
  }
  s_status.interference.recommended_min_dist_cm = min_safe_cm;

  snprintf(s_status.interference.summary,
           sizeof(s_status.interference.summary),
           "d=%s: dB_on=%.1fuT dHdg_on=%.1fdeg post=%.1fuT (%s)",
           step.distance_label,
           step.delta_b_peak_ut,
           step.delta_heading_peak_deg,
           step.post_residual_b_ut,
           step.acceptable_for_case ? "OK" : "HIGH_INTERF");

  return true;
}

const SensorsMotorInterferenceReport &sensorsGetInterferenceReport() {
  ensureDefaultsInitialized();
  return s_status.interference;
}

void sensorsClearInterferenceReport() {
  ensureDefaultsInitialized();
  memset(&s_status.interference, 0, sizeof(s_status.interference));
  s_status.interference.recommended_min_dist_cm = 10.0f;
  snprintf(s_status.interference.summary, sizeof(s_status.interference.summary), "NOT TESTED");
  snprintf(s_status.interference.latest.distance_label,
           sizeof(s_status.interference.latest.distance_label), "N/A");
}

// ============================================================================
// TEST 9 - IMU 6 ASSI INTEGRATA QMI8658C (0x6B) & COMPENSAZIONE INCLINAZIONE
// ============================================================================

bool qmi8658Init() {
  ensureDefaultsInitialized();
  if (!i2cBusIsInitialized() && !i2cBusInit()) {
    s_status.imu.online = false;
    return false;
  }
  if (!i2cBusLock(300)) {
    s_status.imu.online = false;
    s_status.imu.error_count++;
    return false;
  }

  bool ok = s_qmi.begin(qmiI2cCallback, qmiHalCallback, HW_ADDR_QMI8658);
  if (!ok) {
    s_status.imu.online = false;
    s_status.imu.error_count++;
    i2cBusUnlock();
    return false;
  }

  int who = s_qmi.whoAmI();
  uint8_t rev = s_qmi.getChipID();

  // Configura Accelerometro (4G, 250Hz, LPF_MODE_0) e Giroscopio (256DPS, ~250Hz = 224.2Hz, LPF_MODE_0)
  s_qmi.configAccelerometer(
      SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_250Hz, SensorQMI8658::LPF_MODE_0);
  s_qmi.configGyroscope(
      SensorQMI8658::GYR_RANGE_256DPS, SensorQMI8658::GYR_ODR_224_2Hz, SensorQMI8658::LPF_MODE_0);
  s_qmi.enableAccelerometer();
  s_qmi.enableGyroscope();

  s_status.imu.online = true;
  s_status.imu.i2c_addr = HW_ADDR_QMI8658;
  s_status.imu.who_am_i = (who >= 0) ? (uint8_t)who : 0;
  s_status.imu.revision_id = rev;
  s_imu_filter_primed = false;
  s_imu_win_head = 0;
  s_imu_win_count = 0;

  i2cBusUnlock();

  // Piccolo ritardo per il primo campione valido dal filtro interno del QMI8658
  delay(10);
  qmi8658Update();
  return true;
}

bool qmi8658Update() {
  ensureDefaultsInitialized();
  if (!s_status.imu.online) {
    return false;
  }

  if (!i2cBusLock(50)) {
    s_status.imu.error_count++;
    return false;
  }

  float ax = 0.0f, ay = 0.0f, az = 0.0f;
  float gx = 0.0f, gy = 0.0f, gz = 0.0f;
  bool acc_ok = s_qmi.getAccelerometer(ax, ay, az);
  bool gyr_ok = s_qmi.getGyroscope(gx, gy, gz);

  // Leggi temperatura interna ogni 16 campioni per ridurre il traffico I2C
  if ((s_status.imu.sample_count & 0x0F) == 0) {
    float t = s_qmi.getTemperature_C();
    if (!isnan(t)) {
      s_status.imu.temp_c = t;
    }
  }

  i2cBusUnlock();

  if (!acc_ok || !gyr_ok) {
    s_status.imu.error_count++;
    return false;
  }

  uint32_t now = millis();
  uint32_t prev_ms = s_status.imu.last_update_ms;
  float dt_sec = (prev_ms > 0 && now > prev_ms) ? ((float)(now - prev_ms) * 0.001f) : 0.02f;
  if (dt_sec > 0.001f && dt_sec < 1.0f) {
    float inst_hz = 1.0f / dt_sec;
    if (s_status.imu.update_rate_hz <= 0.1f) {
      s_status.imu.update_rate_hz = inst_hz;
    } else {
      s_status.imu.update_rate_hz = 0.85f * s_status.imu.update_rate_hz + 0.15f * inst_hz;
    }
  }

  s_status.imu.acc_x_g = ax;
  s_status.imu.acc_y_g = ay;
  s_status.imu.acc_z_g = az;
  s_status.imu.acc_norm_g = sqrtf(ax * ax + ay * ay + az * az);

  s_status.imu.gyr_x_dps = gx;
  s_status.imu.gyr_y_dps = gy;
  s_status.imu.gyr_z_dps = gz;

  // Calcolo Roll e Pitch dall'accelerometro:
  // Nel frame QMI8658 (vista frontale: +X alto, +Y destra, +Z entrante nel display),
  // quando il radar è appoggiato in piano (orizzontale), ax ~ 0, ay ~ 0, |az| ~ 1g -> roll = 0, pitch = 0.
  const float rad2deg = (float)(180.0 / M_PI);
  float roll_acc = atan2f(ay, sqrtf(ax * ax + az * az)) * rad2deg;
  float pitch_acc = atan2f(-ax, sqrtf(ay * ay + az * az)) * rad2deg;
  s_status.imu.roll_acc_deg = roll_acc;
  s_status.imu.pitch_acc_deg = pitch_acc;

  // Filtro complementare leggero accelerometro + giroscopio
  if (!s_imu_filter_primed || dt_sec > 0.25f) {
    s_status.imu.roll_deg = roll_acc;
    s_status.imu.pitch_deg = pitch_acc;
    s_imu_filter_primed = true;
  } else {
    const float kAlpha = 0.85f;
    float gyro_roll_pred = s_status.imu.roll_deg + gx * dt_sec;
    float gyro_pitch_pred = s_status.imu.pitch_deg + gy * dt_sec;
    s_status.imu.roll_deg = kAlpha * gyro_roll_pred + (1.0f - kAlpha) * roll_acc;
    s_status.imu.pitch_deg = kAlpha * gyro_pitch_pred + (1.0f - kAlpha) * pitch_acc;
  }

  s_status.imu.sample_count++;
  s_status.imu.last_update_ms = now;

  s_roll_win[s_imu_win_head] = s_status.imu.roll_deg;
  s_pitch_win[s_imu_win_head] = s_status.imu.pitch_deg;
  s_imu_win_head = (s_imu_win_head + 1) % SENSORS_STATS_WINDOW_SIZE;
  if (s_imu_win_count < SENSORS_STATS_WINDOW_SIZE) {
    s_imu_win_count++;
  }
  updateImuWindowStats();

  // Aggiorna anche l'heading tilt-compensated della bussola
  if (s_status.bmm.online) {
    updateTiltCompensatedHeading();
  }

  return true;
}

const Qmi8658Status &qmi8658GetStatus() {
  ensureDefaultsInitialized();
  return s_status.imu;
}

bool qmi8658IsOnline() {
  ensureDefaultsInitialized();
  return s_status.imu.online;
}

void sensorsSetAxisMapping(int8_t sign_x, int8_t sign_y, int8_t sign_z, bool swap_xy) {
  ensureDefaultsInitialized();
  s_status.axis_map.bmm_to_body_sign_x = (sign_x >= 0) ? 1 : -1;
  s_status.axis_map.bmm_to_body_sign_y = (sign_y >= 0) ? 1 : -1;
  s_status.axis_map.bmm_to_body_sign_z = (sign_z >= 0) ? 1 : -1;
  s_status.axis_map.bmm_swap_xy = swap_xy;
  updateTiltCompensatedHeading();
}

// ============================================================================
// HELPER DI STAMPA DIAGNOSTICA SERIALE (TEST 5, TEST 6, TEST 9)
// ============================================================================

void sensorsPrintBmm150Diagnostics(Stream &out) {
  ensureDefaultsInitialized();
  const Bmm150Status &b = s_status.bmm;

  out.println();
  out.println("========================================================================");
  out.println("[TEST 5] BUSSOLA MAGNETICA 3 ASSI SEEED GROVE BMM150 V2.0 (0x13 / 0x11)");
  out.println("========================================================================");
  out.printf("  Status             : %s\n", b.online ? "ONLINE" : "OFFLINE (NACK on 0x13 & 0x11)");
  out.printf("  I2C Address        : 0x%02X | Chip ID (reg 0x40): 0x%02X (expected 0x32)\n",
             b.i2c_addr, b.chip_id);

  if (!b.online) {
    out.println("  Diagnostic Note    : BMM150 did not ACK on shared I2C bus (GPIO15=TP3 SDA, GPIO14=TP2 SCL).");
    out.println("                       Check continuity on back solder pads TP3 (SDA) & TP2 (SCL) and Grove cable.");
    out.println("[TEST 5 RESULT] FAIL (BMM150 unreachable on I2C bus)");
    out.println("========================================================================");
    return;
  }

  out.printf("  Bosch Trim Regs    : x1=%d y1=%d x2=%d y2=%d z1=%u z2=%d z3=%d z4=%d xy1=%u xy2=%d xyz1=%u (%s)\n",
             b.trim.dig_x1, b.trim.dig_y1, b.trim.dig_x2, b.trim.dig_y2,
             b.trim.dig_z1, b.trim.dig_z2, b.trim.dig_z3, b.trim.dig_z4,
             b.trim.dig_xy1, b.trim.dig_xy2, b.trim.dig_xyz1,
             b.trim.trim_valid ? "VALID" : "INVALID");
  out.printf("  Raw ADC + R-Hall   : rawX=%d, rawY=%d, rawZ=%d, R-Hall=%u\n",
             b.raw_x, b.raw_y, b.raw_z, b.raw_rhall);
  out.printf("  Bosch Comp (RAW)   : X=%.2f uT, Y=%.2f uT, Z=%.2f uT | |B|_raw=%.2f uT\n",
             b.raw_x_ut, b.raw_y_ut, b.raw_z_ut, b.raw_b_norm_ut);
  out.printf("  Calibrated Field   : X=%.2f uT, Y=%.2f uT, Z=%.2f uT | |B|=%.2f uT [%s]\n",
             b.x_ut, b.y_ut, b.z_ut, b.b_norm_ut,
             b.field_plausible ? "PLAUSIBLE 15..120uT" : "OUT_OF_RANGE");
  out.printf("  Heading Preliminary: %.1f deg [RAW (non calibrato)]\n", b.heading_raw_deg);
  out.printf("  Heading 2D Calib   : %.1f deg [%s]\n",
             b.heading_cal_deg, b.calibrated ? "CAL" : "RAW (uncalibrated)");
  out.printf("  Heading Tilt-Comp  : %.1f deg (%s) [%s]\n",
             b.heading_tilt_deg, b.cardinal_str,
             b.tilt_compensated ? "TILT_COMP_ACTIVE" : "2D_ONLY");
  out.printf("  Sampling & Noise   : Rate=%.1f Hz | Samples=%lu | Errors=%lu\n",
             b.update_rate_hz, (unsigned long)b.sample_count, (unsigned long)b.error_count);
  out.printf("  Stability (32-win) : std(X,Y,Z)=(%.2f, %.2f, %.2f) uT | std(|B|)=%.2f uT (p2p=%.2f uT) | std(Hdg)=%.2f deg\n",
             b.std_x_ut, b.std_y_ut, b.std_z_ut, b.std_b_ut, b.p2p_b_ut, b.std_heading_deg);
  sensorsPrintCalibrationDiagnostics(out);
  out.printf("[TEST 5 RESULT] %s (ChipID=0x%02X, Trim=%s, |B|=%.1f uT, Mode=%s)\n",
             (b.online && b.chip_id == BMM150_CHIP_ID && b.trim.trim_valid) ? "PASS" : "FAIL",
             b.chip_id,
             b.trim.trim_valid ? "OK" : "ERR",
             b.b_norm_ut,
             b.calibrated ? "CALIBRATED" : "RAW_PENDING_3D_ROTATION");
  out.println("========================================================================");
}

void sensorsPrintCalibrationDiagnostics(Stream &out) {
  ensureDefaultsInitialized();
  const MagCalibrationData &c = s_status.cal;
  out.printf("  Calibration State  : %s (NVS '%s': %s, InProgress=%s, Samples=%lu)\n",
             c.valid ? "CALIBRATED [CAL]" : "UNCALIBRATED [RAW]",
             SENSORS_NVS_NAMESPACE,
             c.loaded_from_nvs ? "LOADED" : "NONE",
             c.in_progress ? "YES" : "NO",
             (unsigned long)c.sample_count);
  out.printf("  Min / Max (uT)     : X=[%.1f, %.1f] (span=%.1f) | Y=[%.1f, %.1f] (span=%.1f) | Z=[%.1f, %.1f] (span=%.1f)\n",
             c.min_x, c.max_x, c.span_x,
             c.min_y, c.max_y, c.span_y,
             c.min_z, c.max_z, c.span_z);
  out.printf("  Hard-Iron Offsets  : offX=%.2f uT, offY=%.2f uT, offZ=%.2f uT\n",
             c.offset_x, c.offset_y, c.offset_z);
  out.printf("  Soft-Iron Scales   : sclX=%.3f, sclY=%.3f, sclZ=%.3f\n",
             c.scale_x, c.scale_y, c.scale_z);
}

void sensorsPrintInterferenceReport(Stream &out) {
  ensureDefaultsInitialized();
  const SensorsMotorInterferenceReport &r = s_status.interference;

  out.println();
  out.println("========================================================================");
  out.println("[TEST 6] INTERFERENZE MAGNETICHE DEL MOTORINO SULLA BUSSOLA BMM150");
  out.println("========================================================================");
  out.printf("  Test Executed      : %s | BMM150 Online During Test: %s | Steps Recorded: %u\n",
             r.tested ? "YES" : "NO",
             r.bmm_was_online ? "YES" : "NO",
             r.step_count);
  out.printf("  Summary            : %s\n", r.summary);

  if (!r.tested || !r.bmm_was_online || r.step_count == 0) {
    out.println("  Note               : Cannot quantify magnetic interference while BMM150 is offline.");
    out.println("[TEST 6 RESULT] BLOCKED / PENDING BMM150 CONNECTION");
    out.println("========================================================================");
    return;
  }

  for (uint8_t i = 0; i < r.step_count; ++i) {
    const SensorsMotorInterferenceStep &st = r.steps[i];
    out.printf("\n  --- Step [%u] Distance: %s (%.1f cm) ---\n",
               i, st.distance_label, st.distance_cm);
    out.printf("    Phase A (Motor OFF Baseline, n=%u): X=%.2f Y=%.2f Z=%.2f | |B|=%.2f+-%.2f uT | Hdg=%.1f+-%.2f deg\n",
               st.phase_a_off.samples,
               st.phase_a_off.mean_x_ut, st.phase_a_off.mean_y_ut, st.phase_a_off.mean_z_ut,
               st.phase_a_off.mean_b_ut, st.phase_a_off.std_b_ut,
               st.phase_a_off.mean_heading_deg, st.phase_a_off.std_heading_deg);
    out.printf("    Phase B (Motor ON Vibration, n=%u): X=%.2f Y=%.2f Z=%.2f | |B|=%.2f uT (peak=%.2f) | Hdg=%.1f deg\n",
               st.phase_b_on.samples,
               st.phase_b_on.mean_x_ut, st.phase_b_on.mean_y_ut, st.phase_b_on.mean_z_ut,
               st.phase_b_on.mean_b_ut, st.phase_b_on.peak_b_ut,
               st.phase_b_on.mean_heading_deg);
    out.printf("    Dynamic Delta (ON vs OFF)        : d|B|_mean=%.2f uT, d|B|_peak=%.2f uT | dHdg_mean=%.2f deg, dHdg_peak=%.2f deg\n",
               st.delta_b_mean_ut, st.delta_b_peak_ut,
               st.delta_heading_mean_deg, st.delta_heading_peak_deg);
    out.printf("    Phase C (Post-Vibration OFF, n=%u): |B|=%.2f uT, Hdg=%.1f deg | Residual d|B|=%.2f uT, dHdg=%.2f deg -> %s\n",
               st.phase_c_post.samples,
               st.phase_c_post.mean_b_ut, st.phase_c_post.mean_heading_deg,
               st.post_residual_b_ut, st.post_residual_heading_deg,
               st.returned_to_baseline ? "RETURNED_TO_BASELINE (NO HYSTERESIS)" : "RESIDUAL_OFFSET");
    if (st.has_far_reference) {
      out.printf("    Static Offset vs >=20cm (OFF)    : static_d|B|=%.2f uT, static_dHdg=%.2f deg\n",
                 st.static_delta_b_ut, st.static_delta_heading_deg);
    }
    out.printf("    Step Assessment                  : %s\n",
               st.acceptable_for_case ? "ACCEPTABLE (<5 deg / <5 uT)" : "INTERFERENCE SIGNIFICANT (move motor further)");
  }

  out.printf("\n  Recommended Min Distance in Case   : >= %.1f cm\n", r.recommended_min_dist_cm);
  out.println("========================================================================");
}

void sensorsPrintQmi8658Diagnostics(Stream &out) {
  ensureDefaultsInitialized();
  const Qmi8658Status &q = s_status.imu;

  out.println();
  out.println("========================================================================");
  out.println("[TEST 9] IMU 6 ASSI INTEGRATA QMI8658C (0x6B) & COMPENSAZIONE TILT");
  out.println("========================================================================");
  out.printf("  Status             : %s (I2C Addr=0x%02X, WHO_AM_I=0x%02X, REV_ID=0x%02X)\n",
             q.online ? "ONLINE" : "OFFLINE",
             q.i2c_addr, q.who_am_i, q.revision_id);

  if (!q.online) {
    out.println("[TEST 9 RESULT] FAIL (QMI8658 not responding at 0x6B)");
    out.println("========================================================================");
    return;
  }

  out.printf("  Accelerometer (4G) : ax=%+.4f g, ay=%+.4f g, az=%+.4f g | |a|=%.4f g\n",
             q.acc_x_g, q.acc_y_g, q.acc_z_g, q.acc_norm_g);
  out.printf("  Gyroscope (256dps) : gx=%+.3f dps, gy=%+.3f dps, gz=%+.3f dps\n",
             q.gyr_x_dps, q.gyr_y_dps, q.gyr_z_dps);
  out.printf("  Die Temperature    : %.2f C\n", q.temp_c);
  out.printf("  Attitude (Acc Raw) : Roll=%.2f deg, Pitch=%.2f deg\n",
             q.roll_acc_deg, q.pitch_acc_deg);
  out.printf("  Attitude (Filtered): Roll=%.2f deg (std=%.3f), Pitch=%.2f deg (std=%.3f)\n",
             q.roll_deg, q.std_roll_deg, q.pitch_deg, q.std_pitch_deg);
  out.printf("  PCB Axis Mapping   : Front View: +X_qmi=UP(12h/BAT), +Y_qmi=RIGHT(3h/PWR-BOOT), +Z_qmi=INTO_SCREEN\n");
  out.printf("  BMM150->Body Map   : sign=(%+d, %+d, %+d), swapXY=%s | Tilt-Comp Heading=%.1f deg (%s)\n",
             s_status.axis_map.bmm_to_body_sign_x,
             s_status.axis_map.bmm_to_body_sign_y,
             s_status.axis_map.bmm_to_body_sign_z,
             s_status.axis_map.bmm_swap_xy ? "YES" : "NO",
             s_status.bmm.heading_tilt_deg,
             s_status.bmm.online ? "ACTIVE" : "BMM150_OFFLINE");
  out.printf("  Sampling & Errors  : Rate=%.1f Hz | Samples=%lu | I2C Errors=%lu\n",
             q.update_rate_hz, (unsigned long)q.sample_count, (unsigned long)q.error_count);

  bool norm_ok = (q.acc_norm_g >= 0.85f && q.acc_norm_g <= 1.15f);
  bool test9_ok = q.online && (q.who_am_i == 0x05) && norm_ok;
  out.printf("[TEST 9 RESULT] %s (WHO_AM_I=0x%02X, |a|=%.3fg, Roll=%.2f deg, Pitch=%.2f deg)\n",
             test9_ok ? "PASS" : "FAIL",
             q.who_am_i, q.acc_norm_g, q.roll_deg, q.pitch_deg);
  out.println("========================================================================");
}

void sensorsPrintAllDiagnostics(Stream &out) {
  sensorsPrintBmm150Diagnostics(out);
  sensorsPrintInterferenceReport(out);
  sensorsPrintQmi8658Diagnostics(out);
}
