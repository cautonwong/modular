#include "vesc_can/vesc_can.h"

#include <string.h>

static void write_i32_be(uint8_t *buf, int32_t val) {
    buf[0] = (uint8_t)((val >> 24) & 0xFF);
    buf[1] = (uint8_t)((val >> 16) & 0xFF);
    buf[2] = (uint8_t)((val >> 8) & 0xFF);
    buf[3] = (uint8_t)(val & 0xFF);
}

static void write_i16_be(uint8_t *buf, int16_t val) {
    buf[0] = (uint8_t)((val >> 8) & 0xFF);
    buf[1] = (uint8_t)(val & 0xFF);
}

static int32_t read_i32_be(const uint8_t *buf) {
    return ((int32_t)buf[0] << 24) | ((int32_t)buf[1] << 16) | ((int32_t)buf[2] << 8) |
           (int32_t)buf[3];
}

static edge_status_t vesc_can_poll(edge_module_t *mod) {
    vesc_can_app_t *app = (vesc_can_app_t *)edge_module_data(mod);
    return vesc_can_process_incoming(app);
}

static edge_status_t vesc_can_on_event(edge_module_t *mod, const edge_event_t *evt) {
    (void)mod;
    (void)evt;
    return EDGE_OK;
}

static edge_status_t vesc_can_power_off(edge_module_t *mod) {
    (void)mod;
    return EDGE_OK;
}

void vesc_can_construct(vesc_can_app_t *app, uint32_t module_id, uint32_t priority,
                        const vesc_can_config_t *config, const vesc_can_port_t *port) {
    if (!app) {
        return;
    }

    memset(app, 0, sizeof(*app));
    app->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 20u,
        .poll = vesc_can_poll,
        .on_event = vesc_can_on_event,
        .power_off = vesc_can_power_off,
        .private_data = app,
    };

    if (config) {
        app->config = *config;
    }
    if (app->config.baudrate == 0) {
        app->config.baudrate = 500000;
    }
    if (app->config.status_rate_hz <= 0.0f) {
        app->config.status_rate_hz = 50.0f;
    }

    if (port) {
        app->port = *port;
    }
}

edge_status_t vesc_can_init(vesc_can_app_t *app) {
    if (!app || !app->port.send_frame) {
        return EDGE_EINVAL;
    }
    memset(&app->status_to_broadcast, 0, sizeof(app->status_to_broadcast));
    app->last_set_duty = 0.0f;
    app->last_set_current = 0.0f;
    app->last_set_rpm = 0.0f;
    app->new_cmd_received = false;
    return EDGE_OK;
}

void vesc_can_set_telemetry(vesc_can_app_t *app, const vesc_can_status_t *status) {
    if (app && status) {
        app->status_to_broadcast = *status;
    }
}

edge_status_t vesc_can_send_status_1(vesc_can_app_t *app) {
    if (!app || !app->port.send_frame) {
        return EDGE_EINVAL;
    }

    uint32_t can_id = ((uint32_t)CAN_PACKET_STATUS_1 << 8) | (uint32_t)app->config.controller_id;
    uint8_t data[8];

    int32_t erpm = (int32_t)app->status_to_broadcast.erpm;
    int16_t current = (int16_t)(app->status_to_broadcast.current_motor * 10.0f);
    int16_t duty = (int16_t)(app->status_to_broadcast.duty_cycle * 1000.0f);

    write_i32_be(&data[0], erpm);
    write_i16_be(&data[4], current);
    write_i16_be(&data[6], duty);

    return app->port.send_frame(app->port.self, can_id, data, 8);
}

edge_status_t vesc_can_send_status_4(vesc_can_app_t *app) {
    if (!app || !app->port.send_frame) {
        return EDGE_EINVAL;
    }

    uint32_t can_id = ((uint32_t)CAN_PACKET_STATUS_4 << 8) | (uint32_t)app->config.controller_id;
    uint8_t data[8];

    int16_t temp_fet = (int16_t)(app->status_to_broadcast.temp_fet * 10.0f);
    int16_t temp_motor = (int16_t)(app->status_to_broadcast.temp_motor * 10.0f);
    int16_t current_in = (int16_t)(app->status_to_broadcast.current_in * 10.0f);
    int16_t pid_pos = (int16_t)(app->status_to_broadcast.pid_pos * 50.0f);

    write_i16_be(&data[0], temp_fet);
    write_i16_be(&data[2], temp_motor);
    write_i16_be(&data[4], current_in);
    write_i16_be(&data[6], pid_pos);

    return app->port.send_frame(app->port.self, can_id, data, 8);
}

