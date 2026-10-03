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

static int16_t read_i16_be(const uint8_t *buf) {
    return (int16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
}

static edge_status_t vesc_can_poll(edge_module_t *mod) {
    vesc_can_app_t *app = (vesc_can_app_t *)edge_module_data(mod);
    const edge_status_t status = vesc_can_process_incoming(app);

    /* The peers' frames age, the two senders are advanced: the reference runs each of those jobs in
     * a thread of its own, and this module has one tick. */
    vesc_can_tick(app, (float)app->module.period);

    return status;
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
    app->status_1_accum_ms = 0.0f;
    app->status_2_accum_ms = 0.0f;
    /* The reference's own sentinel for an unused slot, set here rather than by the memset. */
    for (size_t i = 0u; i < VESC_CAN_STATUS_MSGS_TO_STORE; i++) {
        app->peers_1[i].id = -1;
        app->peers_2[i].id = -1;
        app->peers_3[i].id = -1;
        app->peers_4[i].id = -1;
    }
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

/*
 * Reference comm_can.c:1226, comm_can_send_status2: the two charge counters, each in amp-hours at
 * 1e4 - what the CAN peer uses to keep its own totals for a multi-controller vehicle.
 */
edge_status_t vesc_can_send_status_2(vesc_can_app_t *app) {
    if (!app || !app->port.send_frame) {
        return EDGE_EINVAL;
    }

    const uint32_t can_id =
        ((uint32_t)CAN_PACKET_STATUS_2 << 8) | (uint32_t)app->config.controller_id;
    uint8_t data[8];

    write_i32_be(&data[0], (int32_t)(app->status_to_broadcast.amp_hours * 1e4f));
    write_i32_be(&data[4], (int32_t)(app->status_to_broadcast.amp_hours_charged * 1e4f));

    return app->port.send_frame(app->port.self, can_id, data, 8);
}

/*
 * Reference comm_can.c:1235, comm_can_send_status3: the same two counters in watt-hours.
 */
edge_status_t vesc_can_send_status_3(vesc_can_app_t *app) {
    if (!app || !app->port.send_frame) {
        return EDGE_EINVAL;
    }

    const uint32_t can_id =
        ((uint32_t)CAN_PACKET_STATUS_3 << 8) | (uint32_t)app->config.controller_id;
    uint8_t data[8];

    write_i32_be(&data[0], (int32_t)(app->status_to_broadcast.watt_hours * 1e4f));
    write_i32_be(&data[4], (int32_t)(app->status_to_broadcast.watt_hours_charged * 1e4f));

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

    const uint32_t can_id =
        ((uint32_t)CAN_PACKET_STATUS_5 << 8) | (uint32_t)app->config.controller_id;
    uint8_t data[8];

    /* comm_can.c:1255-1261: the tachometer whole, the input voltage at a tenth and one int16 the
     * reference reserves. This builder used to write the voltage as a 32-bit field and the
     * tachometer after it, which is not the frame the reference sends. */
    write_i32_be(&data[0], app->status_to_broadcast.tachometer);
    write_i16_be(&data[4], (int16_t)(app->status_to_broadcast.v_in * 1e1f));
    write_i16_be(&data[6], 0);

    return app->port.send_frame(app->port.self, can_id, data, 8);
}

edge_status_t vesc_can_send_masked(vesc_can_app_t *app, uint8_t msgs) {
    if (app == (void *)0) {
        return EDGE_EINVAL;
    }

    edge_status_t status = EDGE_OK;

    /* Bits 0 to 4, the five frames this port builds. Bit 5 is status 6, whose fields are three
     * external ADC channels and the servo output; no product here has either, so it is not sent as
     * zeros but left out, which is a difference a peer can see in its own frame count. */
    if ((msgs >> 0) & 1u) {
        status = vesc_can_send_status_1(app);
    }
    if ((msgs >> 1) & 1u) {
        status = vesc_can_send_status_2(app);
    }
    if ((msgs >> 2) & 1u) {
        status = vesc_can_send_status_3(app);
    }
    if ((msgs >> 3) & 1u) {
        status = vesc_can_send_status_4(app);
    }
    if ((msgs >> 4) & 1u) {
        status = vesc_can_send_status_5(app);
    }

    return status;
}

/*
 * mc_interface.c:1665-1700's own loop over the peers: a slot counts while its frame is younger than
 * a tenth of a second, and each kind contributes what it carries. The reference reads a system
 * clock for that age; here the module's own tick advances it, which is the same window measured by
 * the cycle that would have done the reading.
 */
void vesc_can_get_peer_totals(const vesc_can_app_t *app, vesc_can_peer_totals_t *out) {
    if (app == (void *)0 || out == (void *)0) {
        return;
    }

    memset(out, 0, sizeof(*out));

    for (size_t i = 0u; i < VESC_CAN_STATUS_MSGS_TO_STORE; i++) {
        if (app->peers_1[i].id >= 0 && app->peers_1[i].age_ms < VESC_CAN_PEER_TIMEOUT_MS) {
            out->current_tot += app->peers_1[i].current;
            out->num_vescs_extra++;
        }

        if (app->peers_2[i].id >= 0 && app->peers_2[i].age_ms < VESC_CAN_PEER_TIMEOUT_MS) {
            out->ah_tot += app->peers_2[i].amp_hours;
            out->ah_charge_tot += app->peers_2[i].amp_hours_charged;
        }

        if (app->peers_3[i].id >= 0 && app->peers_3[i].age_ms < VESC_CAN_PEER_TIMEOUT_MS) {
            out->wh_tot += app->peers_3[i].watt_hours;
            out->wh_charge_tot += app->peers_3[i].watt_hours_charged;
        }

        if (app->peers_4[i].id >= 0 && app->peers_4[i].age_ms < VESC_CAN_PEER_TIMEOUT_MS) {
            out->current_in_tot += app->peers_4[i].current_in;
        }
    }
}

void vesc_can_tick(vesc_can_app_t *app, float dt_ms) {
    if (app == (void *)0 || dt_ms <= 0.0f) {
        return;
    }

    for (size_t i = 0u; i < VESC_CAN_STATUS_MSGS_TO_STORE; i++) {
        app->peers_1[i].age_ms += dt_ms;
        app->peers_2[i].age_ms += dt_ms;
        app->peers_3[i].age_ms += dt_ms;
        app->peers_4[i].age_ms += dt_ms;
    }

    vesc_can_tick_status(app, dt_ms);
}

void vesc_can_tick_status(vesc_can_app_t *app, float dt_ms) {
    if (app == (void *)0 || dt_ms <= 0.0f) {
        return;
    }

    /* comm_can.c:1526-1565: each sender runs only in the VESC CAN mode, and only while its rate is
     * non-zero - the reference spins on a ten-millisecond sleep in that case rather than sending.
     */
    if (app->config.can_mode != (uint8_t)VESC_CAN_MODE_VESC) {
        return;
    }

    if (app->config.status_rate_1_hz > 0.0f) {
        app->status_1_accum_ms += dt_ms;
        const float period_ms = 1000.0f / app->config.status_rate_1_hz;
        if (app->status_1_accum_ms >= period_ms) {
            app->status_1_accum_ms -= period_ms;
            (void)vesc_can_send_masked(app, app->config.status_msgs_r1);
        }
    }

    if (app->config.status_rate_2_hz > 0.0f) {
        app->status_2_accum_ms += dt_ms;
        const float period_ms = 1000.0f / app->config.status_rate_2_hz;
        if (app->status_2_accum_ms >= period_ms) {
            app->status_2_accum_ms -= period_ms;
            (void)vesc_can_send_masked(app, app->config.status_msgs_r2);
        }
    }
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

/*
 * The received status frames' own tables (comm_can.c:2038-2090): a slave's entry is matched by the
 * id that sent it, or the first free slot is taken, and the arrival restarts its age. The reference
 * files these under "addressed to all devices", which is why they are handled before the check that
 * a command must be for this controller - a peer's frame carries the peer's own id.
 *
 * Returns true when the frame was one of the four kinds, handled or not.
 */
static bool vesc_can_store_peer_frame(vesc_can_app_t *app, vesc_can_packet_id_t cmd, uint8_t id,
                                      const uint8_t *data, uint8_t len) {
    switch (cmd) {
    case CAN_PACKET_STATUS_1:
        if (len < 8) {
            return true;
        }
        for (size_t i = 0u; i < VESC_CAN_STATUS_MSGS_TO_STORE; i++) {
            vesc_can_peer_status_t *slot = &app->peers_1[i];
            if (slot->id == (int8_t)id || slot->id == -1) {
                slot->id = (int8_t)id;
                slot->age_ms = 0.0f;
                slot->rpm = (float)read_i32_be(&data[0]);
                slot->current = (float)read_i16_be(&data[4]) / 10.0f;
                slot->duty = (float)read_i16_be(&data[6]) / 1000.0f;
                break;
            }
        }
        return true;

    case CAN_PACKET_STATUS_2:
        if (len < 8) {
            return true;
        }
        for (size_t i = 0u; i < VESC_CAN_STATUS_MSGS_TO_STORE; i++) {
            vesc_can_peer_status_2_t *slot = &app->peers_2[i];
            if (slot->id == (int8_t)id || slot->id == -1) {
                slot->id = (int8_t)id;
                slot->age_ms = 0.0f;
                slot->amp_hours = (float)read_i32_be(&data[0]) / 1e4f;
                slot->amp_hours_charged = (float)read_i32_be(&data[4]) / 1e4f;
                break;
            }
        }
        return true;

    case CAN_PACKET_STATUS_3:
        if (len < 8) {
            return true;
        }
        for (size_t i = 0u; i < VESC_CAN_STATUS_MSGS_TO_STORE; i++) {
            vesc_can_peer_status_3_t *slot = &app->peers_3[i];
            if (slot->id == (int8_t)id || slot->id == -1) {
                slot->id = (int8_t)id;
                slot->age_ms = 0.0f;
                slot->watt_hours = (float)read_i32_be(&data[0]) / 1e4f;
                slot->watt_hours_charged = (float)read_i32_be(&data[4]) / 1e4f;
                break;
            }
        }
        return true;

    case CAN_PACKET_STATUS_4:
        if (len < 8) {
            return true;
        }
        for (size_t i = 0u; i < VESC_CAN_STATUS_MSGS_TO_STORE; i++) {
            vesc_can_peer_status_4_t *slot = &app->peers_4[i];
            if (slot->id == (int8_t)id || slot->id == -1) {
                slot->id = (int8_t)id;
                slot->age_ms = 0.0f;
                slot->temp_fet = (float)read_i16_be(&data[0]) / 10.0f;
                slot->temp_motor = (float)read_i16_be(&data[2]) / 10.0f;
                slot->current_in = (float)read_i16_be(&data[4]) / 10.0f;
                slot->pid_pos_now = (float)read_i16_be(&data[6]) / 50.0f;
                break;
            }
        }
        return true;

    default:
        return false;
    }
}

edge_status_t vesc_can_process_incoming(vesc_can_app_t *app) {
    if (!app || !app->port.receive_frame) {
        return EDGE_OK;
    }

    uint32_t can_id = 0;
    uint8_t data[8];
    uint8_t len = 0;

    while (app->port.receive_frame(app->port.self, &can_id, data, &len) == EDGE_OK) {
        const uint8_t target_id = (uint8_t)(can_id & 0xFF);
        const vesc_can_packet_id_t cmd = (vesc_can_packet_id_t)((can_id >> 8) & 0xFF);

        /* The status frames are addressed to everyone and carry the sender's own id, so they are
         * filed before the check that would otherwise drop them. */
        if (vesc_can_store_peer_frame(app, cmd, target_id, data, len)) {
            continue;
        }

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
