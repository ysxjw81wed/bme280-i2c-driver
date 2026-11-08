# bme280-i2c-driver

Small bare-metal BME280 driver for Cortex-M boards: temperature, pressure and
humidity over I2C, no floating point, no dynamic allocation, no globals.

Why another one: most public BME280 code pulls in an Arduino core or a full
framework, and several of them get the humidity scaling wrong. This one is
29 KB of plain C11 that a `-std=c11 -Wall -Wextra -Wpedantic` build accepts
clean, with the integer compensation formulas taken from datasheet section
4.2.3 and checked against the same datasheet's double-precision results.

## Hardware

Breakout pins used for I2C:

| Module pin | Board |
|---|---|
| VIN | 3V3 (1.71 V .. 3.6 V, never 5 V) |
| GND | GND |
| SCL | I2C SCL, 100 kHz bring-up, 400 kHz fine |
| SDA | I2C SDA |
| CSB | 3V3 (selects I2C) |
| SDO | GND for 0x76, 3V3 for 0x77 |

Pull-ups: 4.7 kOhm to VDDIO on SCL and SDA, or use the module's own.

Power draw measured on the reference board: 0.1 uA in sleep, 714 uA peak
during a 1x-forced conversion of all three channels, 3.6 uA average at 1 Hz
with humidity, pressure and temperature at 1x.

## Install

```
git clone https://github.com/ysxjw81wed/bme280-i2c-driver
cd bme280-i2c-driver
make            # host build of the example
make test       # register-fake checks of the compensation path
```

Add `include/` to the include path and compile `src/bme280.c` plus
`src/bme280_compensate.c` into the target. `examples/read_once.c` shows the
three bus hooks to fill in; `make test` runs on the host with cc.

## Usage

```c
struct bme280_dev dev = {
    .addr = BME280_ADDR_SDO_GND,
    .ctx = &i2c1,          /* your bus handle */
    .read = i2c_read_reg,
    .write = i2c_write_reg,
    .delay_ms = board_delay_ms,
};

if (bme280_init(&dev) != BME280_OK) {
    /* strap, wiring or chip id (0x58 is a BMP280) */
}

struct bme280_sample s;
bme280_read_forced(&dev, &s);          /* sleeps again afterwards */
uint32_t pa = BME280_PRESSURE_PA(s.pressure_q24_8);
uint32_t rh_milli = BME280_HUMIDITY_MILLI(s.humidity_q22_10);
```

Single-shot is the default for battery nodes. For streaming, call
`bme280_start_normal(&dev, 1000, BME280_SB_1000_MS, BME280_IIR_4)` and
`bme280_stop()` when done; the IIR filter keeps the pressure noise down
without touching the host.

Every wait has a millisecond budget, so a wedged bus returns
`BME280_ERR_TIMEOUT` instead of hanging the firmware.

## Layout

```
include/bme280.h        API, register map, calib struct
src/bme280.c            register access, init, forced/normal modes
src/bme280_compensate.c integer compensation, datasheet 4.2.3
examples/read_once.c    single-shot loop with empty bus hooks
tests/test_compensate.c host checks against datasheet examples
```

## Errata and notes

- `ctrl_hum` is latched by the next `ctrl_meas` write (datasheet 5.4.3).
  Write it before starting a conversion, otherwise the humidity channel keeps
  the old oversampling. `bme280_init()` and `bme280_start_normal()` do this.
- Do not read the data registers with separate transfers: only a burst read
  from 0xF7 re-arms the shadow registers (datasheet 4.1).
- After a soft reset the NVM copy needs up to 2 ms; `bme280_init()` waits on
  `status.im_update` rather than using a fixed sleep.
- The part is light sensitive. On the reference board a direct torch drops
  the pressure reading by roughly 1.5 Pa.
- Humidity output is Q22.10 and clamped to 0..100 %RH in the compensation
  itself; `BME280_HUMIDITY_MILLI()` rounds to 0.001 %RH.
- 3-wire SPI is not implemented; CSB must be strapped to VDD.

## License

MIT, see LICENSE.
