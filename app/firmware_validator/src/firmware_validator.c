#include "firmware_validator/firmware_validator.h"

static edge_status_t firmware_validator_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t firmware_validator_on_event(edge_module_t *module, const edge_event_t *event) {
    (void)module;
    (void)event;
    return EDGE_OK;
}

static edge_status_t firmware_validator_power_off(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

void firmware_validator_construct(firmware_validator_t *self, uint32_t module_id, uint32_t priority,
                                  const firmware_validator_port_t *port,
                                  const edge_event_sink_t *event_sink) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};
    self->module.module_id = module_id;
    self->module.priority = priority;
    self->module.period = 1000u;
    self->module.budget = 100u;
    self->module.next_due = 0u;
    self->module.poll = firmware_validator_poll;
    self->module.on_event = firmware_validator_on_event;
    self->module.power_off = firmware_validator_power_off;
    self->module.private_data = self;

    self->port = port;
    self->event_sink = event_sink;
}

edge_status_t firmware_validator_init(firmware_validator_t *self,
                                      const firmware_validator_port_t *port,
                                      const edge_event_sink_t *event_sink) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    firmware_validator_construct(self, 0x3700u, 10u, port, event_sink);
    return EDGE_OK;
}

bool firmware_validator_is_validated(const firmware_validator_t *self) {
    if (self == NULL || self->port == NULL || self->port->read_word == NULL) {
        return false;
    }
    uint32_t val = 0;
    if (self->port->read_word(self->port->self, FIRMWARE_VALIDATOR_VALID_BIT_ADDR, &val) !=
        EDGE_OK) {
        return false;
    }
    return val == FIRMWARE_VALIDATOR_VALID_BIT_VAL;
}

edge_status_t firmware_validator_validate(firmware_validator_t *self) {
    if (self == NULL || self->port == NULL || self->port->write_word == NULL) {
        return EDGE_EINVAL;
    }
    if (!firmware_validator_is_validated(self)) {
        return self->port->write_word(self->port->self, FIRMWARE_VALIDATOR_VALID_BIT_ADDR,
                                      FIRMWARE_VALIDATOR_VALID_BIT_VAL);
    }
    return EDGE_OK;
}

edge_status_t firmware_validator_reset(firmware_validator_t *self) {
    if (self == NULL || self->port == NULL || self->port->system_reset == NULL) {
        return EDGE_EINVAL;
    }
    return self->port->system_reset(self->port->self);
}
