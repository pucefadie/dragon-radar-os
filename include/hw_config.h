#pragma once

/**
 * @file hw_config.h
 * @brief Configurazione hardware ufficiale verificata per Dragon Radar Hardware Test.
 *
 * Scheda target: Waveshare ESP32-S3-Touch-AMOLED-1.75-G (SKU 31264)
 * MCU:           ESP32-S3R8 (Dual-core Xtensa LX7 @ 240 MHz, 8 MB OPI PSRAM, 16 MB Flash)
 *
 * Fonti di verità verificate:
 * - references/ESP32-S3-Touch-AMOLED-1.75/HARDWARE_REFERENCE.md
 * - references/ESP32-S3-Touch-AMOLED-1.75/Schematic/ESP32-S3-Touch-AMOLED-1.75-schematic.pdf
 * - references/ESP32-S3-Touch-AMOLED-1.75/examples/arduino/libraries/Mylibrary/pin_config.h
 * - references/ESP32-S3-Touch-AMOLED-1.75/examples/arduino/01_HelloWorld/01_HelloWorld.ino
 * - references/ESP32-S3-Touch-AMOLED-1.75/examples/arduino/09_LC76G_I2C/09_LC76G_I2C.ino
 * - references/ESP32-S3-Touch-AMOLED-1.75/examples/arduino/10_Touch_CST9217/10_Touch_CST9217.ino
 * - references/ESP32-S3-Touch-AMOLED-1.75/examples/esp-idf/01_AXP2101/main/port_axp2101.cpp
 * - references/Grove_3_Axis_Compass_V2.0_BMM150/src/bmm150_defs.h
 */

#include <stdint.h>

#ifndef XPOWERS_CHIP_AXP2101
#define XPOWERS_CHIP_AXP2101
#endif

// ============================================================================
// 1. DISPLAY AMOLED QSPI CO5300 (1.75", 466x466, Connettore J3 FH26-31S-0.3SHW)
// ============================================================================
#define HW_LCD_SDIO0            4       // GPIO4  - QSPI_SIO0 / LCD_DATA0 (J3 pin 16)
#define HW_LCD_SDIO1            5       // GPIO5  - QSPI_SI1  / LCD_DATA1 (J3 pin 15)
#define HW_LCD_SDIO2            6       // GPIO6  - QSPI_SI2  / LCD_DATA2 (J3 pin 12)
#define HW_LCD_SDIO3            7       // GPIO7  - QSPI_SI3  / LCD_DATA3 (J3 pin 13)
#define HW_LCD_SCLK             38      // GPIO38 - QSPI_SCL  / LCD_PCLK  (J3 pin 14)
#define HW_LCD_CS               12      // GPIO12 - LCD_CS                (J3 pin 17)
#define HW_LCD_RESET            39      // GPIO39 - LCD_RESET             (J3 pin 18, separato da TP_RESET!)
#define HW_LCD_TE               13      // GPIO13 - LCD_TE Tearing Effect (J3 pin 11)
#define HW_LCD_WIDTH            466     // Risoluzione orizzontale nativa CO5300
#define HW_LCD_HEIGHT           466     // Risoluzione verticale nativa CO5300
#define HW_LCD_COL_OFFSET1      6       // Offset colonne CO5300 da 01_HelloWorld.ino

// ============================================================================
// 2. TOUCH CAPACITIVO CST9217 (Connettore J3 + Bus I2C condiviso)
// ============================================================================
#define HW_TP_INT               11      // GPIO11 - TP_INT   (J3 pin 30, attivo LOW / FALLING)
#define HW_TP_RESET             40      // GPIO40 - TP_RESET (J3 pin 29, separato da LCD_RESET!)

