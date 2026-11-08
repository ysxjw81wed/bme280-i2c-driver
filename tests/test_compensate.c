/*
 * test_compensate.c - host side checks with a register-file fake.
 *
 * Calibration words and ADC values are the worked example from datasheet
 * appendix B, so the expected temperature and pressure are fixed numbers.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bme280.h"

struct fake_bus {
    uint8_t regs[256];
    uint8_t status_stuck;
    unsigned writes;
};

static int fake_read(void *ctx, uint8_t addr, uint8_t reg, uint8_t *buf, size_t len)
{
    struct fake_bus *bus = ctx;
    (void)addr;
    if ((size_t)reg + len > sizeof bus->regs) {
        return -1;
    }
    memcpy(buf, &bus->regs[reg], len);
    return 0;
}

static int fake_write(void *ctx, uint8_t addr, uint8_t reg, const uint8_t *buf, size_t len)
{
    struct fake_bus *bus = ctx;
    (void)addr;
    if ((size_t)reg + len > sizeof bus->regs) {
        return -1;
    }
    if (reg == BME280_REG_RESET) {
        bus->regs[BME280_REG_STATUS] = 0;
    }
    if (reg == BME280_REG_CTRL_MEAS) {
        bus->regs[BME280_REG_STATUS] = bus->status_stuck ? BME280_STATUS_MEASURING : 0;
    }
    memcpy(&bus->regs[reg], buf, len);
    bus->writes++;
    return 0;
}

static void fake_delay(void *ctx, uint32_t ms)
{
    (void)ctx;
    (void)ms;
}

static void put16(struct fake_bus *bus, uint8_t reg, uint16_t value)
{
    bus->regs[reg] = (uint8_t)(value & 0xFFu);
    bus->regs[reg + 1] = (uint8_t)(value >> 8);
}

static void load_datasheet_calibration(struct fake_bus *bus)
{
    put16(bus, 0x88, 27504);           /* dig_T1 */
    put16(bus, 0x8A, (uint16_t)26435); /* dig_T2 */
    put16(bus, 0x8C, (uint16_t)-1000); /* dig_T3 */
    put16(bus, 0x8E, 36477);           /* dig_P1 */
    put16(bus, 0x90, (uint16_t)-10685);/* dig_P2 */
    put16(bus, 0x92, 3024);            /* dig_P3 */
    put16(bus, 0x94, 2855);            /* dig_P4 */
    put16(bus, 0x96, 140);             /* dig_P5 */
    put16(bus, 0x98, (uint16_t)-7);    /* dig_P6 */
    put16(bus, 0x9A, 15500);           /* dig_P7 */
    put16(bus, 0x9C, (uint16_t)-14600);/* dig_P8 */
    put16(bus, 0x9E, 6000);            /* dig_P9 */
    bus->regs[0xA1] = 75;              /* dig_H1 */
    put16(bus, 0xE1, 362);             /* dig_H2 */
    bus->regs[0xE3] = 0;               /* dig_H3 */
    bus->regs[0xE4] = 0x13;            /* dig_H4[11:4] = 313 */
    bus->regs[0xE5] = 0x29;            /* dig_H4[3:0] = 9, dig_H5[3:0] = 2 */
    bus->regs[0xE6] = 0x03;            /* dig_H5[11:4] = 3 */
    bus->regs[0xE7] = 30;              /* dig_H6 */
}

static void prime(struct fake_bus *bus, struct bme280_dev *dev)
{
    memset(bus, 0, sizeof *bus);
    bus->regs[BME280_REG_CHIP_ID] = BME280_CHIP_ID;
    load_datasheet_calibration(bus);
    memset(dev, 0, sizeof *dev);
    dev->addr = BME280_ADDR_SDO_GND;
    dev->ctx = bus;
    dev->read = fake_read;
    dev->write = fake_write;
    dev->delay_ms = fake_delay;
    assert(bme280_init(dev) == BME280_OK);
}

static int32_t diff(int32_t a, int32_t b)
{
    return a > b ? a - b : b - a;
}

int main(void)
{
    struct fake_bus bus;
    struct bme280_dev dev;
    struct bme280_sample sample;
    int32_t temp;
    uint32_t pressure;

    /* datasheet 4.2.3: adc_T 519888 -> 25.08 degC, t_fine 128422 */
    prime(&bus, &dev);
    temp = bme280_compensate_temperature(&dev.calib, 519888);
    assert(diff(temp, 2508) <= 1);
    assert(dev.calib.t_fine == 128422);
    /* datasheet 8.1 double maths gives 100656 Pa for adc_P 415148 */
    pressure = bme280_compensate_pressure(&dev.calib, 415148);
    assert(diff((int32_t)pressure, 25767936) <= 1000);
    assert(diff((int32_t)BME280_PRESSURE_PA(pressure), 100656) <= 4);
    printf("compensate: t=%ld p=%lu\n", (long)temp, (unsigned long)pressure);

    /* humidity lands in the plausible band and never exceeds 100 %RH */
    prime(&bus, &dev);
    (void)bme280_compensate_temperature(&dev.calib, 519888);
    /* datasheet 8.1 double maths gives 55.0 %RH for adc_H 30000 */
    assert(diff((int32_t)bme280_compensate_humidity(&dev.calib, 30000), 56318) <= 1500);
    assert(bme280_compensate_humidity(&dev.calib, 0xFFFF) <= 100u * 1024u);

    /* a 0x58 part must be rejected before calibration is trusted */
    prime(&bus, &dev);
    bus.regs[BME280_REG_CHIP_ID] = 0x58;
    assert(bme280_init(&dev) == BME280_ERR_ID);

    /* forced conversion completes and parses the burst */
    prime(&bus, &dev);
    /* adc_T 519888, adc_P 415148, adc_H 30000 -> 0xF7..0xFE */
    bus.regs[0xF7] = 0x65;
    bus.regs[0xF8] = 0x5A;
    bus.regs[0xF9] = 0xC0;
    bus.regs[0xFA] = 0x7E;
    bus.regs[0xFB] = 0xED;
    bus.regs[0xFC] = 0x00;
    bus.regs[0xFD] = 0x75;
    bus.regs[0xFE] = 0x30;
    assert(bme280_read_forced(&dev, &sample) == BME280_OK);
    assert(diff(sample.temperature_c100, 2508) <= 1);
    assert(sample.humidity_q22_10 > 0);

    /* stuck status bit trips the bounded wait */
    prime(&bus, &dev);
    bus.status_stuck = BME280_STATUS_MEASURING;
    assert(bme280_read_forced(&dev, &sample) == BME280_ERR_TIMEOUT);

    /* argument guards */
    assert(bme280_init(NULL) == BME280_ERR_ARG);
    assert(bme280_read_forced(&dev, NULL) == BME280_ERR_ARG);

    puts("all checks passed");
    return 0;
}
