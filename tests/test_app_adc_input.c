/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
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

    /* Below the range (0.05V): the reference keeps mapping it and says so in the range flag alone,
     * which is not a fault - it zeroes this demand because 0.05V is below voltage_start. */
    adc.v_throttle = 0.05f;
    assert_int_equal(adc_input_update(&app), EDGE_OK);
    assert_false(adc_input_range_ok(&app));
    assert_false(adc_input_has_fault(&app));
    assert_float_equal(adc_input_get_throttle(&app), 0.0f, 0.001f);
}

/*
 * The paths the init and throttle cases miss: the module's own hooks, update's argument guard, the
 * read-failure path that latches the wire-disconnected fault and zeroes both demands, the
 * safe-start gate (which holds the demands at zero until the throttle has been seen low once), and
 * the getters' guards.
 */
static void test_adc_input_edges_and_guards(void **state) {
    (void)state;
    mock_adc_t adc;
    memset(&adc, 0, sizeof(adc));
    adc.read_ok = true;
    adc.v_throttle = 0.8f;
    adc.v_brake = 0.5f;
    adc_input_port_t port = {.self = &adc,
                             .read_throttle_v = mock_read_throttle,
                             .read_brake_v = mock_read_brake,
                             .read_button = mock_read_btn};

    adc_input_app_t app;
    adc_input_construct(&app, EDGE_MOD_ADC_INPUT, 20u, NULL, &port);
    assert_int_equal(adc_input_init(&app), EDGE_OK);

    /* Guards. */
    assert_int_equal(adc_input_update(NULL), EDGE_EINVAL);
    assert_float_equal(adc_input_get_throttle(NULL), 0.0f, 1e-9f);
    assert_float_equal(adc_input_get_brake(NULL), 0.0f, 1e-9f);
    assert_true(adc_input_has_fault(NULL));
    assert_ptr_equal(adc_input_module(NULL), NULL);
    assert_ptr_equal(adc_input_module(&app), &app.module);

    /* The module's own hooks. */
    assert_int_equal(app.module.poll(&app.module), EDGE_OK);
    assert_int_equal(app.module.power_off(&app.module), EDGE_OK);

    /* The safe-start gate: the first update with the throttle above the low threshold holds the
     * demands at zero, and once the throttle has been seen low the gate opens. */
    assert_int_equal(adc_input_update(&app), EDGE_OK);
    adc.v_throttle = 0.0f;
    assert_int_equal(adc_input_update(&app), EDGE_OK);
    adc.v_throttle = 0.8f;
    assert_int_equal(adc_input_update(&app), EDGE_OK);

    /* A failed read latches the wire-disconnected fault and zeroes both demands; the getters
     * report zero rather than a stale value, and has_fault says so. */
    adc.read_ok = false;
    assert_int_equal(adc_input_update(&app), EDGE_EIO);
    assert_true(adc_input_has_fault(&app));
    assert_float_equal(adc_input_get_throttle(&app), 0.0f, 1e-9f);
    assert_float_equal(adc_input_get_brake(&app), 0.0f, 1e-9f);

    /* The voltage getters report the raw readings, and the normalised ones keep their guard. */
    assert_true(adc_input_get_throttle_v(&app) >= 0.0f);
    assert_true(adc_input_get_brake_v(&app) >= 0.0f);
}

