#include "vesc_comm/vesc_comm.h"
#include "edge/errors.h"
#include "edge/module.h"
#include "vesc_comm_internal.h"
#include <string.h>

static edge_status_t vesc_comm_poll(edge_module_t *module) {
    vesc_comm_t *self = (vesc_comm_t *)edge_module_data(module);
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

static edge_status_t vesc_comm_on_event(edge_module_t *module, const edge_event_t *event) {
    vesc_comm_t *self = (vesc_comm_t *)edge_module_data(module);
    if (self == (void *)0 || event == (void *)0) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

static edge_status_t vesc_comm_power_off(edge_module_t *module) {
    vesc_comm_t *self = (vesc_comm_t *)edge_module_data(module);
    if (self != (void *)0) {
        (void)vesc_comm_deinit(self);
    }
    return EDGE_OK;
}

void vesc_comm_construct(vesc_comm_t *self, uint32_t module_id, uint32_t priority,
                         const edge_stream_tx_port_t *stream_tx,
                         const vesc_motor_provider_port_t *motor,
                         const vesc_app_status_port_t *app_status,
                         const vesc_config_provider_port_t *config,
                         const vesc_identity_t *identity) {
    if (self == (void *)0) {
        return;
    }

    memset(self, 0, sizeof(*self));

    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 10u,
        .budget = 0u,
        .next_due = 0u,
        .poll = vesc_comm_poll,
        .on_event = vesc_comm_on_event,
        .power_off = vesc_comm_power_off,
        .private_data = self,
    };

    self->stream_tx = stream_tx;
    self->motor = motor;
    self->app_status = app_status;
    self->config = config;
    self->identity = identity;
}

edge_status_t vesc_comm_init(vesc_comm_t *self) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }

    self->rx_write_ptr = 0;
    self->rx_read_ptr = 0;
    self->bytes_left = 0;
    self->packets_received = 0;
    self->packets_sent = 0;
    self->crc_errors = 0;

    return EDGE_OK;
}

edge_status_t vesc_comm_deinit(vesc_comm_t *self) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

edge_module_t *vesc_comm_module(vesc_comm_t *self) {
    if (self == (void *)0) {
        return (void *)0;
    }
    return &self->module;
}

uint32_t vesc_comm_packets_received(const vesc_comm_t *self) {
    return (self != (void *)0) ? self->packets_received : 0u;
}

uint32_t vesc_comm_packets_sent(const vesc_comm_t *self) {
    return (self != (void *)0) ? self->packets_sent : 0u;
}

uint32_t vesc_comm_crc_errors(const vesc_comm_t *self) {
    return (self != (void *)0) ? self->crc_errors : 0u;
}
