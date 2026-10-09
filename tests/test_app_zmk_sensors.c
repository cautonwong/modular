/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/app_contract.h"
#include "edge/modules.h"
#include "zmk_sensors/sensors.h"

#define ZMK_BHV_KEY_PRESS 2

typedef struct mock_behavior_sink {
    int calls;
    uint16_t last_behavior_id;
    uint32_t last_param1;
    bool last_pressed;
    uint32_t last_time;
} mock_behavior_sink_t;

static edge_status_t mock_invoke_binding(void *self, uint16_t behavior_id, uint32_t param1,
                                         uint32_t param2, bool pressed, uint32_t timestamp_ms) {
    (void)param2;
    mock_behavior_sink_t *sink = (mock_behavior_sink_t *)self;
    sink->calls++;
    sink->last_behavior_id = behavior_id;
    sink->last_param1 = param1;
    sink->last_pressed = pressed;
    sink->last_time = timestamp_ms;
    return EDGE_OK;
}

static void test_sensors_encoder_quadrature_and_steps(void **state) {
    (void)state;
    mock_behavior_sink_t bhv_sink = {0};
    zmk_sensors_behavior_if_t bhv_if = {.self = &bhv_sink, .invoke_binding = mock_invoke_binding};

    zmk_sensors_app_t app;
    zmk_sensors_construct(&app, EDGE_MOD_ZMK_SENSORS, 50, &bhv_if);
    assert_int_equal(zmk_sensors_init(&app), EDGE_OK);

    // Add encoder 0: CW = Volume Up (0x80), CCW = Volume Down (0x81), 4 pulses per detent
    zmk_sensor_binding_t binding = {
        .behavior_id = ZMK_BHV_KEY_PRESS,
        .cw_param1 = 0x80,
        .ccw_param1 = 0x81,
    };
    uint8_t enc_id = 0;
    assert_int_equal(zmk_sensors_add_encoder(&app, 4, binding, &enc_id), EDGE_OK);

    // Direct step +1 (CW)
    assert_int_equal(zmk_sensors_process_step(&app, enc_id, 1, 100), EDGE_OK);
    // Fires tap (press + release) -> 2 calls
    assert_int_equal(bhv_sink.calls, 2);
    assert_int_equal(bhv_sink.last_param1, 0x80); // CW Volume Up

    // Direct step -1 (CCW)
    assert_int_equal(zmk_sensors_process_step(&app, enc_id, -1, 150), EDGE_OK);
    assert_int_equal(bhv_sink.calls, 4);
    assert_int_equal(bhv_sink.last_param1, 0x81); // CCW Volume Down

    // Quadrature sequence for 1 full detent CW: (0,0) -> (0,1) -> (1,1) -> (1,0) -> (0,0)
    zmk_sensors_process_encoder_pulse(&app, enc_id, 0, 1, 200);
    zmk_sensors_process_encoder_pulse(&app, enc_id, 1, 1, 210);
    zmk_sensors_process_encoder_pulse(&app, enc_id, 1, 0, 220);
    zmk_sensors_process_encoder_pulse(&app, enc_id, 0, 0, 230);

    // 4 pulses accumulated -> 1 CW step fired!
    assert_int_equal(bhv_sink.calls, 6);
    assert_int_equal(bhv_sink.last_param1, 0x80);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_sensors_encoder_quadrature_and_steps),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
