#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "hrs3300/hrs3300.h"

static void test_hrs3300_unpack_valid(void **state) {
    (void)state;
    // Construct a known register sequence
    // ALS: h_als = 0x05, m_als = 0x12, l_als = 0x03 -> (0x05 << 11) | (0x12 << 3) | 0x03
    // HRS: h_hrs = 0x0A, m_hrs = 0x34, l_hrs = 0x0B -> (0x34 << 8) | (0x0A << 4) | 0x0B
    uint8_t raw[8] = {
        0x12, // [0] C1DataM (als)
        0x34, // [1] C0DataM (hrs)
        0x0A, // [2] C0DataH (hrs)
        0x00, // [3] unused
        0x2F, // [4] PDriver
        0x05, // [5] C1DataH (als)
        0x03, // [6] C1DataL (als)
        0x0B  // [7] C0DataL (hrs)
    };

    hrs3300_sample_t sample;
    assert_int_equal(hrs3300_unpack_burst(raw, &sample), EDGE_OK);

    uint16_t expected_hrs = (0x34u << 8) | (0x0Au << 4) | 0x0Bu;
    uint16_t expected_als = ((0x05u & 0x3Fu) << 11) | (0x12u << 3) | 0x03u;

    assert_int_equal(sample.hrs, expected_hrs);
    assert_int_equal(sample.als, expected_als);
}

static void test_hrs3300_unpack_null(void **state) {
    (void)state;
    hrs3300_sample_t sample;
    uint8_t raw[8] = {0};
    assert_int_equal(hrs3300_unpack_burst(NULL, &sample), EDGE_EINVAL);
    assert_int_equal(hrs3300_unpack_burst(raw, NULL), EDGE_EINVAL);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_hrs3300_unpack_valid),
        cmocka_unit_test(test_hrs3300_unpack_null),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
