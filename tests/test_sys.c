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
} fake_app_t;

static int order[8];
static size_t order_count;

static edge_status_t fake_init(edge_module_t *m) { fake_app_t *a = m->private_data; ++a->init_count; order[order_count++] = (int)m->module_id; return a->init_rc; }
static edge_status_t fake_poll(edge_module_t *m) { fake_app_t *a = m->private_data; ++a->poll_count; return a->poll_rc; }
static edge_status_t fake_event(edge_module_t *m, const edge_event_t *e) { fake_app_t *a = m->private_data; ++a->event_count; assert_int_equal(e->id, EDGE_EVT_UART0_RX); return EDGE_OK; }
static edge_status_t fake_off(edge_module_t *m) { fake_app_t *a = m->private_data; ++a->off_count; return EDGE_OK; }
static edge_status_t fake_deinit(edge_module_t *m) { fake_app_t *a = m->private_data; ++a->deinit_count; return EDGE_OK; }

static void make_app(fake_app_t *a, uint32_t id, uint32_t priority)
{
    *a = (fake_app_t){0};
    a->module = (edge_module_t){.module_id=id, .priority=priority, .init=fake_init,
        .poll=fake_poll, .on_event=fake_event, .power_off=fake_off,
        .deinit=fake_deinit, .private_data=a};
}

static void test_priority_and_tie_break(void **state)
{
    (void)state;
    fake_app_t a, b, c;
    make_app(&a, 30u, 10u); make_app(&b, 20u, 10u); make_app(&c, 10u, 20u);
    edge_module_t *apps[] = {&a.module, &b.module, &c.module};
    edge_sys_t sys; order_count = 0u;
    assert_int_equal(edge_sys_init(&sys, apps, 3u), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(order[0], 20); assert_int_equal(order[1], 30); assert_int_equal(order[2], 10);
}

static void test_event_routing_and_shutdown(void **state)
{
    (void)state;
    fake_app_t a; make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module}; edge_event_t storage[4]; edge_event_queue_t queue;
    edge_sys_subscription_t subscriptions[2]; edge_sys_t sys; edge_event_t e = {.id=EDGE_EVT_UART0_RX};
    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_bind_event_queue(&sys, &queue, subscriptions, 2u), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &a.module), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_event_push_isr(&queue, &e), EDGE_OK);
    assert_int_equal(edge_sys_run_once(&sys), EDGE_OK);
    assert_int_equal(a.event_count, 1); assert_int_equal(a.poll_count, 1);
    assert_int_equal(edge_sys_power_off(&sys), EDGE_OK); assert_int_equal(edge_sys_deinit(&sys), EDGE_OK);
    assert_int_equal(a.off_count, 1); assert_int_equal(a.deinit_count, 1);
}

static void test_init_failure_rolls_back(void **state)
{
    (void)state;
    fake_app_t a, b; make_app(&a, 1u, 1u); make_app(&b, 2u, 2u); b.init_rc = EDGE_EIO;
    edge_module_t *apps[] = {&a.module, &b.module}; edge_sys_t sys; order_count = 0u;
    assert_int_equal(edge_sys_init(&sys, apps, 2u), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_EIO);
    assert_int_equal(a.deinit_count, 1); assert_int_equal(a.initialized, 0u); assert_int_equal(b.failed, 1u);
}

int main(void)
{
    const struct CMUnitTest tests[] = {cmocka_unit_test(test_priority_and_tie_break), cmocka_unit_test(test_event_routing_and_shutdown), cmocka_unit_test(test_init_failure_rolls_back)};
    return cmocka_run_group_tests(tests, NULL, NULL);
}
