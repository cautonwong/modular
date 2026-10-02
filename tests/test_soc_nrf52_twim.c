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

static soc_nrf52_twim_regs_t twim_regs;

static void test_soc_nrf52_twim_init_and_io(void **state) {
    (void)state;
    memset(&twim_regs, 0, sizeof(twim_regs));

    soc_nrf52_twim_config_t cfg = {
        .pin_scl = 7u, .pin_sda = 6u, .frequency_code = 0x06680000u, /* 400 kHz */
    };

    assert_true(soc_nrf52_twim_init(&twim_regs, &cfg));
    assert_int_equal(twim_regs.enable, 6u);
    assert_int_equal(twim_regs.psel_scl, 7u);
    assert_int_equal(twim_regs.psel_sda, 6u);
    assert_int_equal(twim_regs.frequency, 0x06680000u);

    uint8_t tx[2] = {0x01, 0x02};
    uint8_t rx[4] = {0};

    assert_true(soc_nrf52_twim_write(&twim_regs, 0x15, tx, sizeof(tx), false));
    assert_int_equal(twim_regs.address, 0x15);
    assert_int_equal(twim_regs.txd_ptr, (uint32_t)(uintptr_t)tx);
    assert_int_equal(twim_regs.txd_maxcnt, sizeof(tx));
    assert_int_equal(twim_regs.events_stopped, 1u);

    assert_true(soc_nrf52_twim_read(&twim_regs, 0x15, rx, sizeof(rx)));
    assert_int_equal(twim_regs.address, 0x15);
    assert_int_equal(twim_regs.rxd_ptr, (uint32_t)(uintptr_t)rx);
    assert_int_equal(twim_regs.rxd_maxcnt, sizeof(rx));
    assert_int_equal(twim_regs.events_stopped, 1u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_soc_nrf52_twim_init_and_io),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
