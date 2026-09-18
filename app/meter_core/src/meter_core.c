#include "meter_core/meter_core.h"

#include "edge/event.h"

static edge_status_t meter_core_init(edge_module_t *module) {
    meter_core_t *self = (meter_core_t *)edge_module_data(module);
    if (self == NULL)
        return EDGE_EINVAL;
    self->polls = 0u;
    self->pulses = 0u;
    self->last_event = 0u;
    return EDGE_OK;
}

static edge_status_t meter_core_poll(edge_module_t *module) {
    meter_core_t *self = (meter_core_t *)edge_module_data(module);
    if (self == NULL)
        return EDGE_EINVAL;
    ++self->polls;
    /* Synthetic accumulation so the register file evolves over time. */
    meter_core_pulse(self, 1u);
    return EDGE_OK;
}

static edge_status_t meter_core_on_event(edge_module_t *module, const edge_event_t *event) {
    meter_core_t *self = (meter_core_t *)edge_module_data(module);
    if (self == NULL || event == NULL)
        return EDGE_EINVAL;
    self->last_event = event->id;
    return EDGE_OK;
}

static edge_status_t meter_core_power_off(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t meter_core_deinit(edge_module_t *module) {
    meter_core_t *self = (meter_core_t *)edge_module_data(module);
    if (self == NULL)
        return EDGE_EINVAL;
    self->polls = 0u;
    return EDGE_OK;
}

void meter_core_construct(meter_core_t *self, uint32_t module_id, uint32_t priority) {
    if (self == NULL)
        return;
    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 1u,
        .budget = 0u,
        .next_due = 0u,
        .init = meter_core_init,
        .poll = meter_core_poll,
        .on_event = meter_core_on_event,
        .power_off = meter_core_power_off,
        .deinit = meter_core_deinit,
        .private_data = self,
    };
    for (uint16_t i = 0u; i < METER_CORE_REGISTER_COUNT; ++i)
        self->registers[i] = 0u;
    for (uint16_t i = 0u; i < METER_CORE_COIL_COUNT; ++i)
        self->coils[i] = 0u;
    self->polls = 0u;
    self->pulses = 0u;
    self->last_event = 0u;
}

edge_status_t meter_core_read_register(const meter_core_t *self, uint16_t addr, uint16_t *value) {
    if (self == NULL || value == NULL)
        return EDGE_EINVAL;
    if (addr >= METER_CORE_REGISTER_COUNT)
        return EDGE_ENOENT;
    *value = self->registers[addr];
    return EDGE_OK;
}

edge_status_t meter_core_write_register(meter_core_t *self, uint16_t addr, uint16_t value) {
    if (self == NULL)
        return EDGE_EINVAL;
    if (addr >= METER_CORE_REGISTER_COUNT)
        return EDGE_ENOENT;
    self->registers[addr] = value;
    return EDGE_OK;
}

edge_status_t meter_core_read_coil(const meter_core_t *self, uint16_t addr, bool *value) {
    if (self == NULL || value == NULL)
        return EDGE_EINVAL;
    if (addr >= METER_CORE_COIL_COUNT)
        return EDGE_ENOENT;
    *value = self->coils[addr] != 0u;
    return EDGE_OK;
}

edge_status_t meter_core_write_coil(meter_core_t *self, uint16_t addr, bool value) {
    if (self == NULL)
        return EDGE_EINVAL;
    if (addr >= METER_CORE_COIL_COUNT)
        return EDGE_ENOENT;
    self->coils[addr] = value ? 1u : 0u;
    return EDGE_OK;
}

void meter_core_pulse(meter_core_t *self, uint32_t count) {
    if (self == NULL)
        return;
    self->pulses += count;
    self->registers[0] = (uint16_t)(self->registers[0] + (uint16_t)(count & 0xFFFFu));
    self->registers[1] = (uint16_t)(self->registers[1] + (uint16_t)((count >> 16) & 0xFFFFu));
}
