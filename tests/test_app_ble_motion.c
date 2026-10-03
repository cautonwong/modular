#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "ble_motion/ble_motion.h"
#include "edge/events.h"
#include "edge/modules.h"

static uint32_t s_mock_notified_steps = 0;
static int16_t s_mock_notified_x = 0;
static int16_t s_mock_notified_y = 0;
static int16_t s_mock_notified_z = 0;

static edge_status_t mock_notify_step_count(void *self, uint32_t steps) {
    (void)self;
    s_mock_notified_steps = steps;
    return EDGE_OK;
}

static edge_status_t mock_notify_motion_values(void *self, int16_t x, int16_t y, int16_t z) {
    (void)self;
    s_mock_notified_x = x;
    s_mock_notified_y = y;
    s_mock_notified_z = z;
    return EDGE_OK;
}

static void test_ble_motion_encoding_and_notifications(void **state) {
    (void)state;

    ble_motion_notify_port_t port = {
        .self = NULL,
        .notify_step_count = mock_notify_step_count,
        .notify_motion_values = mock_notify_motion_values,
    };

    ble_motion_t motion;
    ble_motion_init(&motion, &port, NULL);

    /* Test buffer encoding */
    uint8_t step_buf[4];
    assert_int_equal(ble_motion_encode_steps(123456u, step_buf, sizeof(step_buf)), EDGE_OK);
    uint32_t decoded_steps = (uint32_t)step_buf[0] | ((uint32_t)step_buf[1] << 8u) |
                             ((uint32_t)step_buf[2] << 16u) | ((uint32_t)step_buf[3] << 24u);
    assert_int_equal(decoded_steps, 123456u);

    uint8_t val_buf[6];
    assert_int_equal(ble_motion_encode_values(-100, 250, -1000, val_buf, sizeof(val_buf)), EDGE_OK);
    int16_t dx = (int16_t)((uint16_t)val_buf[0] | ((uint16_t)val_buf[1] << 8u));
    int16_t dy = (int16_t)((uint16_t)val_buf[2] | ((uint16_t)val_buf[3] << 8u));
    int16_t dz = (int16_t)((uint16_t)val_buf[4] | ((uint16_t)val_buf[5] << 8u));
    assert_int_equal(dx, -100);
    assert_int_equal(dy, 250);
    assert_int_equal(dz, -1000);

    /* Notification disabled by default */
    s_mock_notified_steps = 0;
    ble_motion_on_step_count(&motion, 5000u);
    assert_int_equal(s_mock_notified_steps, 0);

    /* Enable step notification */
    ble_motion_set_step_notify_enabled(&motion, true);
    ble_motion_on_step_count(&motion, 5000u);
    assert_int_equal(s_mock_notified_steps, 5000u);

    /* Enable motion values notification */
    ble_motion_set_motion_notify_enabled(&motion, true);
    ble_motion_on_motion_values(&motion, 10, -20, 30);
    assert_int_equal(s_mock_notified_x, 10);
    assert_int_equal(s_mock_notified_y, -20);
    assert_int_equal(s_mock_notified_z, 30);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ble_motion_encoding_and_notifications),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
