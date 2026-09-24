/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <math.h>

#include <cmocka.h>
/* clang-format on */

#include "edge/errors.h"
#include "imu/imu.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static void test_imu_ahrs_static_gravity(void **state) {
    (void)state;
    imu_ahrs_t ahrs;
    imu_ahrs_init(&ahrs, 2.0f, 0.005f);

    /* Flat on table: accel = (0, 0, 1g), gyro = (0, 0, 0) */
    for (int i = 0; i < 100; i++) {
        assert_int_equal(imu_ahrs_update(&ahrs, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.01f),
                         EDGE_OK);
    }

    float roll = 0.0f, pitch = 0.0f, yaw = 0.0f;
    imu_ahrs_get_euler(&ahrs, &roll, &pitch, &yaw);

    assert_true(fabsf(roll) < 1e-2f);
    assert_true(fabsf(pitch) < 1e-2f);
}

static void test_imu_ahrs_roll_convergence(void **state) {
    (void)state;
    imu_ahrs_t ahrs;
    imu_ahrs_init(&ahrs, 5.0f, 0.05f);

    /* Tilted 45 degrees around X axis: accel = (0, sin(45), cos(45)) = (0, 0.707, 0.707) */
    float ax = 0.0f;
    float ay = sinf((float)M_PI / 4.0f);
    float az = cosf((float)M_PI / 4.0f);

    for (int i = 0; i < 200; i++) {
        assert_int_equal(imu_ahrs_update(&ahrs, 0.0f, 0.0f, 0.0f, ax, ay, az, 0.01f), EDGE_OK);
    }

    float roll = 0.0f, pitch = 0.0f, yaw = 0.0f;
    imu_ahrs_get_euler(&ahrs, &roll, &pitch, &yaw);

    /* Should converge close to 45 deg (pi/4 rad = 0.785 rad) */
    assert_true(fabsf(roll - ((float)M_PI / 4.0f)) < 0.05f);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_imu_ahrs_static_gravity),
        cmocka_unit_test(test_imu_ahrs_roll_convergence),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
