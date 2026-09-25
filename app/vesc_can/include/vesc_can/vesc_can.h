#ifndef VESC_CAN_H
#define VESC_CAN_H

#include <stdbool.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"
#include "edge/modules.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CAN_PACKET_SET_DUTY = 0,
    CAN_PACKET_SET_CURRENT = 1,
    CAN_PACKET_SET_CURRENT_BRAKE = 2,
    CAN_PACKET_SET_RPM = 3,
    CAN_PACKET_SET_POS = 4,
    /* Reference datatypes.h:1162-1165: the multi-frame receive path comm_can_send_buffer
     * uses for payloads longer than six bytes. */
    CAN_PACKET_FILL_RX_BUFFER = 5,
    CAN_PACKET_FILL_RX_BUFFER_LONG = 6,
    CAN_PACKET_PROCESS_RX_BUFFER = 7,
    CAN_PACKET_PROCESS_SHORT_BUFFER = 8,
    CAN_PACKET_STATUS_1 = 9,
    CAN_PACKET_STATUS_2 = 14,
    CAN_PACKET_STATUS_3 = 15,
    CAN_PACKET_STATUS_4 = 16,
    CAN_PACKET_STATUS_5 = 27,
    CAN_PACKET_FORWARD_CAN = 33
} vesc_can_packet_id_t;

typedef struct vesc_can_status {
    float erpm;
    float current_motor;
    float duty_cycle;
    float amp_hours;
    float watt_hours;
    float temp_fet;
    float temp_motor;
    float current_in;
    float v_in;
    float pid_pos;
} vesc_can_status_t;

typedef struct vesc_can_port {
    void *self;
    edge_status_t (*send_frame)(void *self, uint32_t can_id, const uint8_t *data, uint8_t len);
    edge_status_t (*receive_frame)(void *self, uint32_t *can_id, uint8_t *data, uint8_t *len);
} vesc_can_port_t;

typedef struct vesc_can_config {
    uint8_t controller_id;
    uint32_t baudrate;
    float status_rate_hz;
} vesc_can_config_t;

typedef struct vesc_can_app {
    edge_module_t module;
    vesc_can_config_t config;
    vesc_can_port_t port;
    vesc_can_status_t status_to_broadcast;
    float last_set_duty;
    float last_set_current;
    float last_set_rpm;
    bool new_cmd_received;
} vesc_can_app_t;

void vesc_can_construct(vesc_can_app_t *app, uint32_t module_id, uint32_t priority,
                        const vesc_can_config_t *config, const vesc_can_port_t *port);
edge_status_t vesc_can_init(vesc_can_app_t *app);

edge_status_t vesc_can_send_status_1(vesc_can_app_t *app);
edge_status_t vesc_can_send_status_4(vesc_can_app_t *app);
edge_status_t vesc_can_send_status_5(vesc_can_app_t *app);

edge_status_t vesc_can_send_duty(vesc_can_app_t *app, uint8_t target_id, float duty);

/*
 * The reference's comm_can_send_buffer(): hand a whole packet to another controller. Six
 * bytes or less travel in one short-buffer frame; longer payloads are split into the
 * FILL_RX_BUFFER / FILL_RX_BUFFER_LONG frames and closed by a PROCESS_RX_BUFFER frame
 * carrying the sender, the length and a CRC. `send` is the reference's "is this a response"
 * flag, passed through unchanged.
 */
edge_status_t vesc_can_send_buffer(vesc_can_app_t *app, uint8_t controller_id, const uint8_t *data,
                                   size_t len, uint8_t send);
edge_status_t vesc_can_send_current(vesc_can_app_t *app, uint8_t target_id, float current);
edge_status_t vesc_can_send_rpm(vesc_can_app_t *app, uint8_t target_id, float rpm);

edge_status_t vesc_can_process_incoming(vesc_can_app_t *app);
void vesc_can_set_telemetry(vesc_can_app_t *app, const vesc_can_status_t *status);
edge_module_t *vesc_can_module(vesc_can_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* VESC_CAN_H */
