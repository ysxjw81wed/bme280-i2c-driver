/*
 * bme280.c - register level driver.
 *
 * Every wait is bounded: a wedged bus or a sensor that never finishes its
 * conversion returns BME280_ERR_TIMEOUT instead of spinning forever.
 */
#include "bme280.h"

#define BME280_RESET_WAIT_MS 2u   /* datasheet 1.1: 2 ms after soft reset */
#define BME280_STATUS_WAIT_MS 20u /* NVM copy, datasheet 1.1 */

static int write_reg(struct bme280_dev *dev, uint8_t reg, uint8_t value)
{
    if (dev->write(dev->ctx, dev->addr, reg, &value, 1) != 0) {
        return BME280_ERR_IO;
    }
    return BME280_OK;
}

static int read_regs(struct bme280_dev *dev, uint8_t reg, uint8_t *buf, size_t len)
{
    if (dev->read(dev->ctx, dev->addr, reg, buf, len) != 0) {
        return BME280_ERR_IO;
    }
    return BME280_OK;
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static int load_calibration(struct bme280_dev *dev)
{
    uint8_t raw[26];
    uint8_t rh[7];
    uint8_t h1;
    int err;

    err = read_regs(dev, BME280_REG_CALIB00, raw, sizeof raw);
    if (err != BME280_OK) {
        return err;
    }
    err = read_regs(dev, BME280_REG_CALIB26, rh, sizeof rh);
    if (err != BME280_OK) {
        return err;
    }
    err = read_regs(dev, 0xA1u, &h1, 1);
    if (err != BME280_OK) {
        return err;
    }

    dev->calib.dig_t1 = le16(&raw[0]);
    dev->calib.dig_t2 = (int16_t)le16(&raw[2]);
    dev->calib.dig_t3 = (int16_t)le16(&raw[4]);
    dev->calib.dig_p1 = le16(&raw[6]);
    dev->calib.dig_p2 = (int16_t)le16(&raw[8]);
    dev->calib.dig_p3 = (int16_t)le16(&raw[10]);
    dev->calib.dig_p4 = (int16_t)le16(&raw[12]);
    dev->calib.dig_p5 = (int16_t)le16(&raw[14]);
    dev->calib.dig_p6 = (int16_t)le16(&raw[16]);
    dev->calib.dig_p7 = (int16_t)le16(&raw[18]);
    dev->calib.dig_p8 = (int16_t)le16(&raw[20]);
    dev->calib.dig_p9 = (int16_t)le16(&raw[22]);
    dev->calib.dig_h1 = h1;

    /* Datasheet 4.2.3: H4/H5 share one nibble-packed byte. */
    dev->calib.dig_h2 = (int16_t)le16(&rh[0]);
    dev->calib.dig_h3 = rh[2];
    dev->calib.dig_h4 = (int16_t)((rh[3] << 4) | (rh[4] & 0x0Fu));
    dev->calib.dig_h5 = (int16_t)((rh[5] << 4) | (rh[4] >> 4));
    dev->calib.dig_h6 = (int8_t)rh[6];
    return BME280_OK;
}

static int wait_status_clear(struct bme280_dev *dev, uint8_t mask, uint32_t budget_ms)
{
    uint8_t status;
    uint32_t waited = 0;

    while (waited < budget_ms) {
        if (read_regs(dev, BME280_REG_STATUS, &status, 1) != BME280_OK) {
            return BME280_ERR_IO;
        }
        if ((status & mask) == 0) {
            return BME280_OK;
        }
        dev->delay_ms(dev->ctx, 1);
        waited++;
    }
    return BME280_ERR_TIMEOUT;
}

static uint8_t ctrl_meas_value(const struct bme280_dev *dev, uint8_t mode)
{
    return (uint8_t)((dev->osrs_t << 5) | (dev->osrs_p << 2) | mode);
}

int bme280_init(struct bme280_dev *dev)
{
    uint8_t id;
    int err;

    if (dev == NULL || dev->read == NULL || dev->write == NULL ||
        dev->delay_ms == NULL) {
        return BME280_ERR_ARG;
    }
    if (dev->osrs_t == BME280_OSRS_SKIP || dev->osrs_p == BME280_OSRS_SKIP ||
        dev->osrs_h == BME280_OSRS_SKIP) {
        dev->osrs_t = dev->osrs_t == BME280_OSRS_SKIP ? BME280_OSRS_X1 : dev->osrs_t;
        dev->osrs_p = dev->osrs_p == BME280_OSRS_SKIP ? BME280_OSRS_X1 : dev->osrs_p;
        dev->osrs_h = dev->osrs_h == BME280_OSRS_SKIP ? BME280_OSRS_X1 : dev->osrs_h;
    }

    err = read_regs(dev, BME280_REG_CHIP_ID, &id, 1);
    if (err != BME280_OK) {
        return err;
    }
    if (id != BME280_CHIP_ID) {
        return BME280_ERR_ID;
    }

    if (write_reg(dev, BME280_REG_RESET, BME280_RESET_MAGIC) != BME280_OK) {
        return BME280_ERR_IO;
    }
    dev->delay_ms(dev->ctx, BME280_RESET_WAIT_MS);
    err = wait_status_clear(dev, BME280_STATUS_IM_UPDATE, BME280_STATUS_WAIT_MS);
    if (err != BME280_OK) {
        return err;
    }

    /* ctrl_hum is latched: it must land before the next ctrl_meas write. */
    if (write_reg(dev, BME280_REG_CTRL_HUM, dev->osrs_h) != BME280_OK) {
        return BME280_ERR_IO;
    }
    if (write_reg(dev, BME280_REG_CONFIG, 0x00) != BME280_OK) { /* standby 0.5 ms, IIR off */
        return BME280_ERR_IO;
    }
    return load_calibration(dev);
}

uint32_t bme280_measure_ms(uint8_t osrs_t, uint8_t osrs_p, uint8_t osrs_h)
{
    /* Datasheet 9.1: max conversion times per channel, rounded up. */
    static const uint16_t temp_ms[] = {0, 2, 3, 5, 10, 20};
    static const uint16_t press_ms[] = {0, 3, 5, 9, 17, 34};
    static const uint16_t hum_ms[] = {0, 3, 5, 9, 17, 34};
    uint32_t total = 0;

    if (osrs_t < 6) {
        total += temp_ms[osrs_t];
    }
    if (osrs_p < 6) {
        total += press_ms[osrs_p];
    }
    if (osrs_h < 6) {
        total += hum_ms[osrs_h];
    }
    return total + 2; /* margin */
}

int bme280_read_forced(struct bme280_dev *dev, struct bme280_sample *out)
{
    uint8_t raw[8];
    int32_t adc_t;
    int32_t adc_p;
    int32_t adc_h;
    int err;

    if (dev == NULL || out == NULL) {
        return BME280_ERR_ARG;
    }
    if (write_reg(dev, BME280_REG_CTRL_MEAS, ctrl_meas_value(dev, 0x01u)) != BME280_OK) {
        return BME280_ERR_IO;
    }
    dev->delay_ms(dev->ctx, bme280_measure_ms(dev->osrs_t, dev->osrs_p, dev->osrs_h));
    err = wait_status_clear(dev, BME280_STATUS_MEASURING, BME280_STATUS_WAIT_MS);
    if (err != BME280_OK) {
        return err;
    }

    err = read_regs(dev, BME280_REG_PRESS_MSB, raw, sizeof raw);
    if (err != BME280_OK) {
        return err;
    }
    adc_p = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
    adc_t = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);
    adc_h = ((int32_t)raw[6] << 8) | raw[7];

    out->temperature_c100 = bme280_compensate_temperature(&dev->calib, adc_t);
    out->pressure_q24_8 = bme280_compensate_pressure(&dev->calib, adc_p);
    out->humidity_q22_10 = bme280_compensate_humidity(&dev->calib, adc_h);
    return BME280_OK;
}

int bme280_start_normal(struct bme280_dev *dev, uint32_t period_ms,
                        enum bme280_standby sb, enum bme280_iir iir)
{
    uint8_t config;

    if (dev == NULL || period_ms < bme280_measure_ms(dev->osrs_t, dev->osrs_p, dev->osrs_h)) {
        return BME280_ERR_ARG;
    }
    config = (uint8_t)((sb << 5) | (iir << 2));
    if (write_reg(dev, BME280_REG_CONFIG, config) != BME280_OK) {
        return BME280_ERR_IO;
    }
    if (write_reg(dev, BME280_REG_CTRL_HUM, dev->osrs_h) != BME280_OK) {
        return BME280_ERR_IO;
    }
    return write_reg(dev, BME280_REG_CTRL_MEAS, ctrl_meas_value(dev, 0x03u));
}

int bme280_stop(struct bme280_dev *dev)
{
    if (dev == NULL) {
        return BME280_ERR_ARG;
    }
    return write_reg(dev, BME280_REG_CTRL_MEAS, ctrl_meas_value(dev, 0x00u));
}
