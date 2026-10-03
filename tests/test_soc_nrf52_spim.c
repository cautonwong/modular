/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "soc_nrf52/soc_nrf52.h"

static soc_nrf52_spim_regs_t spim_regs;

static void test_soc_nrf52_spim_init(void **state) {
    (void)state;
    memset(&spim_regs, 0, sizeof(spim_regs));

    soc_nrf52_spim_config_t cfg = {
        .pin_sck = 2u,
        .pin_mosi = 3u,
        .pin_miso = 4u,
        .frequency_code = 0x80000000u, /* 8 MHz */
        .spi_mode = 0u,
    };

    assert_true(soc_nrf52_spim_init(&spim_regs, &cfg));
    assert_int_equal(spim_regs.enable, 7u);
    assert_int_equal(spim_regs.psel_sck, 2u);
    assert_int_equal(spim_regs.psel_mosi, 3u);
    assert_int_equal(spim_regs.psel_miso, 4u);
    assert_int_equal(spim_regs.frequency, 0x80000000u);
    assert_int_equal(spim_regs.config, 0u);

    assert_false(soc_nrf52_spim_init(NULL, &cfg));
    assert_false(soc_nrf52_spim_init(&spim_regs, NULL));
}

static void test_soc_nrf52_spim_transfer(void **state) {
    (void)state;
    memset(&spim_regs, 0, sizeof(spim_regs));

    soc_nrf52_spim_config_t cfg = {
        .pin_sck = 2u,
        .pin_mosi = 3u,
        .pin_miso = 0xFFu,
        .frequency_code = 0x80000000u,
        .spi_mode = 3u,
    };
    assert_true(soc_nrf52_spim_init(&spim_regs, &cfg));

    uint8_t tx[4] = {0x11, 0x22, 0x33, 0x44};
    uint8_t rx[4] = {0};

    assert_true(soc_nrf52_spim_transfer(&spim_regs, tx, sizeof(tx), rx, sizeof(rx)));
    assert_int_equal(spim_regs.txd_ptr, (uint32_t)(uintptr_t)tx);
    assert_int_equal(spim_regs.txd_maxcnt, sizeof(tx));
    assert_int_equal(spim_regs.rxd_ptr, (uint32_t)(uintptr_t)rx);
    assert_int_equal(spim_regs.rxd_maxcnt, sizeof(rx));
    assert_int_equal(spim_regs.events_end, 1u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_soc_nrf52_spim_init),
        cmocka_unit_test(test_soc_nrf52_spim_transfer),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
