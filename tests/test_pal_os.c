#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "example/sys.h"
#include "pal_host/host.h"
#include "pal_os/idle.h"

static void test_os_port_records_yield_and_sleep(void **state) {
    (void)state;
    edge_os_port_t os = pal_host_os_port();
    const uint32_t y0 = pal_host_yields();
    const uint64_t s0 = pal_host_slept_ms();

    assert_non_null(os.yield);
    assert_non_null(os.sleep_ms);
    os.yield(os.self);
    os.yield(os.self);
    os.sleep_ms(os.self, 5u);
    assert_int_equal(pal_host_yields() - y0, 2u);
    assert_int_equal((int)(pal_host_slept_ms() - s0), 5);
}

static void test_idle_hook_bridges_os_port(void **state) {
    (void)state;
    edge_os_idle_t idle = {.os = NULL, .sleep_ms = 0u};
    const uint32_t y0 = pal_host_yields();
    const uint64_t s0 = pal_host_slept_ms();

    edge_os_idle_hook(NULL);  /* null context is safe */
    edge_os_idle_hook(&idle); /* null os port is safe */

    edge_os_port_t os = pal_host_os_port();
    idle.os = &os;
    idle.sleep_ms = 3u;
    edge_os_idle_hook(&idle);
    assert_int_equal(pal_host_yields() - y0, 1u);
    assert_int_equal((int)(pal_host_slept_ms() - s0), 3);
}

static void test_idle_hook_runs_via_sys_idle(void **state) {
    (void)state;
    edge_module_t app = {.module_id = 1u, .priority = 1u, .period = 0u};
    edge_module_t *apps[] = {&app};
    edge_sys_t sys;
    edge_os_port_t os = pal_host_os_port();
    edge_os_idle_t idle = {.os = &os, .sleep_ms = 7u};
    const uint32_t y0 = pal_host_yields();
    const uint64_t s0 = pal_host_slept_ms();

    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_set_idle(&sys, edge_os_idle_hook, &idle), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_run_once(&sys), EDGE_OK);

    /* No event, no due poll -> the step idles and yields to the OS. */
    assert_int_equal(pal_host_yields() - y0, 1u);
    assert_int_equal((int)(pal_host_slept_ms() - s0), 7);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_os_port_records_yield_and_sleep),
        cmocka_unit_test(test_idle_hook_bridges_os_port),
        cmocka_unit_test(test_idle_hook_runs_via_sys_idle),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
