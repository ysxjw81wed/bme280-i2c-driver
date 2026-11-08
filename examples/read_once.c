/*
 * read_once.c - forced-mode single shot on a 0x76 device.
 *
 * Replace the bus_* helpers with the I2C peripheral of your target. The
 * driver never stores anything globally, so the same bus helpers serve as
 * many struct bme280_dev instances as you have sensors.
 */
#include <stdio.h>

#include "bme280.h"

static int bus_read(void *ctx, uint8_t addr, uint8_t reg, uint8_t *buf, size_t len);
static int bus_write(void *ctx, uint8_t addr, uint8_t reg, const uint8_t *buf, size_t len);
static void bus_delay(void *ctx, uint32_t ms);

static struct bme280_dev sensor = {
    .addr = BME280_ADDR_SDO_GND,
    .ctx = NULL,
    .read = bus_read,
    .write = bus_write,
    .delay_ms = bus_delay,
};

int main(void)
{
    struct bme280_sample sample;
    int err = bme280_init(&sensor);

    if (err != BME280_OK) {
        return 1; /* wiring, strap or chip id mismatch */
    }
    for (;;) {
        err = bme280_read_forced(&sensor, &sample);
        if (err == BME280_OK) {
            printf("t=%ld.%02ld C p=%lu Pa rh=%lu.%03lu %%\n",
                   (long)(sample.temperature_c100 / 100),
                   (long)(sample.temperature_c100 % 100),
                   (unsigned long)BME280_PRESSURE_PA(sample.pressure_q24_8),
                   (unsigned long)(BME280_HUMIDITY_MILLI(sample.humidity_q22_10) / 1000),
                   (unsigned long)(BME280_HUMIDITY_MILLI(sample.humidity_q22_10) % 1000));
        }
        bus_delay(NULL, 1000);
    }
}

static int bus_read(void *ctx, uint8_t addr, uint8_t reg, uint8_t *buf, size_t len)
{
    (void)ctx;
    (void)addr;
    (void)reg;
    (void)buf;
    (void)len;
    return 0;
}

static int bus_write(void *ctx, uint8_t addr, uint8_t reg, const uint8_t *buf, size_t len)
{
    (void)ctx;
    (void)addr;
    (void)reg;
    (void)buf;
    (void)len;
    return 0;
}

static void bus_delay(void *ctx, uint32_t ms)
{
    (void)ctx;
    (void)ms;
}
