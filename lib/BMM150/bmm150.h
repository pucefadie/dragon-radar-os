#ifndef _BMM150_H_
#define _BMM150_H_

/** Includes */
#include <Arduino.h>
#include <Wire.h>
#include "bmm150_defs.h"

/**
 * @brief Driver Bosch BMM150 (basato sulla libreria ufficiale Seeed Grove Compass V2.0)
 *
 * Modifiche rispetto al file originale Seeed per compatibilità ESP32-S3-Touch-AMOLED-1.75-G:
 * 1. BMM150::initialize() NON chiama Wire.begin() senza argomenti (usa il bus condiviso Wire
 *    già inizializzato su SDA=GPIO15, SCL=GPIO14 a 100 kHz).
 * 2. Supporto indirizzo I2C configurabile a runtime (default 0x13, fallback 0x11) e lettura
 *    esplicita del Chip ID (0x32 dal registro 0x40).
 * 3. Formule di compensazione Bosch intatte in int16_t (compensate_x/y/z) affiancate da
 *    versioni sub-uT in virgola mobile (compensate_x_float/y_float/z_float con divisione / 16.0f).
 */
class BMM150 {
  public:
    BMM150();

    /**
     * @brief Inizializza il sensore BMM150 sul bus Wire già attivo (NON chiama Wire.begin()).
     * @param i2c_addr Indirizzo I2C 7-bit (default 0x13, alternativo 0x11)
     * @return BMM150_OK (0) se Chip ID == 0x32, altrimenti codice errore negativo
     */
    int8_t initialize(uint8_t i2c_addr = BMM150_I2C_Address);

    void set_i2c_address(uint8_t i2c_addr);
    uint8_t get_i2c_address() const;

    uint8_t read_chip_id();
    uint8_t get_chip_id() const;
    bool last_i2c_ok() const;

    /**
     * @brief Verifica il bit DRDY (bit 0 del registro 0x48 BMM150_DATA_READY_STATUS).
     */
    bool is_data_ready();

    /**
     * @brief Legge i registri 0x42..0x49 (X, Y, Z, R-Hall) e calcola sia mag_data (int16_t uT)
     *        sia mag_data_float (float uT con risoluzione 1/16 uT = 0.0625 uT).
     */
    void read_mag_data();

    /**
     * @brief Compensazione standard Bosch X in int16_t (uT).
     */
    int16_t compensate_x(int16_t mag_data_x, uint16_t data_rhall);

    /**
     * @brief Compensazione standard Bosch Y in int16_t (uT).
     */
    int16_t compensate_y(int16_t mag_data_y, uint16_t data_rhall);

    /**
     * @brief Compensazione standard Bosch Z in int16_t (uT).
     */
    int16_t compensate_z(int16_t mag_data_z, uint16_t data_rhall);

    /**
     * @brief Compensazione Bosch X in float (uT, risoluzione sub-uT / 16.0f).
     */
    float compensate_x_float(int16_t mag_data_x, uint16_t data_rhall);

    /**
     * @brief Compensazione Bosch Y in float (uT, risoluzione sub-uT / 16.0f).
     */
    float compensate_y_float(int16_t mag_data_y, uint16_t data_rhall);

    /**
     * @brief Compensazione Bosch Z in float (uT, risoluzione sub-uT / 16.0f).
     */
    float compensate_z_float(int16_t mag_data_z, uint16_t data_rhall);

    /**
     * @brief Imposta la modalità operativa (BMM150_NORMAL_MODE, FORCED, SLEEP, SUSPEND).
     */
    void set_op_mode(uint8_t op_mode);

    /**
     * @brief Legge i registri di trim di fabbrica Bosch (dig_x1..dig_xyz1).
     */
    void read_trim_registers();

    /**
     * @brief Scrive i bit op_mode nel registro 0x4C.
     */
    void write_op_mode(uint8_t op_mode);

    /**
     * @brief Imposta il bit Power Control nel registro 0x4B.
     */
    void set_power_control_bit(uint8_t pwrcntrl_bit);

    /**
     * @brief Porta il sensore da Suspend Mode a Sleep Mode impostando il bit 0 di 0x4B a 1.
     */
    void suspend_to_sleep_mode();

    /**
     * @brief Imposta il preset mode (LOWPOWER, REGULAR, HIGHACCURACY, ENHANCED).
     */
    void set_presetmode(uint8_t preset_mode);
    void set_preset_mode(uint8_t mode) { set_presetmode(mode); }

    /**
     * @brief Imposta ODR e ripetizioni XY/Z dalla struttura settings.
     */
    void set_odr_xyz_rep(struct bmm150_settings settings);

    /**
     * @brief Imposta il numero di ripetizioni XY nel registro 0x51.
     */
    void set_xy_rep(struct bmm150_settings settings);

    /**
     * @brief Imposta il numero di ripetizioni Z nel registro 0x52.
     */
    void set_z_rep(struct bmm150_settings settings);

    /**
     * @brief Imposta il Data Rate (ODR) nel registro 0x4C.
     */
    void set_odr(struct bmm150_settings settings);

    /**
     * @brief Esegue il soft-reset del sensore (registro 0x4B).
     */
    void soft_reset();

    struct bmm150_settings settings;
    struct bmm150_raw_mag_data raw_mag_data;
    struct bmm150_mag_data mag_data;
    struct bmm150_mag_data_float mag_data_float;
    struct bmm150_trim_registers trim_data;

    void i2c_write(short address, short byte);
    void i2c_read(short address, uint8_t* buffer, short length);
    void i2c_read(short address, int8_t* buffer, short length);
    uint8_t i2c_read(short address);

  private:
    uint8_t _i2c_addr;
    uint8_t _chip_id;
    bool _last_i2c_ok;
};

#endif
