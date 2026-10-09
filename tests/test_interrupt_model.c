#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "edge/irq.h"
#include "edge/ring_buffer.h"

static void test_irq_classification_constants(void **state) {
    (void)state;
    assert_int_equal(EDGE_IRQ_CLASS_EVENT, 0);
    assert_int_equal(EDGE_IRQ_CLASS_DATA, 1);
    assert_int_equal(EDGE_IRQ_CLASS_SCHEDULER, 2);
    assert_int_equal(EDGE_IRQ_CLASS_FAULT, 3);
}

static void test_ring_buffer_basic_ops(void **state) {
    (void)state;
    uint8_t buffer[8];
    edge_ring_buffer_t rb;

    edge_ring_buffer_init(&rb, buffer, sizeof(buffer));
    assert_true(edge_ring_buffer_is_empty(&rb));
    assert_false(edge_ring_buffer_is_full(&rb));
    assert_int_equal(edge_ring_buffer_size(&rb), 0);
    assert_int_equal(edge_ring_buffer_free_space(&rb), 7);

    /* Write 3 bytes */
    const uint8_t data_in[] = {0xAA, 0xBB, 0xCC};
    size_t written = edge_ring_buffer_write(&rb, data_in, sizeof(data_in));
    assert_int_equal(written, 3);
    assert_int_equal(edge_ring_buffer_size(&rb), 3);
    assert_false(edge_ring_buffer_is_empty(&rb));

    /* Read 2 bytes */
    uint8_t data_out[4] = {0};
    size_t read_bytes = edge_ring_buffer_read(&rb, data_out, 2);
    assert_int_equal(read_bytes, 2);
    assert_int_equal(data_out[0], 0xAA);
    assert_int_equal(data_out[1], 0xBB);
    assert_int_equal(edge_ring_buffer_size(&rb), 1);

    /* Fill up ring buffer to trigger overflow */
    const uint8_t overflow_data[] = {1, 2, 3, 4, 5, 6, 7, 8};
    written = edge_ring_buffer_write(&rb, overflow_data, sizeof(overflow_data));
    assert_true(edge_ring_buffer_is_full(&rb));
    assert_true(rb.overflow_count > 0);
}

static void test_fault_record_struct(void **state) {
    (void)state;
    edge_fault_record_t record = {
        .r0 = 0x11111111u,
        .pc = 0x08000100u,
        .lr = 0x08000200u,
        .cfsr = 0x00000001u,
        .reset_reason = 0x4u,
    };

    assert_int_equal(record.pc, 0x08000100u);
    assert_int_equal(record.reset_reason, 0x4u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_irq_classification_constants),
        cmocka_unit_test(test_ring_buffer_basic_ops),
        cmocka_unit_test(test_fault_record_struct),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
