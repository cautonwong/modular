#include <pthread.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/event.h"
#include "edge/events.h"
#include "example/sys.h"

#define THREAD_EVENTS 500u

typedef struct thread_ctx {
    edge_event_queue_t queue;
    edge_event_sink_t sink;
    edge_event_t storage[512];
    edge_module_t app;
} thread_ctx_t;

static thread_ctx_t g_ctx;
static edge_sys_t g_sys;
static int g_handled;

static edge_status_t app_on_event(edge_module_t *module, const edge_event_t *event) {
    (void)module;
    (void)event;
    ++g_handled;
    return EDGE_OK;
}

static void *producer(void *arg) {
    thread_ctx_t *ctx = (thread_ctx_t *)arg;
    for (uint32_t i = 0u; i < THREAD_EVENTS; ++i) {
        const edge_event_t event = {.id = EDGE_EVT_UART0_RX, .arg0 = i};
        while (edge_event_sink_push_isr(&ctx->sink, &event) == EDGE_EOVERFLOW) {
            /* Single producer waiting for the runner to drain the bounded queue. */
        }
    }
    return NULL;
}

/* T7c: one thread repeatedly runs the runner step while another thread pushes
 * events, proving the runner decomposition and the SPSC queue are safe without
 * locks or a runtime allocator. */
static void test_thread_model(void **state) {
    (void)state;
    edge_module_t *apps[1];
    edge_sys_subscription_t subscriptions[2];
    pthread_t thread;

    g_handled = 0;
    g_ctx = (thread_ctx_t){0};
    g_ctx.app = (edge_module_t){.module_id = 1u,
                                .priority = 1u,
                                .period = 0u,
                                .on_event = app_on_event,
                                .private_data = &g_ctx};
    apps[0] = &g_ctx.app;

    assert_int_equal(edge_event_queue_init(&g_ctx.queue, g_ctx.storage, 512u), EDGE_OK);
    g_ctx.sink = (edge_event_sink_t){.queue = &g_ctx.queue, .clock = NULL, .guard = NULL};

    assert_int_equal(edge_sys_init(&g_sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_bind_event_queue(&g_sys, &g_ctx.queue, subscriptions, 2u), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&g_sys, EDGE_EVT_UART0_RX, &g_ctx.app), EDGE_OK);
    assert_int_equal(edge_sys_start(&g_sys), EDGE_OK);

    assert_int_equal(pthread_create(&thread, NULL, producer, &g_ctx), 0);
    while (g_handled < (int)THREAD_EVENTS) {
        assert_int_equal(edge_sys_run_once(&g_sys), EDGE_OK);
    }
    assert_int_equal(pthread_join(thread, NULL), 0);

    assert_int_equal(g_handled, (int)THREAD_EVENTS);
    assert_int_equal(edge_event_count(&g_ctx.queue), 0u);
    assert_int_equal(edge_event_dropped(&g_ctx.queue), 0u);

    assert_int_equal(edge_sys_power_off(&g_sys), EDGE_OK);
    assert_int_equal(edge_sys_deinit(&g_sys), EDGE_OK);
}

int main(void) {
    const struct CMUnitTest tests[] = {cmocka_unit_test(test_thread_model)};
    return cmocka_run_group_tests(tests, NULL, NULL);
}
