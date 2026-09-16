#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/event.h"
#include "edge/module.h"
#include "edge/modules.h"
#include "edge/ports.h"

static edge_status_t fake_kv_read(void *self, uint32_t key, void *buf, size_t len) {
    (void)self;
    (void)key;
    (void)buf;
    (void)len;
    return EDGE_OK;
}

static edge_status_t fake_kv_write(void *self, uint32_t key, const void *buf, size_t len) {
    (void)self;
    (void)key;
    (void)buf;
    (void)len;
    return EDGE_OK;
}

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

static void test_error_segment_allocation(void **state) {
    (void)state;
    assert_true(EDGE_ERR(EDGE_MOD_DLT645, 1u) < EDGE_EINVAL);
    assert_int_equal(EDGE_ERR(EDGE_MOD_DLT645, 1u), -(int32_t)0x1001);
    assert_int_equal(EDGE_ERR(0xFFFFu, 0u), -(int32_t)0xFF00);
}

static void test_module_id_segments(void **state) {
    (void)state;
    assert_int_equal(EDGE_MODULE_SEGMENT(EDGE_MOD_DLT645), EDGE_MOD_DLT645);
    assert_int_equal(EDGE_MODULE_SEGMENT(EDGE_MOD_DLMS), EDGE_MOD_DLMS);
    assert_int_equal(EDGE_MODULE_SEGMENT(EDGE_MOD_RELAY), EDGE_MOD_RELAY);
    assert_true(EDGE_MOD_DLT645 != EDGE_MOD_DLMS);
    assert_true(EDGE_MOD_DLMS != EDGE_MOD_RELAY);
}

static void test_narrow_ports(void **state) {
    (void)state;
    int token = 0;
    uint8_t buf[4] = {0};
    edge_storage_kv_t kv = {.read = fake_kv_read, .write = fake_kv_write, .self = &token};
    edge_byte_reader_t reader = {.read = NULL, .self = NULL};
    edge_byte_writer_t writer = {.write = NULL, .self = NULL};

    assert_int_equal(kv.read(kv.self, 1u, buf, sizeof buf), EDGE_OK);
    assert_int_equal(kv.write(kv.self, 1u, buf, sizeof buf), EDGE_OK);
    assert_null(reader.read);
    assert_null(writer.write);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_fixed_payload_size),
        cmocka_unit_test(test_status_codes_are_distinct_negative),
        cmocka_unit_test(test_module_data_accessor),
        cmocka_unit_test(test_module_contract_size),
        cmocka_unit_test(test_error_segment_allocation),
        cmocka_unit_test(test_module_id_segments),
        cmocka_unit_test(test_narrow_ports),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
