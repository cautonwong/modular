/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "nunchuk/nunchuk.h"

#include <math.h>

typedef struct mock_chuk {
    uint32_t now_ms;
    float peer_rpm_lowest;
    float peer_current_highest;
    float peer_duty_highest;
    bool peers_ok;
} mock_chuk_t;

static edge_status_t mock_transfer(void *self, const uint8_t *tx, size_t tx_len, uint8_t *rx,
                                   size_t rx_len) {
    (void)self;
    (void)tx;
    (void)tx_len;
    (void)rx;
    (void)rx_len;
    return EDGE_OK;
}

static edge_status_t mock_delay(void *self, uint32_t ms) {
    (void)self;
    (void)ms;
    return EDGE_OK;
}

static edge_status_t mock_now_ms(void *self, uint32_t *ms) {
    mock_chuk_t *m = (mock_chuk_t *)self;
    *ms = m->now_ms;
    return EDGE_OK;
}

static edge_status_t mock_peer_aggregate(void *self, float *rpm_lowest, float *current_highest,
                                         float *duty_highest_abs) {
    mock_chuk_t *m = (mock_chuk_t *)self;
    if (!m->peers_ok) {
        return EDGE_EIO;
    }
    *rpm_lowest = m->peer_rpm_lowest;
    *current_highest = m->peer_current_highest;
    *duty_highest_abs = m->peer_duty_highest;
    return EDGE_OK;
}

static nunchuk_config_t default_config(void) {
    nunchuk_config_t cfg = {
        .ctrl_type = NUNCHUK_MODE_CURRENT,
        .hyst = 0.0f,
        .ramp_time_pos = 0.0f,
        .ramp_time_neg = 0.0f,
        .stick_erpm_per_s_in_cc = 1000.0f,
        .throttle_exp = 0.0f,
        .throttle_exp_brake = 0.0f,
        .throttle_exp_mode = NUNCHUK_CURVE_EXPO,
        .multi_esc = false,
        .tc = false,
        .tc_max_diff = 0.0f,
        .use_smart_rev = false,
        .smart_rev_max_duty = 0.3f,
        .smart_rev_ramp_time = 0.5f,
        .coast_brake_level = 0.0f,
        .coast_brake_ramp_time = 0.1f,
    };
    return cfg;
}

static void make_app(nunchuk_app_t *app, mock_chuk_t *m, const nunchuk_config_t *cfg) {
    nunchuk_port_t port = {
        .self = m,
        .transfer = mock_transfer,
        .delay_ms = mock_delay,
        .now_ms = mock_now_ms,
        .read_peer_aggregate = mock_peer_aggregate,
    };
    nunchuk_construct(app, EDGE_MOD_NUNCHUK, 20u, cfg, &port);
    assert_int_equal(nunchuk_init(app), EDGE_OK);
}

static nunchuk_policy_in_t default_in(void) {
    nunchuk_policy_in_t in = {
        .rpm_now = 0.0f,
        .lo_current_max = 50.0f,
        .lo_current_min = -30.0f,
        .l_current_max = 60.0f,
        .l_current_max_scale = 1.0f,
        .l_current_min = -40.0f,
        .l_current_min_scale = 1.0f,
        .l_min_duty = 0.05f,
        .cc_min_current = 1.0f,
        .s_pid_min_erpm = 100.0f,
        .duty_now = 0.0f,
        .current_now = 0.0f,
    };
    return in;
}

/* A frame with the two stick bytes, the three coarse axes and the packed bits in the last one. */
static void frame_of(uint8_t frame[6], uint8_t js_x, uint8_t js_y, bool bt_z, bool bt_c) {
    memset(frame, 0, 6u);
    frame[0] = js_x;
    frame[1] = js_y;
    frame[2] = 0x10u;
    frame[3] = 0x20u;
    frame[4] = 0x30u;
    /* Both buttons are active low, so a pressed one is a clear bit. */
    frame[5] = (uint8_t)((bt_z ? 0u : 1u) | (bt_c ? 0u : 2u));
}

