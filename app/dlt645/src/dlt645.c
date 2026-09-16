#include "dlt645/dlt645.h"
#include "edge/event.h"
#include "edge/events.h"

static edge_status_t dlt645_init(edge_module_t *module) {
    dlt645_t *self = (dlt645_t *)edge_module_data(module);
    if (self == NULL || self->storage == NULL || self->storage->read == NULL)
        return EDGE_EINVAL;
    self->poll_count = 0u;
    self->last_event = 0u;
    return EDGE_OK;
}

static edge_status_t dlt645_poll(edge_module_t *module) {
    dlt645_t *self = (dlt645_t *)edge_module_data(module);
    if (self == NULL)
        return EDGE_EINVAL;
    ++self->poll_count;
    return EDGE_OK;
}

static edge_status_t dlt645_on_event(edge_module_t *module, const edge_event_t *event) {
    dlt645_t *self = (dlt645_t *)edge_module_data(module);
    if (self == NULL || event == NULL)
        return EDGE_EINVAL;
    self->last_event = event->id;
    return EDGE_OK;
}

static edge_status_t dlt645_power_off(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t dlt645_deinit(edge_module_t *module) {
    dlt645_t *self = (dlt645_t *)edge_module_data(module);
    if (self == NULL)
        return EDGE_EINVAL;
    self->storage = NULL;
    return EDGE_OK;
}

void dlt645_construct(dlt645_t *self, uint32_t module_id, uint32_t priority,
                      const dlt645_storage_if_t *storage) {
    if (self == NULL)
        return;
    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .init = dlt645_init,
        .poll = dlt645_poll,
        .on_event = dlt645_on_event,
        .power_off = dlt645_power_off,
        .deinit = dlt645_deinit,
        .private_data = self,
    };
    self->storage = storage;
    self->poll_count = 0u;
    self->last_event = 0u;
}
