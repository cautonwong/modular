/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "adc_input/adc_input.h"

typedef struct mock_adc {
    float v_throttle;
    float v_brake;
    bool btn_state;
    bool read_ok;
} mock_adc_t;

static edge_status_t mock_read_throttle(void *self, float *v) {
    mock_adc_t *adc = (mock_adc_t *)self;
    *v = adc->v_throttle;
    return adc->read_ok ? EDGE_OK : EDGE_EIO;
}

static edge_status_t mock_read_brake(void *self, float *v) {
    mock_adc_t *adc = (mock_adc_t *)self;
    *v = adc->v_brake;
    return adc->read_ok ? EDGE_OK : EDGE_EIO;
}

static bool mock_read_btn(void *self, uint8_t idx) {
    (void)idx;
    mock_adc_t *adc = (mock_adc_t *)self;
    return adc->btn_state;
}

static void test_adc_input_init_validation(void **state) {
    (void)state;
    adc_input_app_t app;
    adc_input_construct(&app, EDGE_MOD_ADC_INPUT, 20u, NULL, NULL);
    assert_int_equal(adc_input_init(NULL), EDGE_EINVAL);
    assert_int_equal(adc_input_init(&app), EDGE_EINVAL);
}

static void test_adc_input_throttle_and_brake(void **state) {
    (void)state;
    adc_input_app_t app;
    mock_adc_t adc = {
        .v_throttle = 0.8f,
        .v_brake = 0.5f,
        .read_ok = true,
    };
    adc_input_port_t port = {
        .self = &adc,
        .read_throttle_v = mock_read_throttle,
        .read_brake_v = mock_read_brake,
        .read_button = mock_read_btn,
    };
    adc_input_config_t cfg = {
        .mode = ADC_MODE_CURRENT,
        .voltage_min = 0.2f,
        .voltage_max = 3.2f,
        .voltage_start = 0.8f,
        .voltage_end = 2.8f,
        .use_brake_input = true,
        .brake_start = 0.8f,
        .brake_end = 2.8f,
        .safe_start = false,
    };

    adc_input_construct(&app, EDGE_MOD_ADC_INPUT, 20u, &cfg, &port);
    assert_int_equal(adc_input_init(&app), EDGE_OK);

    /* 0.8V -> 0% throttle */
    adc.v_throttle = 0.8f;
    assert_int_equal(adc_input_update(&app), EDGE_OK);
    assert_float_equal(adc_input_get_throttle(&app), 0.0f, 0.001f);
    assert_false(adc_input_has_fault(&app));

    /* 1.8V -> 50% throttle */
    adc.v_throttle = 1.8f;
    assert_int_equal(adc_input_update(&app), EDGE_OK);
    assert_float_equal(adc_input_get_throttle(&app), 0.5f, 0.001f);

    /* 2.8V -> 100% throttle */
    adc.v_throttle = 2.8f;
    assert_int_equal(adc_input_update(&app), EDGE_OK);
    assert_float_equal(adc_input_get_throttle(&app), 1.0f, 0.001f);

    /* Brake input 1.8V -> 50% brake */
    adc.v_brake = 1.8f;
    assert_int_equal(adc_input_update(&app), EDGE_OK);
    assert_float_equal(adc_input_get_brake(&app), 0.5f, 0.001f);

    /* Out of range (disconnected wire / 0.0V) -> fault detected */
    adc.v_throttle = 0.05f;
    assert_int_equal(adc_input_update(&app), EDGE_OK);
    assert_true(adc_input_has_fault(&app));
    assert_float_equal(adc_input_get_throttle(&app), 0.0f, 0.001f);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_adc_input_init_validation),
        cmocka_unit_test(test_adc_input_throttle_and_brake),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
