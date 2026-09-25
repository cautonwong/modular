/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "vesc_can/vesc_can.h"
#include "vesc_comm/vesc_comm.h" /* vesc_crc16, for the cross-check */

/* The multi-frame path sends a sequence, so the mock keeps one. */
#define MOCK_CAN_MAX_FRAMES 48

typedef struct mock_can_frame {
    uint32_t id;
    uint8_t data[8];
    uint8_t len;
} mock_can_frame_t;

typedef struct mock_can_bus {
    mock_can_frame_t tx[MOCK_CAN_MAX_FRAMES];
    int tx_count;
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
    if (bus->tx_count < MOCK_CAN_MAX_FRAMES) {
        bus->tx[bus->tx_count].id = can_id;
        bus->tx[bus->tx_count].len = len;
        for (int i = 0; i < len && i < 8; i++) {
            bus->tx[bus->tx_count].data[i] = data[i];
        }
        bus->tx_count++;
    }
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

/*
 * vesc_can_send_buffer is the reference's comm_can_send_buffer, all three branches. The
 * frames are checked as a sequence (ids, index bytes, the closing frame's length and CRC),
 * and the CRC is cross-checked against vesc_comm's table-driven implementation so the
 * bitwise copy here cannot drift from it.
 */
static void test_vesc_can_send_buffer_framing(void **state) {
    (void)state;
    mock_can_bus_t bus;
    memset(&bus, 0, sizeof(bus));
    vesc_can_port_t port = {.send_frame = mock_send, .receive_frame = mock_receive, .self = &bus};
    vesc_can_config_t cfg = {.controller_id = 7u};
    vesc_can_app_t app;
    vesc_can_construct(&app, EDGE_MOD_VESC_CAN, 20u, &cfg, &port);
    assert_int_equal(vesc_can_init(&app), EDGE_OK);

    /* Six bytes or less: one short-buffer frame carrying the sender and the send flag. */
    uint8_t short_payload[4] = {1u, 2u, 3u, 4u};
    bus.tx_count = 0;
    assert_int_equal(vesc_can_send_buffer(&app, 3u, short_payload, sizeof(short_payload), 1u),
                     EDGE_OK);
    assert_int_equal(bus.tx_count, 1);
    assert_int_equal(bus.tx[0].id,
                     (5u == 5u) ? ((uint32_t)CAN_PACKET_PROCESS_SHORT_BUFFER << 8) | 3u : 0u);
    assert_int_equal(bus.tx[0].len, 6u);
    assert_int_equal(bus.tx[0].data[0], 7u); /* our own controller id */
    assert_int_equal(bus.tx[0].data[1], 1u); /* the send flag, passed through */
    assert_int_equal(bus.tx[0].data[2], 1u);
    assert_int_equal(bus.tx[0].data[5], 4u);

    /* Longer: FILL_RX_BUFFER frames of a one-byte index plus seven bytes, then the closing
     * PROCESS_RX_BUFFER frame with the length and the CRC. */
    uint8_t long_payload[21];
    for (size_t i = 0u; i < sizeof(long_payload); i++) {
        long_payload[i] = (uint8_t)(i + 1u);
    }
    bus.tx_count = 0;
    assert_int_equal(vesc_can_send_buffer(&app, 9u, long_payload, sizeof(long_payload), 0u),
                     EDGE_OK);
    assert_int_equal(bus.tx_count, 4); /* indices 0, 7, 14, then the closing frame */
    for (int f = 0; f < 3; f++) {
        assert_int_equal(bus.tx[f].id, ((uint32_t)CAN_PACKET_FILL_RX_BUFFER << 8) | 9u);
        assert_int_equal(bus.tx[f].data[0], (uint8_t)(f * 7));
        assert_int_equal(bus.tx[f].len, 8u);
    }
    assert_int_equal(bus.tx[2].data[1], 15u); /* index 14 -> payload byte 15 */
    assert_int_equal(bus.tx[3].id, ((uint32_t)CAN_PACKET_PROCESS_RX_BUFFER << 8) | 9u);
    assert_int_equal(bus.tx[3].len, 6u);
    assert_int_equal(bus.tx[3].data[0], 7u);
    assert_int_equal(bus.tx[3].data[1], 0u);
    assert_int_equal(bus.tx[3].data[2], 0u);
    assert_int_equal(bus.tx[3].data[3], (uint8_t)sizeof(long_payload));
    uint16_t crc = vesc_crc16(long_payload, sizeof(long_payload));
    assert_int_equal(bus.tx[3].data[4], (uint8_t)(crc >> 8));
    assert_int_equal(bus.tx[3].data[5], (uint8_t)(crc & 0xFFu));

    /* Past index 255 the remainder goes in FILL_RX_BUFFER_LONG frames with a two-byte
     * index, which is the branch that is easy to get wrong. */
    uint8_t very_long[270];
    for (size_t i = 0u; i < sizeof(very_long); i++) {
        very_long[i] = (uint8_t)i;
    }
    bus.tx_count = 0;
    assert_int_equal(vesc_can_send_buffer(&app, 5u, very_long, sizeof(very_long), 0u), EDGE_OK);
    /* indices 0..252 step 7 = 37 frames, then 259 and 265 as LONG frames (two-byte index
     * plus six payload bytes), then the closing frame: 40 in all. */
    assert_int_equal(bus.tx_count, 40);
    assert_int_equal(bus.tx[36].id, ((uint32_t)CAN_PACKET_FILL_RX_BUFFER << 8) | 5u);
    assert_int_equal(bus.tx[36].data[0], 252u);
    assert_int_equal(bus.tx[37].id, ((uint32_t)CAN_PACKET_FILL_RX_BUFFER_LONG << 8) | 5u);
    assert_int_equal(bus.tx[37].data[0], (uint8_t)(259u >> 8));
    assert_int_equal(bus.tx[37].data[1], (uint8_t)(259u & 0xFFu));
    assert_int_equal(bus.tx[37].len, 8u);
    assert_int_equal(bus.tx[38].data[0], (uint8_t)(265u >> 8));
    assert_int_equal(bus.tx[38].data[1], (uint8_t)(265u & 0xFFu));
    assert_int_equal(bus.tx[38].len, 7u); /* two index bytes plus the five left over */
    assert_int_equal(bus.tx[39].id, ((uint32_t)CAN_PACKET_PROCESS_RX_BUFFER << 8) | 5u);
    assert_int_equal(bus.tx[39].data[2], (uint8_t)(sizeof(very_long) >> 8));
    assert_int_equal(bus.tx[39].data[3], (uint8_t)sizeof(very_long));
    uint16_t crc2 = vesc_crc16(very_long, sizeof(very_long));
    assert_int_equal(bus.tx[39].data[4], (uint8_t)(crc2 >> 8));
    assert_int_equal(bus.tx[39].data[5], (uint8_t)(crc2 & 0xFFu));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_vesc_can_init_validation),
        cmocka_unit_test(test_vesc_can_status_broadcast),
        cmocka_unit_test(test_vesc_can_rx_command),
        cmocka_unit_test(test_vesc_can_send_buffer_framing),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
