#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "pal_host/host.h"
#include "pal_os/power.h"

static bool g_fake_pending = false;
static edge_pm_mode_t g_last_board_mode = EDGE_PM_ACTIVE;
static uint64_t g_last_board_ticks = 0u;
static uint32_t g_board_entry_count = 0u;

static bool check_pending(void *ctx) {
    (void)ctx;
    return g_fake_pending;
}

static edge_status_t fake_board_pm(edge_pm_mode_t mode, uint64_t idle_ticks, void *ctx) {
    (void)ctx;
    g_last_board_mode = mode;
    g_last_board_ticks = idle_ticks;
    ++g_board_entry_count;
    return EDGE_OK;
}

static void test_pm_init_and_locks(void **state) {
    (void)state;
    edge_pm_state_t pm;

    /* NULL check */
    assert_int_equal(edge_pm_init(NULL, 100u, 10000u), EDGE_EINVAL);

    assert_int_equal(edge_pm_init(&pm, 100u, 10000u), EDGE_OK);

    /* Normal mode transitions with no locks */
    assert_int_equal(edge_pm_target_mode(&pm, 0u), EDGE_PM_ACTIVE);
    assert_int_equal(edge_pm_target_mode(&pm, 50u), EDGE_PM_IDLE);
    assert_int_equal(edge_pm_target_mode(&pm, 100u), EDGE_PM_STOP);
    assert_int_equal(edge_pm_target_mode(&pm, 500u), EDGE_PM_STOP);
    assert_int_equal(edge_pm_target_mode(&pm, 10000u), EDGE_PM_STANDBY);

    /* Lock IDLE (e.g. UART reception or Flash write in progress) */
    assert_int_equal(edge_pm_lock(&pm, EDGE_PM_IDLE), EDGE_OK);
    /* Now even with 10000 ticks idle, target mode is constrained to IDLE */
    assert_int_equal(edge_pm_target_mode(&pm, 10000u), EDGE_PM_IDLE);

    /* Unlock IDLE */
    assert_int_equal(edge_pm_unlock(&pm, EDGE_PM_IDLE), EDGE_OK);
    /* Unlocking when 0 -> ESTATE */
    assert_int_equal(edge_pm_unlock(&pm, EDGE_PM_IDLE), EDGE_ESTATE);
    assert_int_equal(edge_pm_target_mode(&pm, 10000u), EDGE_PM_STANDBY);

    /* Lock STOP -> allows STOP but blocks STANDBY */
    assert_int_equal(edge_pm_lock(&pm, EDGE_PM_STOP), EDGE_OK);
    assert_int_equal(edge_pm_target_mode(&pm, 10000u), EDGE_PM_STOP);
    assert_int_equal(edge_pm_unlock(&pm, EDGE_PM_STOP), EDGE_OK);

    /* Lock ACTIVE */
    assert_int_equal(edge_pm_lock(&pm, EDGE_PM_ACTIVE), EDGE_OK);
    assert_int_equal(edge_pm_target_mode(&pm, 500u), EDGE_PM_ACTIVE);
    assert_int_equal(edge_pm_unlock(&pm, EDGE_PM_ACTIVE), EDGE_OK);
}

static void test_pm_execute(void **state) {
    (void)state;
    edge_pm_state_t pm;
    const edge_pal_port_t pal = pal_host_port();

    assert_int_equal(edge_pm_init(&pm, 100u, 10000u), EDGE_OK);
    g_board_entry_count = 0u;
    g_fake_pending = false;

    /* Execute STOP mode */
    assert_int_equal(edge_pm_execute(&pm, 500u, &pal, fake_board_pm, NULL, check_pending, NULL),
                     EDGE_OK);
    assert_int_equal(g_board_entry_count, 1u);
    assert_int_equal(g_last_board_mode, EDGE_PM_STOP);
    assert_int_equal(g_last_board_ticks, 500u);

    /* If pending is true, execution is skipped */
    g_fake_pending = true;
    assert_int_equal(edge_pm_execute(&pm, 500u, &pal, fake_board_pm, NULL, check_pending, NULL),
                     EDGE_OK);
    assert_int_equal(g_board_entry_count, 1u); /* count unchanged */

    /* If idle_ticks == 0, returns immediately without entering low power */
    g_fake_pending = false;
    assert_int_equal(edge_pm_execute(&pm, 0u, &pal, fake_board_pm, NULL, check_pending, NULL),
                     EDGE_OK);
    assert_int_equal(g_board_entry_count, 1u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pm_init_and_locks),
        cmocka_unit_test(test_pm_execute),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
