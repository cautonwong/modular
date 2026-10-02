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

static soc_nrf52_saadc_regs_t saadc_regs;

static void test_soc_nrf52_saadc_init_and_sample(void **state) {
    (void)state;
    memset(&saadc_regs, 0, sizeof(saadc_regs));

    soc_nrf52_saadc_config_t cfg = {
        .channel = 0u,
        .ain_pin = 7u,    /* AIN7 */
        .resolution = 2u, /* 12-bit */
        .gain_and_ref = 0x00020000u,
    };

    assert_true(soc_nrf52_saadc_init(&saadc_regs, &cfg));
    assert_int_equal(saadc_regs.enable, 1u);
    assert_int_equal(saadc_regs.ch[0].pselp, 8u); /* AIN7 is 8 */
    assert_int_equal(saadc_regs.resolution, 2u);

    int16_t sample = 0;
    assert_int_equal(soc_nrf52_saadc_sample_blocking(&saadc_regs, &sample), 0);
    assert_int_equal(sample, 650);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_soc_nrf52_saadc_init_and_sample),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