edge_status_t vesc_can_send_status_5(vesc_can_app_t *app) {
    if (!app || !app->port.send_frame) {
        return EDGE_EINVAL;
    }

    uint32_t can_id = ((uint32_t)CAN_PACKET_STATUS_5 << 8) | (uint32_t)app->config.controller_id;
    uint8_t data[8];

    int32_t v_in = (int32_t)(app->status_to_broadcast.v_in * 10.0f);
    int32_t tacho = (int32_t)(app->status_to_broadcast.erpm / 6.0f);

    write_i32_be(&data[0], v_in);
    write_i32_be(&data[4], tacho);

    return app->port.send_frame(app->port.self, can_id, data, 8);
}

edge_status_t vesc_can_send_duty(vesc_can_app_t *app, uint8_t target_id, float duty) {
    if (!app || !app->port.send_frame) {
        return EDGE_EINVAL;
    }

    uint32_t can_id = ((uint32_t)CAN_PACKET_SET_DUTY << 8) | (uint32_t)target_id;
    uint8_t data[4];
    int32_t val = (int32_t)(duty * 100000.0f);
    write_i32_be(data, val);

    return app->port.send_frame(app->port.self, can_id, data, 4);
}

edge_status_t vesc_can_send_current(vesc_can_app_t *app, uint8_t target_id, float current) {
    if (!app || !app->port.send_frame) {
        return EDGE_EINVAL;
    }

    uint32_t can_id = ((uint32_t)CAN_PACKET_SET_CURRENT << 8) | (uint32_t)target_id;
    uint8_t data[4];
    int32_t val = (int32_t)(current * 1000.0f);
    write_i32_be(data, val);

    return app->port.send_frame(app->port.self, can_id, data, 4);
}

edge_status_t vesc_can_send_rpm(vesc_can_app_t *app, uint8_t target_id, float rpm) {
    if (!app || !app->port.send_frame) {
        return EDGE_EINVAL;
    }

    uint32_t can_id = ((uint32_t)CAN_PACKET_SET_RPM << 8) | (uint32_t)target_id;
    uint8_t data[4];
    int32_t val = (int32_t)rpm;
    write_i32_be(data, val);

    return app->port.send_frame(app->port.self, can_id, data, 4);
}

edge_status_t vesc_can_process_incoming(vesc_can_app_t *app) {
    if (!app || !app->port.receive_frame) {
        return EDGE_OK;
    }

    uint32_t can_id = 0;
    uint8_t data[8];
    uint8_t len = 0;

    while (app->port.receive_frame(app->port.self, &can_id, data, &len) == EDGE_OK) {
        uint8_t target_id = (uint8_t)(can_id & 0xFF);
        vesc_can_packet_id_t cmd = (vesc_can_packet_id_t)((can_id >> 8) & 0xFF);

        if (target_id != app->config.controller_id && target_id != 255) {
            continue; /* Not for this controller */
        }

        if (cmd == CAN_PACKET_SET_DUTY && len >= 4) {
            int32_t val = read_i32_be(data);
            app->last_set_duty = (float)val / 100000.0f;
            app->new_cmd_received = true;
        } else if (cmd == CAN_PACKET_SET_CURRENT && len >= 4) {
            int32_t val = read_i32_be(data);
            app->last_set_current = (float)val / 1000.0f;
            app->new_cmd_received = true;
        } else if (cmd == CAN_PACKET_SET_RPM && len >= 4) {
            int32_t val = read_i32_be(data);
            app->last_set_rpm = (float)val;
            app->new_cmd_received = true;
        }
    }

    return EDGE_OK;
}

