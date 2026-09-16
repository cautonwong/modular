#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "edge/sys.h"

static int logbuf[8];
static size_t n;
static int init_fn(edge_module_t *m) { logbuf[n++] = (int)m->module_id; return 0; }
static int poll_fn(edge_module_t *m) { logbuf[n++] = (int)m->module_id; return 0; }
static int off_fn(edge_module_t *m) { logbuf[n++] = (int)m->module_id; return 0; }

static void test_order(void **state) {
    (void)state;
    edge_module_t a = { .module_id=3, .priority=20, .init=init_fn, .poll=poll_fn, .power_off=off_fn };
    edge_module_t b = { .module_id=1, .priority=10, .init=init_fn, .poll=poll_fn, .power_off=off_fn };
    edge_module_t c = { .module_id=2, .priority=10, .init=init_fn, .poll=poll_fn, .power_off=off_fn };
    edge_module_t *apps[] = { &a, &c, &b };
    edge_sys_t sys;
    n = 0;
    assert_int_equal(edge_sys_init(&sys, apps, 3), EDGE_OK);
    assert_int_equal(logbuf[0], 1);
    assert_int_equal(logbuf[1], 2);
    assert_int_equal(logbuf[2], 3);
    n = 0;
    assert_int_equal(edge_sys_run_once(&sys), EDGE_OK);
    assert_int_equal(logbuf[0], 1);
    assert_int_equal(logbuf[1], 2);
    assert_int_equal(logbuf[2], 3);
    n = 0;
    assert_int_equal(edge_sys_power_off(&sys), EDGE_OK);
    assert_int_equal(logbuf[0], 3);
    assert_int_equal(logbuf[1], 2);
    assert_int_equal(logbuf[2], 1);
}

int main(void) {
    const struct CMUnitTest tests[] = { cmocka_unit_test(test_order) };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
