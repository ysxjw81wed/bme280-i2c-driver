/*
 * bme280_iir.c - see bme280_iir.h.
 */
#include "bme280_iir.h"

uint8_t bme280_iir_shift(enum bme280_iir iir)
{
    if (iir > BME280_IIR_16) {
        return 4;
    }
    return (uint8_t)iir; /* OFF..X16 map 1:1 onto shift 0..4 */
}

uint32_t bme280_iir_apply(struct bme280_iir_filt *f, uint32_t sample)
{
    int32_t diff;

    if (f->shift == 0) {
        return sample;
    }
    diff = (int32_t)((int64_t)sample - f->state);
    /* Round to nearest so a steady input converges instead of drifting. */
    f->state += (int32_t)(((int64_t)diff + (1 << (f->shift - 1))) >> f->shift);
    return (uint32_t)f->state;
}

void bme280_iir_reset(struct bme280_iir_filt *f, uint32_t sample)
{
    f->state = (int32_t)sample;
}
