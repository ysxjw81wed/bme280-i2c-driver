/*
 * bme280_iir.h - host-side PT1 smoothing for pressure samples.
 *
 * The sensor's own IIR filter (datasheet 5.4, config[4:2]) only runs in
 * normal mode and freezes its state when the part sleeps. Nodes that stay in
 * forced mode therefore get the same coefficient ladder here, in software.
 */
#ifndef BME280_IIR_H
#define BME280_IIR_H

#include <stdint.h>

#include "bme280.h"

struct bme280_iir_filt {
    int32_t state;
    uint8_t shift; /* 0 disables the filter */
};

/* Datasheet 5.4 config[4:2] -> shift, 0 for BME280_IIR_OFF. */
uint8_t bme280_iir_shift(enum bme280_iir iir);

/*
 * Condition a new raw pressure sample (Q24.8) with the coefficient a = 2^-shift
 * from datasheet 9.2. Returns the filtered value; state carries over.
 */
uint32_t bme280_iir_apply(struct bme280_iir_filt *f, uint32_t sample);

/* Jump straight to a new level instead of ramping from zero. */
void bme280_iir_reset(struct bme280_iir_filt *f, uint32_t sample);

#endif /* BME280_IIR_H */