edge_module_t *vesc_can_module(vesc_can_app_t *app) {
    return app ? &app->module : NULL;
}

/*
 * The reference's crc16() (util/crc.c): CRC-16/CCITT-FALSE, zero initial value, computed
 * bitwise here instead of with its 256-entry table. app/vesc_comm has a table-driven
 * vesc_crc16 that must agree with this across lengths and payload shapes, and
 * tests/test_app_vesc_can.c checks exactly that - which is what keeps the two honest
 * without either module reaching into the other.
 */
static uint16_t vesc_can_crc16(const uint8_t *buf, size_t len) {
    uint16_t cksum = 0u;
    for (size_t i = 0u; i < len; i++) {
        cksum ^= (uint16_t)((uint16_t)buf[i] << 8);
        for (int b = 0; b < 8; b++) {
            cksum = (cksum & 0x8000u) ? (uint16_t)((uint16_t)(cksum << 1) ^ 0x1021u)
                                      : (uint16_t)(cksum << 1);
        }
    }
    return cksum;
}

edge_status_t vesc_can_send_buffer(vesc_can_app_t *app, uint8_t controller_id, const uint8_t *data,
                                   size_t len, uint8_t send) {
    if (app == (void *)0 || data == (void *)0) {
        return EDGE_EINVAL;
    }

    uint8_t frame[8];
    const uint8_t own_id = app->config.controller_id;

    /*
     * Reference comm_can_send_buffer() (comm/comm_can.c), all three branches: six bytes or
     * less travel in one short-buffer frame; anything longer is split into FILL_RX_BUFFER
     * frames of a one-byte index plus seven payload bytes, and beyond index 255 the rest
     * goes in FILL_RX_BUFFER_LONG frames of a two-byte index plus six; a PROCESS_RX_BUFFER
     * frame then carries the sender, the length and a CRC over the payload.
     */
    if (len <= 6u) {
        frame[0] = own_id;
        frame[1] = send;
        memcpy(frame + 2u, data, len);
        return app->port.send_frame(app->port.self,
                                    ((uint32_t)CAN_PACKET_PROCESS_SHORT_BUFFER << 8) |
                                        (uint32_t)controller_id,
                                    frame, (uint8_t)(len + 2u));
    }

    size_t end_a = 0u;
    for (size_t i = 0u; i < len; i += 7u) {
        if (i > 255u) {
            break;
        }
        end_a = i + 7u;

        frame[0] = (uint8_t)i;
        size_t n = 7u;
        if ((i + 7u) > len) {
            n = len - i;
        }
        memcpy(frame + 1u, data + i, n);
        edge_status_t st = app->port.send_frame(
            app->port.self, ((uint32_t)CAN_PACKET_FILL_RX_BUFFER << 8) | (uint32_t)controller_id,
            frame, (uint8_t)(n + 1u));
        if (st != EDGE_OK) {
            return st;
        }
    }

    for (size_t i = end_a; i < len; i += 6u) {
        frame[0] = (uint8_t)(i >> 8);
        frame[1] = (uint8_t)(i & 0xFFu);
        size_t n = 6u;
        if ((i + 6u) > len) {
            n = len - i;
        }
        memcpy(frame + 2u, data + i, n);
        edge_status_t st = app->port.send_frame(app->port.self,
                                                ((uint32_t)CAN_PACKET_FILL_RX_BUFFER_LONG << 8) |
                                                    (uint32_t)controller_id,
                                                frame, (uint8_t)(n + 2u));
        if (st != EDGE_OK) {
            return st;
        }
    }

    const uint16_t crc = vesc_can_crc16(data, len);
    frame[0] = own_id;
    frame[1] = send;
    frame[2] = (uint8_t)(len >> 8);
    frame[3] = (uint8_t)(len & 0xFFu);
    frame[4] = (uint8_t)(crc >> 8);
    frame[5] = (uint8_t)(crc & 0xFFu);
    return app->port.send_frame(
        app->port.self, ((uint32_t)CAN_PACKET_PROCESS_RX_BUFFER << 8) | (uint32_t)controller_id,
        frame, 6u);
}