static void test_adc_input_range_flag(void **state) {
    (void)state;
    adc_input_app_t app;
    mock_adc_t adc = {
        .v_throttle = 1.8f,
        .v_brake = 1.8f,
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

    /* applications/app_adc.c:207's comparison, and both of its ends are included. */
    assert_int_equal(adc_input_update(&app), EDGE_OK);
    assert_true(adc_input_range_ok(&app)); /* 1.8V, inside */

    adc.v_throttle = 0.2f; /* exactly voltage_min */
    (void)adc_input_update(&app);
    assert_true(adc_input_range_ok(&app));

    adc.v_throttle = 3.2f; /* exactly voltage_max */
    (void)adc_input_update(&app);
    assert_true(adc_input_range_ok(&app));

    adc.v_throttle = 0.19f; /* just below the lower end */
    (void)adc_input_update(&app);
    assert_false(adc_input_range_ok(&app));

    adc.v_throttle = 3.21f; /* just above the upper end */
    (void)adc_input_update(&app);
    assert_false(adc_input_range_ok(&app));

    assert_false(adc_input_range_ok(NULL));
}

static void test_adc_input_detach_override_and_filter(void **state) {
    (void)state;
    adc_input_app_t app;
    mock_adc_t adc = {
        .v_throttle = 1.8f,
        .v_brake = 1.8f,
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

    /* Mode 1 substitutes both channels, so the wire's own reading is not what is judged. */
    adc_input_override_throttle(&app, 2.8f);
    adc_input_override_brake(&app, 0.8f);
    adc_input_detach(&app, 1);
    assert_int_equal(adc_input_update(&app), EDGE_OK);
    assert_int_equal(adc_input_get_detach(&app), 1);
    assert_float_equal(adc_input_get_throttle_v(&app), 2.8f, 1e-6f);
    assert_float_equal(adc_input_get_throttle(&app), 1.0f, 0.001f);

    /* Mode 2 is the throttle alone, so the brake channel is read again. */
    adc_input_detach(&app, 2);
    assert_int_equal(adc_input_update(&app), EDGE_OK);
    assert_float_equal(adc_input_get_throttle_v(&app), 2.8f, 1e-6f);
    assert_float_equal(adc_input_get_brake_v(&app), 1.8f, 1e-6f);

    /* An override is truncated into [0, 3.3], the way utils_truncate_number truncates it, and 3.3V
     * is past this configuration's own upper end, which the range flag then reports. */
    adc_input_override_throttle(&app, 5.0f);
    adc_input_override_brake(&app, -1.0f);
    assert_int_equal(adc_input_update(&app), EDGE_OK);
    assert_float_equal(adc_input_get_throttle_v(&app), 3.3f, 1e-6f);
    assert_false(adc_input_range_ok(&app));

    /* applications/app_adc.c:198: the first update moves a third of the way from zero, since the
     * reference's approximation is 2/(N+1) with five samples. */
    adc_input_detach(&app, 0);
    app.config.use_filter = true;
    float before = app.throttle_filter;
    adc.v_throttle = 3.0f;
    assert_int_equal(adc_input_update(&app), EDGE_OK);
    /* The reference's approximation is 2/(N+1) with N five: a third of the way to 3.0V, and the
     * verdict of that update is the filter's own value rather than the reading. */
    assert_float_equal(app.throttle_filter, before - ((before - 3.0f) / 3.0f), 1e-4f);
    assert_float_equal(adc_input_get_throttle_v(&app), app.throttle_filter, 1e-6f);

    /* While the buttons are detached the serial pins are never the buttons, whatever was asked. */
    adc_input_set_rx_tx_as_buttons(&app, true);
    assert_true(adc_input_rx_tx_as_buttons(&app));
    adc_input_detach_buttons(&app, true);
    assert_true(adc_input_buttons_detached(&app));
    adc_input_set_rx_tx_as_buttons(&app, true);
    assert_false(adc_input_rx_tx_as_buttons(&app));

    /* The guards, and the two getters' answers with no application behind them. */
    adc_input_detach(NULL, 1);
    adc_input_override_throttle(NULL, 1.0f);
    adc_input_override_brake(NULL, 1.0f);
    adc_input_detach_buttons(NULL, true);
    adc_input_set_rx_tx_as_buttons(NULL, true);
    assert_int_equal(adc_input_get_detach(NULL), 0);
    assert_false(adc_input_buttons_detached(NULL));
    assert_false(adc_input_rx_tx_as_buttons(NULL));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_adc_input_init_validation),
        cmocka_unit_test(test_adc_input_throttle_and_brake),
        cmocka_unit_test(test_adc_input_edges_and_guards),
        cmocka_unit_test(test_adc_input_range_flag),
        cmocka_unit_test(test_adc_input_detach_override_and_filter),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
