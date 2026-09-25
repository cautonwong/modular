#ifndef APP_VESC_COMM_INTERNAL_H
#define APP_VESC_COMM_INTERNAL_H

/*
 * Private definition of vesc_comm. This is the only place the fields exist: the
 * public header exposes an opaque type plus a size and an alignment, so a caller
 * provides the memory without knowing - or being able to touch - the internals.
 *
 * The two assertions below are what make that safe in C. A storage contract
 * written by hand can silently go stale when a field is added here, and the caller
 * would then hand over a block that is too small; these turn that into a build
 * error instead of a memory corruption at runtime.
 */

#include "edge/module.h"
#include "vesc_comm/vesc_comm.h"
#include <stdalign.h>
#include <stddef.h>

struct vesc_comm {
    edge_module_t module;

    /* Injected Ports */
    const edge_stream_tx_port_t *stream_tx;
    const vesc_motor_provider_port_t *motor;
    const vesc_app_status_port_t *app_status;
    const vesc_config_provider_port_t *config;
    const vesc_identity_t *identity;

    /* Packet RX State */
    uint8_t rx_buffer[VESC_PACKET_BUF_LEN];
    size_t rx_write_ptr;
    size_t rx_read_ptr;
    int bytes_left;

    /* Packet TX Buffer */
    uint8_t tx_buffer[VESC_PACKET_BUF_LEN];

    /* Caller-provided reply scratch; see VESC_CMD_REPLY_BUF_LEN. */
    uint8_t cmd_reply_buf[VESC_CMD_REPLY_BUF_LEN];

    /* Statistics */
    uint32_t packets_received;
    uint32_t packets_sent;
    uint32_t crc_errors;
};

_Static_assert(sizeof(struct vesc_comm) <= VESC_COMM_STORAGE_SIZE,
               "VESC_COMM_STORAGE_SIZE is stale: the caller would under-allocate");
_Static_assert(alignof(struct vesc_comm) <= VESC_COMM_STORAGE_ALIGN,
               "VESC_COMM_STORAGE_ALIGN is stale: the caller would under-align");

#endif /* APP_VESC_COMM_INTERNAL_H */
