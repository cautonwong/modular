#ifndef APP_VESC_COMM_H
#define APP_VESC_COMM_H

#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VESC_PACKET_MAX_PL_LEN 512
#define VESC_PACKET_BUF_LEN (VESC_PACKET_MAX_PL_LEN + 8)

/* VESC Command IDs */
typedef enum {
    COMM_FW_VERSION = 0,
    COMM_JUMP_TO_BOOTLOADER = 1,
    COMM_ERASE_NEW_APP = 2,
    COMM_WRITE_NEW_APP_DATA = 3,
    COMM_GET_VALUES = 4,
    COMM_SET_DUTY = 5,
    COMM_SET_CURRENT = 6,
    COMM_SET_CURRENT_BRAKE = 7,
    COMM_SET_RPM = 8,
    COMM_SET_POS = 9,
    COMM_SET_HANDBRAKE = 10,
    COMM_REBOOT = 29,
    COMM_ALIVE = 30,
    COMM_GET_DECODED_ADC = 34,
    COMM_GET_DECODED_PPM = 35,
    COMM_GET_VALUES_SETUP = 65
} vesc_comm_cmd_t;

/* Telemetry values queried from motor provider */
typedef struct vesc_values {
    float temp_mos;
    float temp_motor;
    float current_motor;
    float current_in;
    float id;
    float iq;
    float duty_now;
    float rpm;
    float v_in;
    float amp_hours;
    float amp_hours_charged;
    float watt_hours;
    float watt_hours_charged;
    int32_t tachometer;
    int32_t tachometer_abs;
    uint32_t fault_code;
    float pid_pos_now;
} vesc_values_t;

/*
 * Consumer-Defined Ports (Rules: must have void *self; callbacks take void *self)
 */
typedef struct edge_stream_tx_port {
    edge_status_t (*write)(void *self, const uint8_t *data, size_t len);
    void *self;
} edge_stream_tx_port_t;

typedef struct vesc_motor_provider_port {
    edge_status_t (*get_values)(void *self, vesc_values_t *out_val);
    edge_status_t (*set_duty)(void *self, float duty);
    edge_status_t (*set_current)(void *self, float current);
    edge_status_t (*set_current_brake)(void *self, float current);
    edge_status_t (*set_rpm)(void *self, float rpm);
    edge_status_t (*set_pos)(void *self, float pos);
    void *self;
} vesc_motor_provider_port_t;

typedef struct vesc_comm {
    edge_module_t module;

    /* Injected Ports */
    const edge_stream_tx_port_t *stream_tx;
    const vesc_motor_provider_port_t *motor;

    /* Packet RX State */
    uint8_t rx_buffer[VESC_PACKET_BUF_LEN];
    size_t rx_write_ptr;
    size_t rx_read_ptr;
    int bytes_left;

    /* Packet TX Buffer */
    uint8_t tx_buffer[VESC_PACKET_BUF_LEN];

    /* Statistics */
    uint32_t packets_received;
    uint32_t packets_sent;
    uint32_t crc_errors;
} vesc_comm_t;

void vesc_comm_construct(vesc_comm_t *self, uint32_t module_id, uint32_t priority,
                         const edge_stream_tx_port_t *stream_tx,
                         const vesc_motor_provider_port_t *motor);

edge_status_t vesc_comm_init(vesc_comm_t *self);
edge_status_t vesc_comm_deinit(vesc_comm_t *self);
edge_module_t *vesc_comm_module(vesc_comm_t *self);

/* Packet Processing & Framing */
void vesc_comm_process_byte(vesc_comm_t *self, uint8_t byte);
edge_status_t vesc_comm_send_packet(vesc_comm_t *self, const uint8_t *payload, size_t len);
edge_status_t vesc_comm_process_command(vesc_comm_t *self, const uint8_t *data, size_t len);

/* Helper: CRC16 calculation */
uint16_t vesc_crc16(const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* APP_VESC_COMM_H */
