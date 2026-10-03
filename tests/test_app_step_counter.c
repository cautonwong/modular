#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/events.h"
#include "edge/modules.h"
#include "step_counter/step_counter.h"

typedef struct mock_imu {
    int16_t x;
    int16_t y;
    int16_t z;
    uint32_t steps;
} mock_imu_t;

// cppcheck-suppress constParameterPointer ; signature fixed by consumer port
// cppcheck-suppress constParameterCallback ; signature fixed by consumer port
static edge_status_t mock_read_accel(void *self, int16_t *x, int16_t *y, int16_t *z) {
    const mock_imu_t *mock = (const mock_imu_t *)self;
    *x = mock->x;
    *y = mock->y;
    *z = mock->z;
    return EDGE_OK;
}

// cppcheck-suppress constParameterPointer ; signature fixed by consumer port
// cppcheck-suppress constParameterCallback ; signature fixed by consumer port
static edge_status_t mock_read_steps(void *self, uint32_t *steps) {
    const mock_imu_t *mock = (const mock_imu_t *)self;
    *steps = mock->steps;
    return EDGE_OK;
}

static void test_step_counter_fast_asin(void **state) {
    (void)state;
    assert_int_equal(step_counter_fast_asin(0), 0);
    assert_int_equal(step_counter_fast_asin(32767), 90);
    assert_int_equal(step_counter_fast_asin(-32767), -90);
    // sin(30 deg) = 0.5 * 32767 = 16383
    assert_in_range(step_counter_fast_asin(16383), 29, 31);
}

static void test_step_counting_and_trip(void **state) {
    (void)state;
    mock_imu_t mock = {.x = 0, .y = -800, .z = 400, .steps = 100};
    imu_sensor_if_t imu_if = {
        .read_accel = mock_read_accel,
        .read_steps = mock_read_steps,
        .reset_steps = NULL,
        .self = &mock,
    };

    step_counter_t sc;
    step_counter_construct(&sc, EDGE_MOD_STEP_COUNTER, 100u, &imu_if);
    assert_int_equal(step_counter_init(&sc), EDGE_OK);

    step_counter_update_motion(&sc, 0, -800, 400, 100, 10);
    assert_int_equal(step_counter_get_steps(&sc), 100);
    assert_int_equal(step_counter_get_trip_steps(&sc), 100);

    step_counter_update_motion(&sc, 0, -800, 400, 150, 20);
    assert_int_equal(step_counter_get_steps(&sc), 150);
    assert_int_equal(step_counter_get_trip_steps(&sc), 150);

    step_counter_reset_trip(&sc);
    assert_int_equal(step_counter_get_trip_steps(&sc), 0);
    assert_int_equal(step_counter_get_steps(&sc), 150);

    step_counter_update_motion(&sc, 0, -800, 400, 175, 30);
    assert_int_equal(step_counter_get_steps(&sc), 175);
    assert_int_equal(step_counter_get_trip_steps(&sc), 25);

    step_counter_advance_day(&sc);
    assert_int_equal(step_counter_get_steps(&sc), 0);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_step_counter_fast_asin),
        cmocka_unit_test(test_step_counting_and_trip),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
