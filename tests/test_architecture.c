#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/event.h"
#include "edge/module.h"

static void test_fixed_payload_size(void **state) {
    (void)state;
    assert_int_equal(sizeof(edge_event_t), 24u);
    assert_int_equal(sizeof(((edge_event_t *)0)->id), 4u);
    assert_int_equal(sizeof(((edge_event_t *)0)->timestamp), 8u);
}

static void test_status_codes_are_distinct_negative(void **state) {
    (void)state;
    assert_int_equal(EDGE_OK, 0);
    const edge_status_t codes[] = {EDGE_EINVAL, EDGE_ENOENT,  EDGE_EBUSY,
                                   EDGE_ESTATE, EDGE_EDEPEND, EDGE_EOVERFLOW,
                                   EDGE_ENOSPC, EDGE_EIO,     EDGE_ENOTSUP};
    for (size_t i = 0u; i < sizeof(codes) / sizeof(codes[0]); ++i) {
        assert_true(codes[i] < 0);
        for (size_t j = 0u; j < i; ++j) {
            assert_true(codes[i] != codes[j]);
        }
    }
}

static void test_module_data_accessor(void **state) {
    (void)state;
    edge_module_t module = {0};
    int token = 5;

    assert_null(edge_module_data(NULL));
    assert_null(edge_module_data(&module));
    module.private_data = &token;
    assert_ptr_equal(edge_module_data(&module), &token);
}

static void test_module_contract_size(void **state) {
    (void)state;
    assert_true(sizeof(edge_status_t) >= sizeof(int));
    assert_true(sizeof(edge_module_t) >= sizeof(void *) * 6u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_fixed_payload_size),
        cmocka_unit_test(test_status_codes_are_distinct_negative),
        cmocka_unit_test(test_module_data_accessor),
        cmocka_unit_test(test_module_contract_size),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
