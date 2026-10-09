/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "soc_rp2040/soc_rp2040.h"

static void test_soc_rp2040_sio_gpio(void **state) {
    (void)state;
    soc_rp2040_sio_regs_t sio = {0};

    soc_rp2040_gpio_init(&sio, 15u, true);
    assert_int_equal(sio.gpio_oe & (1u << 15u), (1u << 15u));

    soc_rp2040_gpio_put(&sio, 15u, true);
    assert_int_equal(sio.gpio_out & (1u << 15u), (1u << 15u));

    soc_rp2040_gpio_toggle(&sio, 15u);
    assert_int_equal(sio.gpio_out & (1u << 15u), 0u);

    soc_rp2040_gpio_init(&sio, 15u, false);
    assert_int_equal(sio.gpio_oe & (1u << 15u), 0u);

    sio.gpio_in = (1u << 8u);
    assert_true(soc_rp2040_gpio_get(&sio, 8u));
    assert_false(soc_rp2040_gpio_get(&sio, 9u));
}

static void test_soc_rp2040_watchdog(void **state) {
    (void)state;
    soc_rp2040_wdt_regs_t wdt = {0};

    soc_rp2040_wdt_start(&wdt, 1000u);
    assert_int_equal(wdt.load, 2000000u);
    assert_int_equal(wdt.ctrl & 0x40000000u, 0x40000000u);

    /* Simulate countdown */
    wdt.load = 500u;
    soc_rp2040_wdt_feed(&wdt);
    assert_int_equal(wdt.load, 2000000u);
    assert_int_equal(wdt.ctrl & 0x40000000u, 0x40000000u);

    soc_rp2040_wdt_reboot(&wdt, 0x10000100u, 0x20040000u, 50u);
    assert_int_equal(wdt.scratch[4], 0x10000100u);
    assert_int_equal(wdt.scratch[5], 0x20040000u);
    assert_int_equal(wdt.load, 100000u);
}

static void test_soc_rp2040_spi(void **state) {
    (void)state;
    soc_rp2040_spi_regs_t spi = {0};

    assert_true(soc_rp2040_spi_init(&spi, 1000000u, 8u));
    assert_int_equal(spi.sspcr0, 7u);
    assert_int_equal(spi.sspcr1, 0x02u);

    const uint8_t tx[3] = {0x01, 0x02, 0x03};
    uint8_t rx[3] = {0};
    assert_true(soc_rp2040_spi_transfer(&spi, tx, rx, 3u));
    assert_int_equal(rx[0], 0x01);
    assert_int_equal(rx[1], 0x02);
    assert_int_equal(rx[2], 0x03);
}

static void test_soc_rp2040_i2c(void **state) {
    (void)state;
    soc_rp2040_i2c_regs_t i2c = {0};

    assert_true(soc_rp2040_i2c_init(&i2c, 400000u));
    assert_int_equal(i2c.ic_con, 0x65u);
    assert_int_equal(i2c.ic_enable, 1u);

    const uint8_t tx[2] = {0xAA, 0x55};
    assert_true(soc_rp2040_i2c_write(&i2c, 0x3C, tx, 2u));
    assert_int_equal(i2c.ic_tar, 0x3C);

    uint8_t rx[2] = {0};
    assert_true(soc_rp2040_i2c_read(&i2c, 0x3C, rx, 2u));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_soc_rp2040_sio_gpio),
        cmocka_unit_test(test_soc_rp2040_watchdog),
        cmocka_unit_test(test_soc_rp2040_spi),
        cmocka_unit_test(test_soc_rp2040_i2c),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
