/*
 * bme280.h - BME280 temperature / pressure / humidity driver (I2C, bare metal).
 *
 * Datasheet: Bosch BME280, rev 1.6 (section numbers in comments).
 * No dynamic allocation, no floating point, no global state: all sensor state
 * lives in struct bme280_dev, so several devices can coexist on one bus.
 */
#ifndef BME280_H
#define BME280_H

#include <stddef.h>
#include <stdint.h>

/* Datasheet 6.2: fixed I2C addresses selected by the SDO pin strap. */
#define BME280_ADDR_SDO_GND 0x76u
#define BME280_ADDR_SDO_VDD 0x77u

/* Datasheet 5.4: chip id is 0x60; 0x58 and 0x56 are BMP280 parts. */
#define BME280_CHIP_ID 0x60u

/* Datasheet 5.3 / 5.4: register map, the only addresses this driver touches. */
#define BME280_REG_CALIB00 0x88u /* 0x88..0xA1 */
#define BME280_REG_CHIP_ID 0xD0u
#define BME280_REG_RESET 0xE0u
#define BME280_REG_CALIB26 0xE1u /* 0xE1..0xE7 humidity calibration */
#define BME280_REG_CTRL_HUM 0xF2u
#define BME280_REG_STATUS 0xF3u
#define BME280_REG_CTRL_MEAS 0xF4u
#define BME280_REG_CONFIG 0xF5u
#define BME280_REG_PRESS_MSB 0xF7u /* 0xF7..0xFE burst */

/* Datasheet 5.4: soft reset magic and status bits. */
#define BME280_RESET_MAGIC 0xB6u
#define BME280_STATUS_MEASURING 0x08u
#define BME280_STATUS_IM_UPDATE 0x01u

/* Oversampling factors, datasheet 5.4 (ctrl_meas / ctrl_hum). */
enum bme280_osrs {
    BME280_OSRS_SKIP = 0,
    BME280_OSRS_X1 = 1,
    BME280_OSRS_X2 = 2,
    BME280_OSRS_X4 = 3,
    BME280_OSRS_X8 = 4,
    BME280_OSRS_X16 = 5,
};

/* Standby time in normal mode, datasheet 5.4 (config[7:5]). */
enum bme280_standby {
    BME280_SB_0_5_MS = 0,
    BME280_SB_62_5_MS = 1,
    BME280_SB_125_MS = 2,
    BME280_SB_250_MS = 3,
    BME280_SB_500_MS = 4,
    BME280_SB_1000_MS = 5,
    BME280_SB_10_MS = 6,
    BME280_SB_20_MS = 7,
};

/* Datasheet 5.4 (config[4:2]). */
enum bme280_iir {
    BME280_IIR_OFF = 0,
    BME280_IIR_2 = 1,
    BME280_IIR_4 = 2,
    BME280_IIR_8 = 3,
    BME280_IIR_16 = 4,
};

enum bme280_err {
    BME280_OK = 0,
    BME280_ERR_ARG = -1,
    BME280_ERR_IO = -2,
    BME280_ERR_ID = -3,
    BME280_ERR_TIMEOUT = -4,
};

/* Datasheet 4.2.2: signed integer calibration words. */
struct bme280_calib {
    uint16_t dig_t1;
    int16_t dig_t2;
    int16_t dig_t3;
    uint16_t dig_p1;
    int16_t dig_p2;
    int16_t dig_p3;
    int16_t dig_p4;
    int16_t dig_p5;
    int16_t dig_p6;
    int16_t dig_p7;
    int16_t dig_p8;
    int16_t dig_p9;
    uint8_t dig_h1;
    int16_t dig_h2;
    uint8_t dig_h3;
    int16_t dig_h4;
    int16_t dig_h5;
    int8_t dig_h6;
    int32_t t_fine; /* filled in by the temperature compensation pass */
};

struct bme280_dev;

/* Blocking I2C transfer, register addressed. Return 0 on success. */
typedef int (*bme280_i2c_read)(void *ctx, uint8_t addr, uint8_t reg,
                               uint8_t *buf, size_t len);
typedef int (*bme280_i2c_write)(void *ctx, uint8_t addr, uint8_t reg,
                                const uint8_t *buf, size_t len);
/* Busy-wait or sleep for at least ms milliseconds (datasheet 1.1 timings). */
typedef void (*bme280_delay_ms)(void *ctx, uint32_t ms);

struct bme280_dev {
    uint8_t addr;
    void *ctx;
    bme280_i2c_read read;
    bme280_i2c_write write;
    bme280_delay_ms delay_ms;
    struct bme280_calib calib;
    uint8_t osrs_t;
    uint8_t osrs_p;
    uint8_t osrs_h;
};

struct bme280_sample {
    int32_t temperature_c100;   /* 0.01 degC */
    uint32_t pressure_q24_8;    /* Q24.8 Pa, use BME280_PRESSURE_PA() */
    uint32_t humidity_q22_10;   /* Q22.10 %RH, use BME280_HUMIDITY_MILLI() */
};

/*
 * Verify chip id, soft-reset, wait for NVM copy (datasheet 5.4 / 5.3.2) and
 * load calibration. Humidity oversampling must be known here because
 * ctrl_hum is latched by the next ctrl_meas write (datasheet 5.4.3 errata).
 */
int bme280_init(struct bme280_dev *dev);

/* Datasheet 9.1: worst-case measurement time for the oversampling setting. */
uint32_t bme280_measure_ms(uint8_t osrs_t, uint8_t osrs_p, uint8_t osrs_h);

/* One forced-mode conversion; the part returns to sleep afterwards. */
int bme280_read_forced(struct bme280_dev *dev, struct bme280_sample *out);

/* Consecutive reads in normal mode; call bme280_stop() to go back to sleep. */
int bme280_start_normal(struct bme280_dev *dev, uint32_t period_ms,
                        enum bme280_standby sb, enum bme280_iir iir);
int bme280_stop(struct bme280_dev *dev);

/*
 * Compensation, datasheet 4.2.3 (all three channels) as
 * 64-bit integer math. t_fine comes from the temperature pass.
 */
int32_t bme280_compensate_temperature(struct bme280_calib *calib, int32_t adc_t);
uint32_t bme280_compensate_pressure(const struct bme280_calib *calib, int32_t adc_p);
uint32_t bme280_compensate_humidity(const struct bme280_calib *calib, int32_t adc_h);

/* Fixed-point outputs to whole units; both round to nearest. */
#define BME280_PRESSURE_PA(q24_8) (((q24_8) + 128u) >> 8)
#define BME280_HUMIDITY_MILLI(q22_10) ((((q22_10) * 1000u) + 512u) >> 10)

#endif /* BME280_H */
