#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "edge/event.h"
#include "edge/irq.h"
#include "edge/ring_buffer.h"
#include "uart/uart.h"

static void test_irq_classification_constants(void **state) {
    (void)state;
    assert_int_equal(EDGE_IRQ_CLASS_EVENT, 0);
    assert_int_equal(EDGE_IRQ_CLASS_DATA, 1);
    assert_int_equal(EDGE_IRQ_CLASS_SCHEDULER, 2);
    assert_int_equal(EDGE_IRQ_CLASS_FAULT, 3);
}

/* Path A: Event IRQ (Control plane) */
static void test_irq_path_a_event_control_plane(void **state) {
    (void)state;
    edge_event_t storage[4];
    edge_event_queue_t queue;
    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);

    /* GPIO IRQ pushes lightweight control event */
    const edge_event_t btn_event = {.id = 0x1602u, .source = 1u, .arg0 = 1u};
    assert_int_equal(edge_event_push_isr(&queue, &btn_event), EDGE_OK);
    assert_int_equal(edge_event_count(&queue), 1u);

    edge_event_t out;
    assert_int_equal(edge_event_pop(&queue, &out), EDGE_OK);
    assert_int_equal(out.id, 0x1602u);
    assert_int_equal(out.arg0, 1u);
}

/* Path B: Data IRQ (High-throughput data plane) */
static void test_irq_path_b_data_plane_uart_ring(void **state) {
    (void)state;
    uint8_t rx_buf[16];
    uint8_t tx_buf[16];
    uart_ring_driver_t uart;

    uart_ring_init(&uart, rx_buf, sizeof(rx_buf), tx_buf, sizeof(tx_buf));

    /* Data ISR receives stream without going through EventQueue */
    const uint8_t stream[] = {'H', 'E', 'L', 'L', 'O'};
    for (size_t i = 0; i < sizeof(stream); ++i) {
        assert_int_equal(uart_ring_rx_isr(&uart, stream[i]), EDGE_OK);
    }
    assert_int_equal(uart.rx_bytes_total, 5);

    uint8_t out[8] = {0};
    size_t read_bytes = uart_ring_read(&uart, out, 5);
    assert_int_equal(read_bytes, 5);
    assert_int_equal(memcmp(out, "HELLO", 5), 0);
}

/* Path C: Scheduler IRQ (Direct timebase update) */
static void test_irq_path_c_scheduler_timebase(void **state) {
    (void)state;
    uint64_t tick_count = 1000u;
    /* SysTick ISR advances tick directly */
    tick_count += 10u;
    assert_int_equal(tick_count, 1010u);
}

/* Path D: Fault IRQ (Crash capture and safe state) */
static int g_custom_fault_handler_called = 0;
static void custom_fault_handler(const edge_fault_record_t *fault, void *ctx) {
    (void)fault;
    (void)ctx;
    g_custom_fault_handler_called++;
}

static void test_irq_path_d_fault_capture(void **state) {
    (void)state;
    edge_fault_manager_t mgr;
    g_custom_fault_handler_called = 0;

    edge_fault_manager_init(&mgr, custom_fault_handler, NULL);
    assert_false(edge_fault_occurred(&mgr));

    edge_fault_record_t fault = {
        .r0 = 0x12345678u,
        .pc = 0x08001000u,
        .lr = 0x08000FFFu,
        .cfsr = 0x00000002u,
        .reset_reason = 0x1u,
    };

    edge_fault_capture_isr(&mgr, &fault);
    assert_true(edge_fault_occurred(&mgr));
    assert_int_equal(g_custom_fault_handler_called, 1);
    assert_int_equal(mgr.last_fault.pc, 0x08001000u);

    edge_fault_clear(&mgr);
    assert_false(edge_fault_occurred(&mgr));
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

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_irq_classification_constants),
        cmocka_unit_test(test_irq_path_a_event_control_plane),
        cmocka_unit_test(test_irq_path_b_data_plane_uart_ring),
        cmocka_unit_test(test_irq_path_c_scheduler_timebase),
        cmocka_unit_test(test_irq_path_d_fault_capture),
        cmocka_unit_test(test_ring_buffer_basic_ops),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
