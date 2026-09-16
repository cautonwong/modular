#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>

#include "edge/event.h"
#include "edge/module.h"

static void test_public_contracts(void **state)
{
    (void)state;
    assert_true(sizeof(edge_event_t) == 24u);
    assert_true(sizeof(edge_status_t) >= sizeof(int));
    assert_true(EDGE_OK == 0);
    assert_true(EDGE_EINVAL < 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_public_contracts),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
