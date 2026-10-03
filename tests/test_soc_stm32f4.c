/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "soc_stm32f4/soc_stm32f4.h"

static void test_soc_stm32f4_gpio(void **state) {
    (void)state;
    soc_stm32f4_gpio_regs_t gpio = {0};

    soc_stm32f4_gpio_set_mode(&gpio, 13u, SOC_STM32F4_GPIO_MODE_OUTPUT);
    assert_int_equal(gpio.moder & (3u << 26u), (1u << 26u));

    soc_stm32f4_gpio_write(&gpio, 13u, true);
    assert_int_equal(gpio.bsrr & (1u << 13u), (1u << 13u));
    assert_int_equal(gpio.odr & (1u << 13u), (1u << 13u));

    soc_stm32f4_gpio_toggle(&gpio, 13u);
    assert_int_equal(gpio.odr & (1u << 13u), 0u);

    gpio.idr = (1u << 0u);
    assert_true(soc_stm32f4_gpio_read(&gpio, 0u));
    assert_false(soc_stm32f4_gpio_read(&gpio, 1u));
}

static void test_soc_stm32f4_iwdg(void **state) {
    (void)state;
    soc_stm32f4_iwdg_regs_t iwdg = {0};

    soc_stm32f4_iwdg_start(&iwdg, 1000u);
    assert_int_equal(iwdg.pr, 0x04u);
    assert_int_equal(iwdg.rlr, 1000u);
    assert_int_equal(iwdg.kr, SOC_STM32F4_IWDG_KEY_RELOAD);

    soc_stm32f4_iwdg_feed(&iwdg);
    assert_int_equal(iwdg.kr, SOC_STM32F4_IWDG_KEY_RELOAD);
}

static void test_soc_stm32f4_spi(void **state) {
    (void)state;
    soc_stm32f4_spi_regs_t spi = {0};

    assert_true(soc_stm32f4_spi_init(&spi, 0x0004u));
    assert_int_equal(spi.cr1 & 0x0040u, 0x0040u);

    uint8_t tx[3] = {0xDE, 0xAD, 0xBE};
    uint8_t rx[3] = {0};
    assert_true(soc_stm32f4_spi_transfer(&spi, tx, rx, 3u));
    assert_int_equal(rx[0], 0xDE);
    assert_int_equal(rx[1], 0xAD);
    assert_int_equal(rx[2], 0xBE);
}

static void test_soc_stm32f4_i2c(void **state) {
    (void)state;
    soc_stm32f4_i2c_regs_t i2c = {0};

    assert_true(soc_stm32f4_i2c_init(&i2c, 0x0028u));
    assert_int_equal(i2c.cr1, 0x0001u);
    assert_int_equal(i2c.ccr, 0x0028u);

    uint8_t tx[2] = {0x12, 0x34};
    assert_true(soc_stm32f4_i2c_write(&i2c, 0x68, tx, 2u));
    assert_int_equal(i2c.dr, 0x34);

    uint8_t rx[2] = {0};
    assert_true(soc_stm32f4_i2c_read(&i2c, 0x68, rx, 2u));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_soc_stm32f4_gpio),
        cmocka_unit_test(test_soc_stm32f4_iwdg),
        cmocka_unit_test(test_soc_stm32f4_spi),
        cmocka_unit_test(test_soc_stm32f4_i2c),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
