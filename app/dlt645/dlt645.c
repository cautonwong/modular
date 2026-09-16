#include "dlt645.h"
#include <stddef.h>

static int dlt645_init(edge_module_t *self) {
    dlt645_t *app = (dlt645_t *)self->private_data;
    if (!app || !app->storage || !app->storage->read) return EDGE_EINVAL;
    return EDGE_OK;
}

static int dlt645_poll(edge_module_t *self) {
    dlt645_t *app = (dlt645_t *)self->private_data;
    uint8_t value = 0;
    if (!app || !app->storage || !app->storage->read) return EDGE_EINVAL;
    if (app->storage->read(app->storage->self, 0u, &value, sizeof(value)) < 0) return EDGE_ESTATE;
    ++app->reads;
    return EDGE_OK;
}

static int dlt645_power_off(edge_module_t *self) {
    (void)self;
    return EDGE_OK;
}

dlt645_t *dlt645_new(dlt645_t *app, const dlt645_storage_if *storage_if) {
    if (!app || !storage_if || !storage_if->read) return NULL;
    app->storage = storage_if;
    app->reads = 0;
    app->mod.module_id = 0x1001u;
    app->mod.priority = 100u;
    app->mod.init = dlt645_init;
    app->mod.poll = dlt645_poll;
    app->mod.power_off = dlt645_power_off;
    app->mod.private_data = app;
    return app;
}
