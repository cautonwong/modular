#include "relay/relay.h"

#include "edge/event.h"
#include "edge/events.h"

static edge_status_t relay_init(edge_module_t *module) {
    relay_t *self = (relay_t *)edge_module_data(module);
    if (self == NULL || self->out == NULL || self->out->set == NULL)
        return EDGE_EINVAL;
    self->state = 0u;
    self->toggles = 0u;
    self->last_event = 0u;
    return EDGE_OK;
}

static edge_status_t relay_poll(edge_module_t *module) {
    return edge_module_data(module) == NULL ? EDGE_EINVAL : EDGE_OK;
}

static edge_status_t relay_on_event(edge_module_t *module, const edge_event_t *event) {
    relay_t *self = (relay_t *)edge_module_data(module);
    if (self == NULL || event == NULL)
        return EDGE_EINVAL;
    self->last_event = event->id;
    if (event->id != EDGE_EVT_RELAY_CHANGED)
        return EDGE_OK;

    const bool on = event->arg1 != 0u;
    const edge_status_t rc = self->out->set(self->out->self, (uint8_t)event->arg0, on);
    if (rc < 0)
        return rc;
    self->state = on ? 1u : 0u;
    ++self->toggles;
    return EDGE_OK;
}

static edge_status_t relay_power_off(edge_module_t *module) {
    relay_t *self = (relay_t *)edge_module_data(module);
    if (self == NULL || self->out == NULL || self->out->set == NULL)
        return EDGE_EINVAL;
    self->state = 0u;
    return self->out->set(self->out->self, 0u, false);
}

static edge_status_t relay_deinit(edge_module_t *module) {
    relay_t *self = (relay_t *)edge_module_data(module);
    if (self == NULL)
        return EDGE_EINVAL;
    self->out = NULL;
    return EDGE_OK;
}

void relay_construct(relay_t *self, uint32_t module_id, uint32_t priority,
                     const relay_out_if_t *out) {
    if (self == NULL)
        return;
    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 1u,
        .budget = 0u,
        .next_due = 0u,
        .init = relay_init,
        .poll = relay_poll,
        .on_event = relay_on_event,
        .power_off = relay_power_off,
        .deinit = relay_deinit,
        .private_data = self,
    };
    self->out = out;
    self->state = 0u;
    self->toggles = 0u;
    self->last_event = 0u;
}
