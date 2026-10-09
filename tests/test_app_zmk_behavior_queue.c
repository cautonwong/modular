/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/app_contract.h"
#include "edge/modules.h"
#include "zmk_behavior_queue/behavior_queue.h"

typedef struct mock_sink {
    int calls;
    uint16_t last_behavior_id;
    uint32_t last_param1;
    uint32_t last_param2;
    bool last_press;
    uint32_t last_pos;
    uint32_t last_time;
    edge_status_t rc;
} mock_sink_t;

static edge_status_t mock_invoke_binding(void *self, uint16_t behavior_id, uint32_t param1,
                                         uint32_t param2, bool press, uint32_t position,
                                         uint32_t timestamp_ms) {
    mock_sink_t *sink = (mock_sink_t *)self;
    sink->calls++;
    sink->last_behavior_id = behavior_id;
    sink->last_param1 = param1;
    sink->last_param2 = param2;
    sink->last_press = press;
    sink->last_pos = position;
    sink->last_time = timestamp_ms;
    return sink->rc;
}

static void test_behavior_queue_contract_and_init(void **state) {
    (void)state;
    mock_sink_t sink_ctx = {0};
    zmk_behavior_queue_sink_if_t sink = {.self = &sink_ctx, .invoke_binding = mock_invoke_binding};

    zmk_behavior_queue_app_t app;
    zmk_behavior_queue_construct(&app, EDGE_MOD_ZMK_BEHAVIOR_QUEUE, 40, &sink);

    assert_int_equal(app.module.module_id, EDGE_MOD_ZMK_BEHAVIOR_QUEUE);
    assert_int_equal(app.module.priority, 40);
    assert_int_equal(zmk_behavior_queue_init(&app), EDGE_OK);
    assert_true(zmk_behavior_queue_is_empty(&app));
}

static void test_behavior_queue_immediate_and_delayed_execution(void **state) {
    (void)state;
    mock_sink_t sink_ctx = {0};
    zmk_behavior_queue_sink_if_t sink = {.self = &sink_ctx, .invoke_binding = mock_invoke_binding};

    zmk_behavior_queue_app_t app;
    zmk_behavior_queue_construct(&app, EDGE_MOD_ZMK_BEHAVIOR_QUEUE, 40, &sink);
    assert_int_equal(zmk_behavior_queue_init(&app), EDGE_OK);

    // Add item 1 with wait_ms = 50ms: gets executed immediately, sets queue to waiting state
    assert_int_equal(zmk_behavior_queue_add(&app, 1, 0x04, 0x00, true, 10, 50, 1000), EDGE_OK);
    assert_int_equal(sink_ctx.calls, 1);
    assert_int_equal(sink_ctx.last_behavior_id, 1);
    assert_int_equal(sink_ctx.last_param1, 0x04);
    assert_true(sink_ctx.last_press);

    // Add item 2 while waiting: queued
    assert_int_equal(zmk_behavior_queue_add(&app, 1, 0x04, 0x00, false, 10, 0, 1010), EDGE_OK);
    assert_int_equal(sink_ctx.calls, 1); // No new execution yet
    assert_int_equal(zmk_behavior_queue_count(&app), 1);

    // Process at 1030ms (before 1050ms deadline): does not execute item 2
    assert_int_equal(zmk_behavior_queue_process(&app, 1030), EDGE_OK);
    assert_int_equal(sink_ctx.calls, 1);

    // Process at 1050ms: deadline reached, item 2 executed!
    assert_int_equal(zmk_behavior_queue_process(&app, 1050), EDGE_OK);
    assert_int_equal(sink_ctx.calls, 2);
    assert_int_equal(sink_ctx.last_param1, 0x04);
    assert_false(sink_ctx.last_press);
    assert_true(zmk_behavior_queue_is_empty(&app));
}

static void test_behavior_queue_power_off_clears(void **state) {
    (void)state;
    mock_sink_t sink_ctx = {0};
    zmk_behavior_queue_sink_if_t sink = {.self = &sink_ctx, .invoke_binding = mock_invoke_binding};

    zmk_behavior_queue_app_t app;
    zmk_behavior_queue_construct(&app, EDGE_MOD_ZMK_BEHAVIOR_QUEUE, 40, &sink);
    assert_int_equal(zmk_behavior_queue_init(&app), EDGE_OK);

    // Add item with wait_ms
    zmk_behavior_queue_add(&app, 2, 0x05, 0x00, true, 11, 100, 1000);
    zmk_behavior_queue_add(&app, 2, 0x05, 0x00, false, 11, 0, 1010);
    assert_int_equal(zmk_behavior_queue_count(&app), 1);

    // Power off clears pending
    assert_int_equal(app.module.power_off(&app.module), EDGE_OK);
    assert_true(zmk_behavior_queue_is_empty(&app));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_behavior_queue_contract_and_init),
        cmocka_unit_test(test_behavior_queue_immediate_and_delayed_execution),
        cmocka_unit_test(test_behavior_queue_power_off_clears),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
