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
    float amp_hours_charged;
    float watt_hours;
    float watt_hours_charged;
    float temp_fet;
    float temp_motor;
    float current_in;
    float v_in;
    float pid_pos;
    /* Reference comm_can.c:1256: status 5's first field is the tachometer, which the aggregate
     * keeps in its own right rather than deriving from the speed. */
    int32_t tachometer;
} vesc_can_status_t;

typedef struct vesc_can_port {
    void *self;
    edge_status_t (*send_frame)(void *self, uint32_t can_id, const uint8_t *data, uint8_t len);
    edge_status_t (*receive_frame)(void *self, uint32_t *can_id, uint8_t *data, uint8_t *len);
} vesc_can_port_t;

/*
 * Reference datatypes.h:286: the modes the status threads are gated on. Only the first is a
 * scheduling mode; the UAVCAN one takes the frames elsewhere, which is why the gate exists at all.
 */
typedef enum { VESC_CAN_MODE_VESC = 0, VESC_CAN_MODE_VESC_UAVCAN = 1 } vesc_can_mode_t;

typedef struct vesc_can_config {
    uint8_t controller_id;
    uint32_t baudrate;
    uint8_t can_mode;
    /*
     * Reference comm_can.c:1526-1565: two periodic senders, each with its own rate in hertz and its
     * own mask of which status frames it sends - bit 0 is status 1, bit 1 status 2, and so on. A
     * rate of zero disables that sender, which is the loop the reference spins on.
     */
    float status_rate_1_hz;
    float status_rate_2_hz;
    uint8_t status_msgs_r1;
    uint8_t status_msgs_r2;
} vesc_can_config_t;

/*
 * Reference comm_can.h:27: ten slots per kind of status frame, and the receiving side matches a
 * slot by the sending controller's id or takes the first free one (comm_can.c:2038-2090). The
 * reference keeps four of these tables - the frames carry different things - and so does this port.
 */
#define VESC_CAN_STATUS_MSGS_TO_STORE 10u

/* mc_interface.c:1678: a peer counts while its frame is under a tenth of a second old. */
#define VESC_CAN_PEER_TIMEOUT_MS 100.0f

typedef struct vesc_can_peer_status {
    int8_t id; /* -1 until the slot is used, as the reference's own sentinel */
    float age_ms;
    float rpm;
    float current;
    float duty;
} vesc_can_peer_status_t;

typedef struct vesc_can_peer_status_2 {
    int8_t id;
    float age_ms;
    float amp_hours;
    float amp_hours_charged;
} vesc_can_peer_status_2_t;

typedef struct vesc_can_peer_status_3 {
    int8_t id;
    float age_ms;
    float watt_hours;
    float watt_hours_charged;
} vesc_can_peer_status_3_t;

typedef struct vesc_can_peer_status_4 {
    int8_t id;
    float age_ms;
    float temp_fet;
    float temp_motor;
    float current_in;
    float pid_pos_now;
} vesc_can_peer_status_4_t;

typedef struct vesc_can_app {
    edge_module_t module;
    vesc_can_config_t config;
    vesc_can_port_t port;
    vesc_can_status_t status_to_broadcast;
    float last_set_duty;
    float last_set_current;
    float last_set_rpm;
    bool new_cmd_received;
    /* The two senders' own time accounts, in milliseconds: the reference sleeps each of them its
     * own period, and here that period is accumulated against the module's tick. */
    float status_1_accum_ms;
    float status_2_accum_ms;
    /*
     * The peers' frames, one table per kind, and the same shape the reference keeps them in. A
     * frame's own arrival is what its age counts from, so a peer that stops sending falls out of
     * the totals a tenth of a second later.
     */
    vesc_can_peer_status_t peers_1[VESC_CAN_STATUS_MSGS_TO_STORE];
    vesc_can_peer_status_2_t peers_2[VESC_CAN_STATUS_MSGS_TO_STORE];
    vesc_can_peer_status_3_t peers_3[VESC_CAN_STATUS_MSGS_TO_STORE];
    vesc_can_peer_status_4_t peers_4[VESC_CAN_STATUS_MSGS_TO_STORE];
} vesc_can_app_t;

void vesc_can_construct(vesc_can_app_t *app, uint32_t module_id, uint32_t priority,
                        const vesc_can_config_t *config, const vesc_can_port_t *port);
edge_status_t vesc_can_init(vesc_can_app_t *app);

edge_status_t vesc_can_send_status_1(vesc_can_app_t *app);
edge_status_t vesc_can_send_status_2(vesc_can_app_t *app);
edge_status_t vesc_can_send_status_3(vesc_can_app_t *app);
edge_status_t vesc_can_send_status_4(vesc_can_app_t *app);
edge_status_t vesc_can_send_status_5(vesc_can_app_t *app);

/*
 * What the received frames add up to, which is what mc_interface_get_setup_values does with them
 * (mc_interface.c:1665-1700): every peer whose frame is still under a tenth of a second old counts,
 * and its own numbers join the totals. num_vescs_extra is those peers, so the caller's own machine
 * is the one it does not include.
 */
typedef struct vesc_can_peer_totals {
    uint8_t num_vescs_extra;
    float current_tot;
    float ah_tot;
    float ah_charge_tot;
    float wh_tot;
    float wh_charge_tot;
    float current_in_tot;
} vesc_can_peer_totals_t;

void vesc_can_get_peer_totals(const vesc_can_app_t *app, vesc_can_peer_totals_t *out);
/*
 * Which peers are there, by the same test the aggregate above uses for a slot that counts: its id
 * is set and its frame is younger than VESC_CAN_PEER_TIMEOUT_MS. COMM_PING_CAN asks this question,
 * and the reference asks it by pinging and waiting for answers; a peer that is answering is one
 * whose frames are arriving, which is what this reads. The count is what was written, up to max.
 */
size_t vesc_can_collect_peer_ids(const vesc_can_app_t *app, uint8_t *ids, size_t max);

/*
 * Reference comm_can.c:1470, send_can_status: the mask chooses which of the six frames go out. Bits
 * 0 to 4 are ported; bit 5 - status 6 - needs the three external ADC channels and the servo output,
 * which no product here has, so it is named and skipped rather than sent as zeros.
 */
edge_status_t vesc_can_send_masked(vesc_can_app_t *app, uint8_t msgs);

/*
 * The two rate senders, advanced by however long this module's tick was: the reference gives each
 * its own thread sleeping a full period per round. The remainder is kept rather than dropped, so a
 * period that is not a whole number of ticks still averages to the configured rate.
 */
void vesc_can_tick_status(vesc_can_app_t *app, float dt_ms);

/*
 * The module's whole tick: the peers' frames age by it, and the two senders are advanced by it. The
 * reference has a thread per job; here one tick does both, in the order a cycle would.
 */
void vesc_can_tick(vesc_can_app_t *app, float dt_ms);

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
