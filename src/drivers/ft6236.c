// FT6236 touch controller on the Presto. Pins, address and register layout as in Pimoroni's
// driver (pimoroni/presto modules/py_frozen/touch.py, MIT).
#include "ft6236.h"

#include "pico/stdlib.h"
#include "hardware/i2c.h"

#define TOUCH_I2C   i2c1
#define TOUCH_SDA   30
#define TOUCH_SCL   31
#define TOUCH_INT   32
#define TOUCH_ADDR  0x48
#define I2C_HZ      400000
#define I2C_TIMEOUT_US 2000

// After this many failed reads in a row, unstick the bus (a controller interrupted mid-transfer
// can hold SDA low) and skip a few polls rather than stall the main loop on every one.
#define MAX_ERRORS   3
#define BACKOFF_POLLS 20

enum { EVENT_DOWN = 0, EVENT_UP = 1, EVENT_CONTACT = 2, EVENT_NONE = 3 };

static int errors, skip;

static void bus_init(void) {
    i2c_init(TOUCH_I2C, I2C_HZ);
    gpio_set_function(TOUCH_SDA, GPIO_FUNC_I2C);
    gpio_set_function(TOUCH_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(TOUCH_SDA);
    gpio_pull_up(TOUCH_SCL);
}

static void bus_recover(void) {
    gpio_init(TOUCH_SCL);
    gpio_set_dir(TOUCH_SCL, GPIO_OUT);
    gpio_put(TOUCH_SCL, 1);
    gpio_init(TOUCH_SDA);
    gpio_set_dir(TOUCH_SDA, GPIO_IN);
    gpio_pull_up(TOUCH_SDA);
    for (int i = 0; i < 9 && !gpio_get(TOUCH_SDA); i++) {
        gpio_put(TOUCH_SCL, 0);
        sleep_us(5);
        gpio_put(TOUCH_SCL, 1);
        sleep_us(5);
    }
    bus_init();
}

void ft6236_init(void) {
    bus_init();
    gpio_init(TOUCH_INT);
    gpio_set_dir(TOUCH_INT, GPIO_IN);
    gpio_pull_up(TOUCH_INT);
}

bool ft6236_poll(touch_state_t *state) {
    if (skip) {
        skip--;
        return false;
    }
    if (gpio_get(TOUCH_INT) && state->count == 0) return true;     // nothing new

    uint8_t reg = 0, buf[15];
    if (i2c_write_timeout_us(TOUCH_I2C, TOUCH_ADDR, &reg, 1, true, I2C_TIMEOUT_US) != 1 ||
        i2c_read_timeout_us(TOUCH_I2C, TOUCH_ADDR, buf, sizeof buf, false, I2C_TIMEOUT_US) != (int)sizeof buf) {
        if (++errors >= MAX_ERRORS) {
            bus_recover();
            errors = 0;
            skip = BACKOFF_POLLS;
        }
        return false;
    }
    errors = 0;

    // buf[2]: touches; then 6 bytes a touch: event<<6 | x[11:8], x[7:0], id<<4 | y[11:8], y[7:0], ...
    touch_state_t s = {0};
    int touches = buf[2] & 0x0f;
    if (touches > 2) touches = 2;
    for (int n = 0; n < touches; n++) {
        const uint8_t *d = &buf[3 + n * 6];
        int event = d[0] >> 6;
        int id = (d[2] >> 4) & 1;
        touch_point_t *p = &s.p[id];
        p->x = ((d[0] & 0x0f) << 8) | d[1];
        p->y = ((d[2] & 0x0f) << 8) | d[3];
        p->down = event != EVENT_UP && event != EVENT_NONE;
        if (p->x > 479) p->x = 479;
        if (p->y > 479) p->y = 479;
    }
    s.count = s.p[0].down + s.p[1].down;
    *state = s;
    return true;
}
