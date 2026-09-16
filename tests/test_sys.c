#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/event.h"
#include "edge/events.h"
#include "example/sys.h"

typedef struct fake_app {
    edge_module_t module;
    int init_count;
    int poll_count;
    int event_count;
    int off_count;
    int deinit_count;
    edge_status_t init_rc;
    edge_status_t poll_rc;
    edge_status_t event_rc;
    edge_status_t off_rc;
} fake_app_t;

static int g_order[8];
static size_t g_order_count;

static fake_app_t *as_fake(edge_module_t *m) {
    return (fake_app_t *)m->private_data;
}

static edge_status_t fake_init(edge_module_t *m) {
    fake_app_t *a = as_fake(m);
    ++a->init_count;
    if (g_order_count < 8u) {
        g_order[g_order_count++] = (int)m->module_id;
    }
    return a->init_rc;
}

static edge_status_t fake_poll(edge_module_t *m) {
    fake_app_t *a = as_fake(m);
    ++a->poll_count;
    return a->poll_rc;
}

static edge_status_t fake_event(edge_module_t *m, const edge_event_t *e) {
    (void)e;
    fake_app_t *a = as_fake(m);
    ++a->event_count;
    return a->event_rc;
}

static edge_status_t fake_off(edge_module_t *m) {
    fake_app_t *a = as_fake(m);
    ++a->off_count;
    return a->off_rc;
}

static edge_status_t fake_deinit(edge_module_t *m) {
    fake_app_t *a = as_fake(m);
    ++a->deinit_count;
    return EDGE_OK;
}

static void make_app(fake_app_t *a, uint32_t id, uint32_t priority) {
    *a = (fake_app_t){0};
    a->module = (edge_module_t){.module_id = id,
                                .priority = priority,
                                .init = fake_init,
                                .poll = fake_poll,
                                .on_event = fake_event,
                                .power_off = fake_off,
                                .deinit = fake_deinit,
                                .private_data = a};
}

static void test_init_validates_arguments(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_sys_t sys;

    assert_int_equal(edge_sys_init(NULL, apps, 1u), EDGE_EINVAL);
    assert_int_equal(edge_sys_init(&sys, NULL, 1u), EDGE_EINVAL);
    assert_int_equal(edge_sys_init(&sys, apps, 0u), EDGE_OK);
}

static void test_duplicate_and_zero_module_id_rejected(void **state) {
    (void)state;
    fake_app_t a;
    fake_app_t b;
    make_app(&a, 7u, 1u);
    make_app(&b, 7u, 2u);
    edge_module_t *dup[] = {&a.module, &b.module};
    edge_sys_t sys;
    assert_int_equal(edge_sys_init(&sys, dup, 2u), EDGE_EBUSY);

    fake_app_t z;
    make_app(&z, 0u, 1u);
    edge_module_t *zero[] = {&z.module};
    assert_int_equal(edge_sys_init(&sys, zero, 1u), EDGE_EINVAL);
}

static void test_priority_and_tie_break(void **state) {
    (void)state;
    fake_app_t a;
    fake_app_t b;
    fake_app_t c;
    make_app(&a, 30u, 10u);
    make_app(&b, 20u, 10u);
    make_app(&c, 10u, 20u);
    edge_module_t *apps[] = {&a.module, &b.module, &c.module};
    edge_sys_t sys;
    g_order_count = 0u;

    assert_int_equal(edge_sys_init(&sys, apps, 3u), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(g_order[0], 20);
    assert_int_equal(g_order[1], 30);
    assert_int_equal(g_order[2], 10);
}

static void test_required_validation(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_sys_t sys;

    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    const uint32_t missing[] = {42u};
    assert_int_equal(edge_sys_set_required(&sys, missing, 1u), EDGE_OK);
    assert_int_equal(edge_sys_validate_required(&sys), EDGE_EDEPEND);
    assert_int_equal(edge_sys_start(&sys), EDGE_EDEPEND);
    assert_int_equal(a.init_count, 0);

    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    const uint32_t present[] = {1u};
    assert_int_equal(edge_sys_set_required(&sys, present, 1u), EDGE_OK);
    assert_int_equal(edge_sys_validate_required(&sys), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
}

static void test_required_rejects_state_after_start(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_sys_t sys;
    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    const uint32_t ids[] = {1u};
    assert_int_equal(edge_sys_set_required(&sys, ids, 1u), EDGE_ESTATE);
}

static void test_subscription_rules(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_sys_subscription_t subs[2];
    edge_sys_t sys;

    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    /* Not bound yet: no subscription storage. */
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &a.module), EDGE_EINVAL);
    assert_int_equal(edge_sys_bind_event_queue(&sys, &queue, subs, 2u), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&sys, 0u, &a.module), EDGE_EINVAL);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, NULL), EDGE_EINVAL);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &a.module), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &a.module), EDGE_EBUSY);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_TX_DONE, &a.module), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_DLT645_RX, &a.module), EDGE_ENOSPC);
}

