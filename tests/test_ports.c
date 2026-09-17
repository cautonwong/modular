#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/ports.h"
#include "gpio/gpio.h"
#include "uart/uart.h"

static void test_uart_port_shape(void **state) {
    (void)state;
    uint8_t link[8] = {0};
    const uint8_t tx[4] = {0xAAu, 0xBBu, 0xCCu, 0xDDu};
    uint8_t rx[4] = {0};
    edge_uart_port_t uart = {.write = uart_write, .self = link};

    assert_int_equal(uart.write(uart.self, tx, sizeof(tx)), EDGE_OK);
    assert_memory_equal(link, tx, sizeof(tx));
    assert_int_equal(uart_read(link, rx, sizeof(rx)), EDGE_OK);
    assert_memory_equal(rx, tx, sizeof(rx));

    assert_int_equal(uart_write(NULL, tx, sizeof(tx)), EDGE_EINVAL);
    assert_int_equal(uart_write(link, NULL, sizeof(tx)), EDGE_EINVAL);
    assert_int_equal(uart_read(link, NULL, sizeof(rx)), EDGE_EINVAL);
}

static void test_gpio_port_shape(void **state) {
    (void)state;
    uint8_t level_state[4] = {0};
    bool level = false;
    edge_gpio_port_t gpio = {.write = gpio_write, .read = gpio_read, .self = level_state};

    assert_int_equal(gpio.write(gpio.self, 2u, true), EDGE_OK);
    assert_int_equal(gpio.read(gpio.self, 2u, &level), EDGE_OK);
    assert_true(level);
    assert_int_equal(gpio.write(gpio.self, 2u, false), EDGE_OK);
    assert_int_equal(gpio.read(gpio.self, 2u, &level), EDGE_OK);
    assert_false(level);

    assert_int_equal(gpio_write(NULL, 0u, true), EDGE_EINVAL);
    assert_int_equal(gpio_read(level_state, 0u, NULL), EDGE_EINVAL);
}

static void test_canonical_shapes_default_null(void **state) {
    (void)state;
    edge_storage_kv_t kv = {0};
    edge_byte_reader_t reader = {0};
    edge_byte_writer_t writer = {0};

    assert_null(kv.read);
    assert_null(kv.write);
    assert_null(reader.read);
    assert_null(writer.write);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_uart_port_shape),
        cmocka_unit_test(test_gpio_port_shape),
        cmocka_unit_test(test_canonical_shapes_default_null),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
