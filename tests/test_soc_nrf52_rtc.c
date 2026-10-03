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

static soc_nrf52_rtc_regs_t rtc_regs;

static void test_soc_nrf52_rtc(void **state) {
    (void)state;
    memset(&rtc_regs, 0, sizeof(rtc_regs));

    assert_true(soc_nrf52_rtc_init(&rtc_regs, 0u));
    assert_int_equal(rtc_regs.prescaler, 0u);
    assert_int_equal(rtc_regs.counter, 0u);

    soc_nrf52_rtc_start(&rtc_regs);
    assert_int_equal(rtc_regs.tasks_start, 1u);

    rtc_regs.counter = 12345u;
    assert_int_equal(soc_nrf52_rtc_get_counter(&rtc_regs), 12345u);

    soc_nrf52_rtc_stop(&rtc_regs);
    assert_int_equal(rtc_regs.tasks_stop, 1u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_soc_nrf52_rtc),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