static void test_bind_requires_constructed_state(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_sys_subscription_t subs[2];
    edge_sys_t sys;

    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    assert_int_equal(edge_sys_bind_event_queue(&sys, NULL, subs, 2u), EDGE_EINVAL);
    assert_int_equal(edge_sys_bind_event_queue(&sys, &queue, subs, 2u), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_bind_event_queue(&sys, &queue, subs, 2u), EDGE_ESTATE);
}

static void test_state_guards(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_sys_t sys;

    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_run_once(&sys), EDGE_ESTATE);
    assert_int_equal(edge_sys_power_off(&sys), EDGE_ESTATE);
    assert_int_equal(edge_sys_dispatch_events(&sys), EDGE_EINVAL);

    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_ESTATE);
    assert_int_equal(edge_sys_deinit(&sys), EDGE_ESTATE);
    assert_int_equal(edge_sys_run_once(&sys), EDGE_OK);
}

static void test_event_routing_and_reverse_shutdown(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_sys_subscription_t subs[2];
    edge_sys_t sys;
    edge_event_t event = {.id = EDGE_EVT_UART0_RX};

    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_bind_event_queue(&sys, &queue, subs, 2u), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &a.module), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_event_push_isr(&queue, &event), EDGE_OK);
    assert_int_equal(edge_sys_run_once(&sys), EDGE_OK);
    assert_int_equal(a.event_count, 1);
    assert_int_equal(a.poll_count, 1);
    assert_int_equal(edge_sys_power_off(&sys), EDGE_OK);
    assert_int_equal(edge_sys_deinit(&sys), EDGE_OK);
    assert_int_equal(a.off_count, 1);
    assert_int_equal(a.deinit_count, 1);
}

static void test_init_failure_rolls_back(void **state) {
    (void)state;
    fake_app_t a;
    fake_app_t b;
    make_app(&a, 1u, 1u);
    make_app(&b, 2u, 2u);
    b.init_rc = EDGE_EIO;
    edge_module_t *apps[] = {&a.module, &b.module};
    edge_sys_t sys;
    g_order_count = 0u;

    assert_int_equal(edge_sys_init(&sys, apps, 2u), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_EIO);
    assert_int_equal(a.deinit_count, 1);
    assert_int_equal(a.module.initialized, 0u);
    assert_int_equal(b.module.failed, 1u);
}

static void test_event_failure_isolates_app(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    a.event_rc = EDGE_EIO;
    edge_module_t *apps[] = {&a.module};
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_sys_subscription_t subs[2];
    edge_sys_t sys;
    edge_event_t event = {.id = EDGE_EVT_UART0_RX};

    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_bind_event_queue(&sys, &queue, subs, 2u), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &a.module), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_event_push_isr(&queue, &event), EDGE_OK);

    assert_int_equal(edge_sys_run_once(&sys), EDGE_EIO);
    assert_int_equal(a.module.failed, 1u);
    assert_int_equal(a.event_count, 1);
    assert_int_equal(a.poll_count, 0);

    /* A failed module is isolated from subsequent scheduling. */
    assert_int_equal(edge_sys_run_once(&sys), EDGE_OK);
    assert_int_equal(a.poll_count, 0);
}

static void test_poll_failure_isolates_app(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    a.poll_rc = EDGE_EIO;
    edge_module_t *apps[] = {&a.module};
    edge_sys_t sys;

    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_run_once(&sys), EDGE_EIO);
    assert_int_equal(a.module.failed, 1u);
    assert_int_equal(a.poll_count, 1);
}

static void test_power_off_failure_marks_failed(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    a.off_rc = EDGE_EIO;
    edge_module_t *apps[] = {&a.module};
    edge_sys_t sys;

    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_power_off(&sys), EDGE_EIO);
    assert_int_equal(a.module.failed, 1u);
}

static void test_sys_example_wrapper(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_sys_subscription_t subs[2];
    edge_sys_t sys;

    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    assert_int_equal(sys_example_init(&sys, apps, 1u, &queue, subs, 2u), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &a.module), EDGE_OK);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_init_validates_arguments),
        cmocka_unit_test(test_duplicate_and_zero_module_id_rejected),
        cmocka_unit_test(test_priority_and_tie_break),
        cmocka_unit_test(test_required_validation),
        cmocka_unit_test(test_required_rejects_state_after_start),
        cmocka_unit_test(test_subscription_rules),
        cmocka_unit_test(test_bind_requires_constructed_state),
        cmocka_unit_test(test_state_guards),
        cmocka_unit_test(test_event_routing_and_reverse_shutdown),
        cmocka_unit_test(test_init_failure_rolls_back),
        cmocka_unit_test(test_event_failure_isolates_app),
        cmocka_unit_test(test_poll_failure_isolates_app),
        cmocka_unit_test(test_power_off_failure_marks_failed),
        cmocka_unit_test(test_sys_example_wrapper),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
