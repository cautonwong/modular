#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "gpio/gpio.h"

static void test_gpio_write(void **state) {
    (void)state;
    uint8_t buf[4] = {0};

    assert_int_equal(gpio_write(NULL, 0u, true), EDGE_EINVAL);
    assert_int_equal(gpio_write(buf, 1u, true), EDGE_OK);
    assert_int_equal(buf[1], 1u);
    assert_int_equal(gpio_write(buf, 1u, false), EDGE_OK);
    assert_int_equal(buf[1], 0u);
    assert_int_equal(gpio_write(buf, 3u, true), EDGE_OK);
    assert_int_equal(buf[3], 1u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_gpio_write),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