static void test_nunchuk_decode_and_getters(void **state) {
    (void)state;
    nunchuk_app_t app;
    mock_chuk_t m;
    memset(&m, 0, sizeof(m));
    nunchuk_config_t cfg = default_config();
    make_app(&app, &m, &cfg);

    /* The centre is 128, and the two buttons are active low: an unpressed button leaves its bit
     * set, which the application reports as false. */
    uint8_t frame[6];
    frame_of(frame, 128u, 128u, false, false);
    nunchuk_data_t d;
    assert_true(nunchuk_decode_frame(&app, frame, &d));
    assert_false(d.bt_z);
    assert_false(d.bt_c);
    assert_float_equal(nunchuk_get_decoded_x(&app), 0.0f, 1e-6f);

    /* The accelerometer's low two bits are packed into the last byte two at a time. */
    frame[5] = 0xEAu; /* 1110 1010: z=2, y=2, c pressed, z pressed */
    assert_true(nunchuk_decode_frame(&app, frame, &d));
    assert_int_equal(d.acc_x, (0x10u << 2) | 2u);
    assert_int_equal(d.acc_y, (0x20u << 2) | 2u);
    assert_int_equal(d.acc_z, (0x30u << 2) | 3u);
    /* 0xEA has bit 0 clear, so Z reads as pressed, and bit 1 set, so C does not. */
    assert_true(d.bt_z);
    assert_false(d.bt_c);

    /* The decoded stick, and a frame that changes nothing. */
    frame_of(frame, 255u, 128u, false, false);
    assert_true(nunchuk_decode_frame(&app, frame, &d));
    nunchuk_update_data(&app, &d);
    assert_float_equal(nunchuk_get_decoded_x(&app), (255.0f - 128.0f) / 128.0f, 1e-6f);
    assert_false(nunchuk_decode_frame(&app, frame, &d)); /* the same frame again */

    assert_false(nunchuk_decode_frame(NULL, frame, &d));
    assert_float_equal(nunchuk_get_decoded_x(NULL), 0.0f, 1e-9f);
    assert_false(nunchuk_get_bt_c(NULL));
    assert_false(nunchuk_has_update(NULL));
    assert_null(nunchuk_module(NULL));
}

static void test_nunchuk_gates(void **state) {
    (void)state;
    nunchuk_app_t app;
    mock_chuk_t m;
    memset(&m, 0, sizeof(m));
    nunchuk_config_t cfg = default_config();
    make_app(&app, &m, &cfg);

    uint8_t frame[6];
    frame_of(frame, 200u, 128u, false, false);
    nunchuk_data_t d;
    assert_true(nunchuk_decode_frame(&app, frame, &d));
    nunchuk_update_data(&app, &d);

    nunchuk_policy_in_t in = default_in();
    /* The stick far from centre, and a live frame, so something is commanded. */
    assert_int_equal(nunchuk_policy(&app, &in, 0.005f).kind, NUNCHUK_CMD_CURRENT);

    /* A controller error, no mode, and a frame that has gone stale each command nothing. */
    nunchuk_set_error(&app, 2);
    assert_int_equal(nunchuk_policy(&app, &in, 0.005f).kind, NUNCHUK_CMD_NONE);
    assert_int_equal(nunchuk_get_error(&app), 2);
    nunchuk_set_error(&app, 0);

    app.config.ctrl_type = NUNCHUK_MODE_NONE;
    assert_int_equal(nunchuk_policy(&app, &in, 0.005f).kind, NUNCHUK_CMD_NONE);
    app.config.ctrl_type = NUNCHUK_MODE_CURRENT;

    m.now_ms =
        2000u + 1u; /* the application's own local timeout, at the iteration it is measured in */
    assert_int_equal(nunchuk_policy(&app, &in, 0.005f).kind, NUNCHUK_CMD_NONE);
    m.now_ms = 0u;

    assert_int_equal(nunchuk_policy(NULL, &in, 0.005f).kind, NUNCHUK_CMD_NONE);
    assert_int_equal(nunchuk_policy(&app, NULL, 0.005f).kind, NUNCHUK_CMD_NONE);
}

/* A stick held down and the C button: the reference ramps a speed target instead of commanding a
 * current, and only from a direction that is not already running the other way. */
