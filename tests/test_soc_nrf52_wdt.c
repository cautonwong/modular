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

static soc_nrf52_wdt_regs_t wdt_regs;

static void test_soc_nrf52_wdt(void **state) {
    (void)state;
    memset(&wdt_regs, 0, sizeof(wdt_regs));

    assert_true(soc_nrf52_wdt_init(&wdt_regs, 7u));
    assert_int_equal(wdt_regs.crv, 7u * 32768u - 1u);
    assert_int_equal(wdt_regs.config, 1u);
    assert_int_equal(wdt_regs.rren, 1u);
    assert_int_equal(wdt_regs.tasks_start, 1u);

    soc_nrf52_wdt_feed(&wdt_regs);
    assert_int_equal(wdt_regs.rr[0], SOC_NRF52_WDT_RR_VALUE);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_soc_nrf52_wdt),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
