/*
 * bme280_compensate.c - datasheet 4.2.3 integer compensation.
 *
 * The 32-bit reference formulas shift past 32 bits in the pressure path, so
 * those intermediates are 64-bit. Everything is branch-light and runs on a
 * Cortex-M0+ without a divide helper being linked in by accident.
 */
#include "bme280.h"

int32_t bme280_compensate_temperature(struct bme280_calib *calib, int32_t adc_t)
{
    int32_t var1;
    int32_t var2;

    var1 = ((((adc_t >> 3) - ((int32_t)calib->dig_t1 << 1))) *
            ((int32_t)calib->dig_t2)) >> 11;
    var2 = (((((adc_t >> 4) - (int32_t)calib->dig_t1) *
              ((adc_t >> 4) - (int32_t)calib->dig_t1)) >> 12) *
            ((int32_t)calib->dig_t3)) >> 14;
    calib->t_fine = var1 + var2;
    return (calib->t_fine * 5 + 128) >> 8;
}

uint32_t bme280_compensate_pressure(const struct bme280_calib *calib, int32_t adc_p)
{
    int64_t var1;
    int64_t var2;
    int64_t p;

    var1 = ((int64_t)calib->t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)calib->dig_p6;
    var2 = var2 + ((var1 * (int64_t)calib->dig_p5) << 17);
    var2 = var2 + (((int64_t)calib->dig_p4) << 35);
    var1 = ((var1 * var1 * (int64_t)calib->dig_p3) >> 8) +
           ((var1 * (int64_t)calib->dig_p2) << 12);
    var1 = (((((int64_t)1) << 47) + var1) * ((int64_t)calib->dig_p1)) >> 33;
    if (var1 == 0) {
        return 0; /* datasheet 4.2.3: uncalibrated part */
    }
    p = 1048576 - adc_p;
    p = ((p << 31) - var2) * 3125 / var1;
    var1 = (((int64_t)calib->dig_p9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)calib->dig_p8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)calib->dig_p7) << 4);
    return (uint32_t)p;
}

uint32_t bme280_compensate_humidity(const struct bme280_calib *calib, int32_t adc_h)
{
    int64_t v;
    int64_t h;
    int64_t bracket;

    v = (int64_t)calib->t_fine - 76800;
    /* Datasheet 4.2.3: adc_H and dig_H4 are scaled by 2^14 relative to each
     * other, so the raw 32-bit code needs 64-bit products here. */
    h = ((((int64_t)adc_h << 14) - ((int64_t)calib->dig_h4 << 20) -
          (int64_t)calib->dig_h5 * v) + 16384) >> 15;
    bracket = (((v * (int64_t)calib->dig_h6 >> 10) *
                ((v * (int64_t)calib->dig_h3 >> 11) + 32768)) >> 10) + 2097152;
    h = h * (bracket * (int64_t)calib->dig_h2 + 8192) >> 14;
    h -= (((h >> 15) * (h >> 15)) >> 7) * (int64_t)calib->dig_h1 >> 4;
    h = h < 0 ? 0 : h;
    h = h > 419430400 ? 419430400 : h;
    return (uint32_t)(h >> 12);
}
