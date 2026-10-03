/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/app_contract.h"
#include "edge/modules.h"
#include "zmk_split/split.h"
#include <string.h>

typedef struct mock_split_bus {
    uint8_t last_packet[16];
    size_t last_packet_len;
    int send_calls;
} mock_split_bus_t;

static edge_status_t mock_send(void *self, const uint8_t *data, size_t len) {
    mock_split_bus_t *bus = (mock_split_bus_t *)self;
    bus->send_calls++;
    if (len <= sizeof(bus->last_packet)) {
        memcpy(bus->last_packet, data, len);
        bus->last_packet_len = len;
    }
    return EDGE_OK;
}

typedef struct mock_receiver {
    int calls;
    uint32_t last_pos;
    bool last_pressed;
    uint32_t last_time;
} mock_receiver_t;

static edge_status_t mock_on_remote_position(void *self, uint32_t position, bool pressed,
                                             uint32_t timestamp_ms) {
    mock_receiver_t *rx = (mock_receiver_t *)self;
    rx->calls++;
    rx->last_pos = position;
    rx->last_pressed = pressed;
    rx->last_time = timestamp_ms;
    return EDGE_OK;
}

static void test_split_peripheral_to_central_forwarding(void **state) {
    (void)state;
    mock_split_bus_t bus = {0};
    zmk_split_transport_if_t transport = {.self = &bus, .send_packet = mock_send};

    // Peripheral instance (Right half, offset = 36)
    zmk_split_app_t peripheral;
    zmk_split_construct(&peripheral, EDGE_MOD_ZMK_SPLIT, 50, ZMK_SPLIT_ROLE_PERIPHERAL, 36,
                        &transport, NULL);
    assert_int_equal(zmk_split_init(&peripheral), EDGE_OK);

    // Central instance
    mock_receiver_t central_rx = {0};
    zmk_split_receiver_if_t rx_if = {.self = &central_rx,
                                     .on_remote_position_changed = mock_on_remote_position};
    zmk_split_app_t central;
    zmk_split_construct(&central, EDGE_MOD_ZMK_SPLIT, 50, ZMK_SPLIT_ROLE_CENTRAL, 0, NULL, &rx_if);
    assert_int_equal(zmk_split_init(&central), EDGE_OK);

    // Peripheral key 2 pressed at t=100
    assert_int_equal(zmk_split_forward_position(&peripheral, 2, true, 100), EDGE_OK);
    assert_int_equal(bus.send_calls, 1);
    assert_int_equal(peripheral.packets_sent, 1);

    // Central receives the packet over transport
    assert_int_equal(zmk_split_receive_packet(&central, bus.last_packet, bus.last_packet_len),
                     EDGE_OK);
    assert_int_equal(central.packets_received, 1);
    assert_int_equal(central_rx.calls, 1);
    // Global position must be local position (2) + offset (36) = 38!
    assert_int_equal(central_rx.last_pos, 38);
    assert_true(central_rx.last_pressed);
    assert_int_equal(central_rx.last_time, 100);
}

static void test_split_crc_error_rejection(void **state) {
    (void)state;
    mock_receiver_t central_rx = {0};
    zmk_split_receiver_if_t rx_if = {.self = &central_rx,
                                     .on_remote_position_changed = mock_on_remote_position};
    zmk_split_app_t central;
    zmk_split_construct(&central, EDGE_MOD_ZMK_SPLIT, 50, ZMK_SPLIT_ROLE_CENTRAL, 0, NULL, &rx_if);
    assert_int_equal(zmk_split_init(&central), EDGE_OK);

    uint8_t bad_packet[sizeof(zmk_split_packet_t)] = {0x01, 0x01, 0x05, 0x01, 0x00,
                                                      0x00, 0x00, 0x00, 0xFF /* corrupted CRC */};
    assert_int_equal(zmk_split_receive_packet(&central, bad_packet, sizeof(bad_packet)), EDGE_EIO);
    assert_int_equal(central_rx.calls, 0); // Not dispatched!
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_split_peripheral_to_central_forwarding),
        cmocka_unit_test(test_split_crc_error_rejection),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
