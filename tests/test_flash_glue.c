#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "dlt645/dlt645.h"
#include "flash/flash.h"

void product_example_make_storage(dlt645_storage_if_t *out, void *flash_state);

static void test_flash_roundtrip(void **state) {
    (void)state;
    uint8_t store[8] = {0};
    const uint8_t input[4] = {1u, 2u, 3u, 4u};
    uint8_t output[4] = {0};

    assert_int_equal(flash_write(store, 0u, input, sizeof(input)), EDGE_OK);
    assert_int_equal(flash_read(store, 0u, output, sizeof(output)), EDGE_OK);
    assert_memory_equal(input, output, sizeof(input));

    assert_int_equal(flash_read(NULL, 0u, output, sizeof(output)), EDGE_EINVAL);
    assert_int_equal(flash_read(store, 0u, NULL, sizeof(output)), EDGE_EINVAL);
    assert_int_equal(flash_write(store, 0u, NULL, sizeof(input)), EDGE_EINVAL);
}

static void test_glue_adapter_roundtrip(void **state) {
    (void)state;
    uint8_t store[8] = {0};
    const uint8_t input[3] = {9u, 8u, 7u};
    uint8_t output[3] = {0};
    dlt645_storage_if_t port;

    product_example_make_storage(&port, store);
    assert_non_null(port.read);
    assert_non_null(port.write);
    assert_ptr_equal(port.self, store);

    assert_int_equal(port.write(port.self, 1u, input, sizeof(input)), EDGE_OK);
    assert_int_equal(port.read(port.self, 1u, output, sizeof(output)), EDGE_OK);
    assert_memory_equal(input, output, sizeof(input));
}

static void test_glue_null_out_is_noop(void **state) {
    (void)state;
    product_example_make_storage(NULL, NULL);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_flash_roundtrip),
        cmocka_unit_test(test_glue_adapter_roundtrip),
        cmocka_unit_test(test_glue_null_out_is_noop),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