// ============================================================================
// 3. BUS I2C CONDIVISO UNICO (ESP32_SDA / ESP32_SCL + Piazzole TP3 / TP2)
// ============================================================================
// Pull-up onboard presenti sullo schematico: R23 = 2.2 kOhm (SCL), R24 = 2.2 kOhm (SDA) verso VCC3V3.
// Frequenza fissata a 100 kHz per piena compatibilità tra LC76G I2C (0x50/0x54),
// BMM150 esterno su piazzole TP2/TP3 e tutte le periferiche onboard.
#define HW_I2C_SDA              15      // GPIO15 - ESP32_SDA / Piazzola retro TP3 (SDA)
#define HW_I2C_SCL              14      // GPIO14 - ESP32_SCL / Piazzola retro TP2 (SCL)
#define HW_I2C_FREQ_HZ          100000  // 100 kHz (richiesto da 09_LC76G_I2C.ino + cablaggio esterno)
#define HW_I2C_BUF_SIZE         512     // Dimensione buffer Wire per letture blocchi NMEA LC76G

// ============================================================================
// 4. COMPONENTI ESTERNI SU BREADBOARD & PULSANTI ONBOARD
// ============================================================================
// NOTA TECNICA SU GPIO16:
// Come documentato in HARDWARE_REFERENCE.md (righe 58, 112, 168) e sullo schematico U1/U6/U8,
// il clock audio I2S_MCLK è cablato su GPIO42. GPIO16 è un GPIO di espansione generale puro
// esposto sull'header H2 (serigrafia "IO16"), nonostante un vecchio #define I2S_MCK_IO 16
// inutilizzato nel legacy pin_config.h.
// Grove Vibration Motor V1.3 ha un transistor NPN onboard (S8050):
// - SIG = HIGH (3.3V) -> Motore ON
// - SIG = LOW  (0V)   -> Motore OFF (stato obbligatorio al boot)
#define HW_MOTOR_PIN            16      // GPIO16 - Header H2 ("IO16") -> Grove Vibration Motor V1.3 SIG
#define HW_MOTOR_ACTIVE_LEVEL   1       // HIGH attiva il transistor del modulo Grove V1.3
#define HW_MOTOR_SAFE_OFF_LEVEL 0       // LOW spegne il motore
#define HW_MOTOR_MAX_ON_MS      1000    // Watchdog hardware massimo per singola attivazione (1000 ms)

// Pulsante esterno tattile su breadboard tra GPIO44 (U0RXD su H2) e GND:
// Configurato con INPUT_PULLUP interno: Rilasciato = HIGH (1), Premuto = LOW (0).
#define HW_BUTTON_PIN           44      // GPIO44 - Header H2 ("RXD" / U0RXD) -> Pulsante esterno a GND
#define HW_BUTTON_ACTIVE_LOW    1       // Premuto = LOW, Rilasciato = HIGH
#define HW_BTN_DEBOUNCE_MS      40      // Tempo di debounce software (ms)
#define HW_BTN_LONG_PRESS_MS    1000    // Soglia pressione lunga (ms)

// Pulsante BOOT onboard (Key1 su schematico, con pull-up esterno R7 = 10 kOhm verso VCC3V3):
#define HW_BOOT_BTN_PIN         0       // GPIO0  - Key1 BOOT onboard (INPUT_PULLUP, premuto = LOW)
#define HW_U0TXD_PIN            43      // GPIO43 - Header H2 ("TXD" / U0TXD)

// ============================================================================
// 5. PIN UART OPZIONALI GPS (Variante -G, resistenze R15/R16 NC/0R su schematico)
// ============================================================================
// Sullo schematico (blocco GNSS Q1 LC76GABMD):
// - GPIO17 passa attraverso R16 (NC/0R) verso GPS_RXD (pin 3 RXD1 di LC76G)
// - GPIO18 passa attraverso R15 (NC/0R) verso GPS_TXD (pin 2 TXD1 di LC76G)
#define HW_GPS_UART_TX_MCU      17      // GPIO17 - MCU TX -> LC76G RXD1 (se R16 montata)
#define HW_GPS_UART_RX_MCU      18      // GPIO18 - MCU RX <- LC76G TXD1 (se R15 montata)