static void test_nunchuk_button_c_ramps_speed(void **state) {
    (void)state;
    nunchuk_app_t app;
    mock_chuk_t m;
    memset(&m, 0, sizeof(m));
    nunchuk_config_t cfg = default_config();
    make_app(&app, &m, &cfg);

    nunchuk_data_t d;
    memset(&d, 0, sizeof(d));
    d.js_y = 128u;
    d.bt_c = true;
    nunchuk_update_data(&app, &d);

    nunchuk_policy_in_t in = default_in();
    /* Stopped, so the target starts where the motor is and the abort does not fire. */
    nunchuk_command_t cmd = nunchuk_policy(&app, &in, 0.005f);
    assert_int_equal(cmd.kind, NUNCHUK_CMD_PID_SPEED);
    assert_float_equal(cmd.pid_rpm, 0.0f, 1e-6f);

    /* Running the other way while the stick asks forward: nothing is commanded. */
    app.data.bt_c = true;
    app.was_pid = false;
    app.is_reverse = false;
    in.rpm_now = -500.0f;
    assert_int_equal(nunchuk_policy(&app, &in, 0.005f).kind, NUNCHUK_CMD_NONE);

    /* From rest, the stick's own value ramps the target at the configured rate. */
    app.was_pid = false;
    in.rpm_now = 0.0f;
    (void)nunchuk_policy(&app, &in, 0.005f);
    nunchuk_data_t up;
    memset(&up, 0, sizeof(up));
    up.js_y = 255u; /* the stick at its end, so the curve asks for one */
    up.bt_c = true;
    nunchuk_update_data(&app, &up);
    cmd = nunchuk_policy(&app, &in, 1.0f);
    assert_int_equal(cmd.kind, NUNCHUK_CMD_PID_SPEED);
    /* A stick byte of 255 is 0.9921875, not one, and the rate is the configuration's. */
    assert_float_equal(cmd.pid_rpm, ((255.0f - 128.0f) / 128.0f) * 1000.0f, 1e-3f);
}

/* The two buttons together are the reference's own stop. */
static void test_nunchuk_both_buttons_stop(void **state) {
    (void)state;
    nunchuk_app_t app;
    mock_chuk_t m;
    memset(&m, 0, sizeof(m));
    nunchuk_config_t cfg = default_config();
    make_app(&app, &m, &cfg);

    nunchuk_data_t d;
    memset(&d, 0, sizeof(d));
    d.js_y = 255u;
    d.bt_c = true;
    d.bt_z = true;
    nunchuk_update_data(&app, &d);
    nunchuk_policy_in_t in = default_in();
    assert_int_equal(nunchuk_policy(&app, &in, 0.005f).kind, NUNCHUK_CMD_NONE);
}

/* The reverse latch: only while the current is small, and the two modes that never reverse. */
static void test_nunchuk_reverse_latch(void **state) {
    (void)state;
    nunchuk_app_t app;
    mock_chuk_t m;
    memset(&m, 0, sizeof(m));
    nunchuk_config_t cfg = default_config();
    make_app(&app, &m, &cfg);

    nunchuk_data_t d;
    memset(&d, 0, sizeof(d));
    d.js_y = 128u; /* the centre, so no current is asked for */
    nunchuk_update_data(&app, &d);

    nunchuk_policy_in_t in = default_in();
    assert_false(nunchuk_get_is_rev(&app));
    /* The button going down flips the direction and the button coming up does not flip it back. */
    d.bt_z = true;
    nunchuk_update_data(&app, &d);
    (void)nunchuk_policy(&app, &in, 0.005f);
    assert_true(app.is_reverse);
    d.bt_z = false;
    nunchuk_update_data(&app, &d);
    (void)nunchuk_policy(&app, &in, 0.005f);
    assert_true(app.is_reverse);

    /* A frame that carries its own direction state overrides the button. */
    d.rev_has_state = true;
    d.is_rev = false;
    nunchuk_update_data(&app, &d);
    (void)nunchuk_policy(&app, &in, 0.005f);
    assert_false(app.is_reverse);

    /* The two modes that never reverse. */
    app.is_reverse = true;
    app.config.ctrl_type = NUNCHUK_MODE_CURRENT_NOREV;
    (void)nunchuk_policy(&app, &in, 0.005f);
    assert_false(app.is_reverse);
    app.is_reverse = true;
    app.config.ctrl_type = NUNCHUK_MODE_CURRENT_BIDIRECTIONAL;
    (void)nunchuk_policy(&app, &in, 0.005f);
    assert_false(app.is_reverse);
}

