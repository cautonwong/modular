/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "vesc_can/vesc_can.h"

typedef struct mock_can_bus {
    uint32_t last_tx_id;
    uint8_t last_tx_data[8];
    uint8_t last_tx_len;
    uint32_t rx_id;
    uint8_t rx_data[8];
    uint8_t rx_len;
    bool has_rx;
} mock_can_bus_t;

static edge_status_t mock_send(void *self, uint32_t can_id, const uint8_t *data, uint8_t len) {
    mock_can_bus_t *bus = (mock_can_bus_t *)self;
    bus->last_tx_id = can_id;
    bus->last_tx_len = len;
    for (int i = 0; i < len; i++) {
        bus->last_tx_data[i] = data[i];
    }
    return EDGE_OK;
}

static edge_status_t mock_receive(void *self, uint32_t *can_id, uint8_t *data, uint8_t *len) {
    mock_can_bus_t *bus = (mock_can_bus_t *)self;
    if (!bus->has_rx) {
        return EDGE_ENOENT;
    }
    *can_id = bus->rx_id;
    *len = bus->rx_len;
    for (int i = 0; i < bus->rx_len; i++) {
        data[i] = bus->rx_data[i];
    }
    bus->has_rx = false;
    return EDGE_OK;
}

static void test_vesc_can_init_validation(void **state) {
    (void)state;
    vesc_can_app_t app;
    vesc_can_construct(&app, EDGE_MOD_VESC_CAN, 20u, NULL, NULL);
    assert_int_equal(vesc_can_init(NULL), EDGE_EINVAL);
    assert_int_equal(vesc_can_init(&app), EDGE_EINVAL);
}

static void test_vesc_can_status_broadcast(void **state) {
    (void)state;
    vesc_can_app_t app;
    mock_can_bus_t bus = {0};
    vesc_can_port_t port = {
        .self = &bus,
        .send_frame = mock_send,
        .receive_frame = mock_receive,
    };
    vesc_can_config_t cfg = {
        .controller_id = 10,
        .baudrate = 500000,
    };

    vesc_can_construct(&app, EDGE_MOD_VESC_CAN, 20u, &cfg, &port);
    assert_int_equal(vesc_can_init(&app), EDGE_OK);

    vesc_can_status_t telem = {
        .erpm = 12000.0f,
        .current_motor = 25.5f,
        .duty_cycle = 0.65f,
        .temp_fet = 45.2f,
        .temp_motor = 55.8f,
        .v_in = 48.2f,
    };
    vesc_can_set_telemetry(&app, &telem);

    assert_int_equal(vesc_can_send_status_1(&app), EDGE_OK);
    assert_int_equal(bus.last_tx_id, (9 << 8) | 10);
    assert_int_equal(bus.last_tx_len, 8);

    assert_int_equal(vesc_can_send_status_4(&app), EDGE_OK);
    assert_int_equal(bus.last_tx_id, (16 << 8) | 10);

    assert_int_equal(vesc_can_send_status_5(&app), EDGE_OK);
    assert_int_equal(bus.last_tx_id, (27 << 8) | 10);
}

static void test_vesc_can_rx_command(void **state) {
    (void)state;
    vesc_can_app_t app;
    mock_can_bus_t bus = {0};
    vesc_can_port_t port = {
        .self = &bus,
        .send_frame = mock_send,
        .receive_frame = mock_receive,
    };
    vesc_can_config_t cfg = {
        .controller_id = 5,
    };

    vesc_can_construct(&app, EDGE_MOD_VESC_CAN, 20u, &cfg, &port);
    assert_int_equal(vesc_can_init(&app), EDGE_OK);

    bus.rx_id = ((uint32_t)CAN_PACKET_SET_CURRENT << 8) | 5;
    bus.rx_len = 4;
    bus.rx_data[0] = 0;
    bus.rx_data[1] = 0;
    bus.rx_data[2] = (uint8_t)(15000 >> 8);
    bus.rx_data[3] = (uint8_t)(15000 & 0xFF);
    bus.has_rx = true;

    assert_int_equal(vesc_can_process_incoming(&app), EDGE_OK);
    assert_true(app.new_cmd_received);
    assert_float_equal(app.last_set_current, 15.0f, 0.01f);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_vesc_can_init_validation),
        cmocka_unit_test(test_vesc_can_status_broadcast),
        cmocka_unit_test(test_vesc_can_rx_command),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