// ============================================================================
// 6. INDIRIZZI I2C 7-BIT ATTESI SUL BUS CONDIVISO (GPIO14/GPIO15)
// ============================================================================
#define HW_ADDR_BMM150          0x13    // Seeed Grove Compass V2.0 BMM150 (default jumper I2C)
#define HW_ADDR_BMM150_ALT      0x11    // BMM150 indirizzo alternativo
#define HW_ADDR_ES8311          0x18    // Codec audio DAC ES8311 (7-bit; 0x30 in notazione 8-bit)
#define HW_ADDR_TCA9554         0x20    // I/O Expander TCA9554PWR (A2:A0 = 000 a GND)
#define HW_ADDR_AXP2101         0x34    // PMIC AXP2101
#define HW_ADDR_ES7210          0x40    // ADC audio 4 canali ES7210 (AD1:AD0 = 00 a AGND)
#define HW_ADDR_LC76G_W         0x50    // GNSS Quectel LC76G - indirizzo I2C di comando/scrittura
#define HW_ADDR_PCF85063        0x51    // RTC PCF85063ATL
#define HW_ADDR_LC76G_R         0x54    // GNSS Quectel LC76G - indirizzo I2C di lettura dati NMEA
#define HW_ADDR_CST9217         0x5A    // Controller Touch capacitivo CST9217
#define HW_ADDR_QMI8658         0x6B    // IMU 6 assi QMI8658C (SDO/SA0 a GND -> 0x6B)

// Costanti identificative registri sensori
#define HW_BMM150_CHIP_ID_REG   0x40    // Registro Chip ID del BMM150
#define HW_BMM150_CHIP_ID       0x32    // Valore Chip ID atteso del BMM150

// ============================================================================
// 7. PIN TCA9554 I/O EXPANDER (Indirizzo 0x20)
// ============================================================================
#define HW_EXIO_EXIO0           0       // P0 - Espansione libera -> Piazzola retro TP4 (EX0)
#define HW_EXIO_EXIO1           1       // P1 - Espansione libera -> Piazzola retro TP5 (EX1)
#define HW_EXIO_EXIO2           2       // P2 - Espansione libera -> Piazzola retro TP6 (EX2)
#define HW_EXIO_RTC_INT         3       // P3 - Input:  RTC_INT (PCF85063 pin 4 INT)
#define HW_EXIO_PWR_BTN         4       // P4 - Input:  SYS_OUT (Key2 PWR condizionato da T1 BSS138: HIGH=premuto, LOW=rilasciato)
#define HW_EXIO_AXP_IRQ         5       // P5 - Input:  AXP_IRQ (AXP2101 pin 38 IRQ)
#define HW_EXIO_QMI_INT1        6       // P6 - Input:  QMI_INT1 (QMI8658 pin 4 INT1)
#define HW_EXIO_GPS_RST         7       // P7 - Output: GPS_RST (LC76G pin 9 RESET)

// ============================================================================
// 8. ALTRI PIN ONBOARD (Riferimento e prevenzione conflitti)
// ============================================================================
#define HW_QMI_INT2_PIN         21      // GPIO21 - QMI_INT2 diretto da QMI8658 pin 9
#define HW_I2S_MCLK_PIN         42      // GPIO42 - I2S_MCLK reale per ES8311 + ES7210
#define HW_I2S_BCLK_PIN         9       // GPIO9  - I2S_SCLK / BCLK condiviso
#define HW_I2S_LRCK_PIN         45      // GPIO45 - I2S_LRCK / WS condiviso
#define HW_I2S_DOUT_PIN         8       // GPIO8  - I2S_DSDIN verso ES8311
#define HW_I2S_DIN_PIN          10      // GPIO10 - I2S_ASDOUT da ES7210
#define HW_PA_CTRL_PIN          46      // GPIO46 - PA_CTRL verso NS4150B CTRL
#define HW_SD_CMD_PIN           1       // GPIO1  - SDMMC CMD
#define HW_SD_CLK_PIN           2       // GPIO2  - SDMMC CLK
#define HW_SD_D0_PIN            3       // GPIO3  - SDMMC D0
#define HW_SD_CS_PIN            41      // GPIO41 - SDMMC CD/D3 (SDCS)
#define HW_USB_DM_PIN           19      // GPIO19 - USB_D- nativo (NON riutilizzare!)
#define HW_USB_DP_PIN           20      // GPIO20 - USB_D+ nativo (NON riutilizzare!)