/* A negative current that is not a bidirectional request is a brake command. */
static void test_nunchuk_dispatch_and_coast_brake(void **state) {
    (void)state;
    nunchuk_app_t app;
    mock_chuk_t m;
    memset(&m, 0, sizeof(m));
    nunchuk_config_t cfg = default_config();
    make_app(&app, &m, &cfg);

    /* The stick pulled down gives a negative output, and on this mode that is a brake. */
    nunchuk_data_t d;
    memset(&d, 0, sizeof(d));
    d.js_y = 0u;
    nunchuk_update_data(&app, &d);

    nunchuk_policy_in_t in = default_in();
    nunchuk_command_t cmd = nunchuk_policy(&app, &in, 0.005f);
    assert_int_equal(cmd.kind, NUNCHUK_CMD_BRAKE);
    assert_true(cmd.current < 0.0f);

    /* Bidirectional sends the same value as a current, sign and all. */
    app.config.ctrl_type = NUNCHUK_MODE_CURRENT_BIDIRECTIONAL;
    cmd = nunchuk_policy(&app, &in, 0.005f);
    assert_int_equal(cmd.kind, NUNCHUK_CMD_CURRENT);

    /* The coast brake holds a brake current while the stick is at the centre. */
    app.config.ctrl_type = NUNCHUK_MODE_CURRENT;
    app.config.coast_brake_level = 0.5f;
    memset(&d, 0, sizeof(d));
    d.js_y = 128u;
    nunchuk_update_data(&app, &d);
    app.prev_current = 0.0f;
    app.coast_brake_prev = false;
    cmd = nunchuk_policy(&app, &in, 0.005f);
    assert_int_equal(cmd.kind, NUNCHUK_CMD_BRAKE);
    /* The coast brake has a ramp of its own, and this is its first step of the five milliseconds
     * the output loop runs at, against the whole current range the configuration allows. */
    assert_float_equal(cmd.current, -5.0f, 1e-6f);

    /* The bidirectional mode keeps the coast brake as a brake. */
    app.config.ctrl_type = NUNCHUK_MODE_CURRENT_BIDIRECTIONAL;
    cmd = nunchuk_policy(&app, &in, 0.005f);
    assert_int_equal(cmd.kind, NUNCHUK_CMD_BRAKE);
}

/*
 * The reverse-by-duty path: it takes over only on a hard pull with a slow bus, and its goal's sign
 * is the reverse flag's - which is where this differs from the PPM application's own version.
 */
static void test_nunchuk_smart_rev_duty(void **state) {
    (void)state;
    nunchuk_app_t app;
    mock_chuk_t m;
    memset(&m, 0, sizeof(m));
    nunchuk_config_t cfg = default_config();
    cfg.use_smart_rev = true;
    make_app(&app, &m, &cfg);

    nunchuk_data_t d;
    memset(&d, 0, sizeof(d));
    d.js_y = 0u; /* a full pull one way */
    nunchuk_update_data(&app, &d);

    nunchuk_policy_in_t in = default_in();
    in.l_min_duty = 0.5f; /* so the bus counts as slow */
    nunchuk_command_t cmd = nunchuk_policy(&app, &in, 0.005f);
    assert_int_equal(cmd.kind, NUNCHUK_CMD_DUTY);
    /* Not reversing, so the goal is the negative of the scaled value. */
    assert_true(cmd.duty <= 0.0f);
    assert_true(app.was_duty_control);
}

/* The traction control pulls a command back toward zero as a sibling wheel falls behind. */
static void test_nunchuk_traction_control(void **state) {
    (void)state;
    nunchuk_app_t app;
    mock_chuk_t m;
    memset(&m, 0, sizeof(m));
    m.peers_ok = true;
    m.peer_rpm_lowest = 100.0f;
    nunchuk_config_t cfg = default_config();
    cfg.multi_esc = true;
    cfg.tc = true;
    cfg.tc_max_diff = 1000.0f;
    make_app(&app, &m, &cfg);

    nunchuk_data_t d;
    memset(&d, 0, sizeof(d));
    d.js_y = 255u;
    nunchuk_update_data(&app, &d);

    nunchuk_policy_in_t in = default_in();
    in.rpm_now = 600.0f; /* this wheel, well above the slowest */
    in.duty_now =
        0.5f; /* turning the same way as the request, so it accelerates rather than brakes */
    nunchuk_command_t cmd = nunchuk_policy(&app, &in, 0.005f);
    assert_int_equal(cmd.kind, NUNCHUK_CMD_CURRENT);
    /* Half of the allowed spread has passed, so the command is half of what the stick asked for -
     * which is 255's own 0.9921875 of the current limit. */
    assert_float_equal(cmd.current, ((255.0f - 128.0f) / 128.0f) * in.lo_current_max * 0.5f, 1e-3f);

    /* At the slowest wheel's own speed there is nothing to pull back, so the command stands. */
    in.rpm_now = 100.0f;
    cmd = nunchuk_policy(&app, &in, 0.005f);
    assert_float_equal(cmd.current, ((255.0f - 128.0f) / 128.0f) * in.lo_current_max, 1e-3f);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_nunchuk_decode_and_getters),
        cmocka_unit_test(test_nunchuk_gates),
        cmocka_unit_test(test_nunchuk_button_c_ramps_speed),
        cmocka_unit_test(test_nunchuk_both_buttons_stop),
        cmocka_unit_test(test_nunchuk_reverse_latch),
        cmocka_unit_test(test_nunchuk_dispatch_and_coast_brake),
        cmocka_unit_test(test_nunchuk_smart_rev_duty),
        cmocka_unit_test(test_nunchuk_traction_control),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
