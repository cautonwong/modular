#include "motor_config/motor_config.h"
#include "motor_config_internal.h"
#include <string.h>

static edge_status_t motor_config_poll(edge_module_t *module) {
    motor_config_t *self = (motor_config_t *)edge_module_data(module);
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }

    if (self->is_dirty) {
        edge_status_t status = motor_config_save(self);
        if (status == EDGE_OK) {
            self->is_dirty = false;
        }
    }
    return EDGE_OK;
}

static edge_status_t motor_config_on_event(edge_module_t *module, const edge_event_t *event) {
    motor_config_t *self = (motor_config_t *)edge_module_data(module);
    if (self == (void *)0 || event == (void *)0) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

static edge_status_t motor_config_power_off(edge_module_t *module) {
    motor_config_t *self = (motor_config_t *)edge_module_data(module);
    if (self != (void *)0 && self->is_dirty) {
        (void)motor_config_save(self);
    }
    return EDGE_OK;
}

void motor_config_construct(motor_config_t *self, uint32_t module_id, uint32_t priority,
                            const motor_config_storage_port_t *storage, uint32_t flash_offset) {
    if (self == (void *)0) {
        return;
    }

    memset(self, 0, sizeof(*self));

    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 100u,
        .budget = 0u,
        .next_due = 0u,
        .poll = motor_config_poll,
        .on_event = motor_config_on_event,
        .power_off = motor_config_power_off,
        .private_data = self,
    };

    self->storage = storage;
    self->flash_offset = flash_offset;
    self->is_dirty = false;

    motor_config_set_defaults(&self->mcconf, &self->appconf);
}

edge_status_t motor_config_init(motor_config_t *self) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }

    /* Try loading saved config from storage */
    if (self->storage != (void *)0 && self->storage->read != (void *)0) {
        edge_status_t status = motor_config_load(self);
        if (status != EDGE_OK) {
            /* Flash uninitialized or corrupted: restore defaults and persist */
            motor_config_set_defaults(&self->mcconf, &self->appconf);
            (void)motor_config_save(self);
        }
    }

    return EDGE_OK;
}

edge_status_t motor_config_deinit(motor_config_t *self) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }
    if (self->is_dirty) {
        (void)motor_config_save(self);
    }
    return EDGE_OK;
}

edge_status_t motor_config_load(motor_config_t *self) {
    if (self == (void *)0 || self->storage == (void *)0 || self->storage->read == (void *)0) {
        return EDGE_EINVAL;
    }

    uint8_t *buffer = self->scratch;
    edge_status_t status =
        self->storage->read(self->storage->self, self->flash_offset, buffer, sizeof(self->scratch));
    if (status != EDGE_OK) {
        return status;
    }

    mc_configuration_t mc;
    app_configuration_t app;
    status = motor_config_deserialize(&mc, &app, buffer, sizeof(self->scratch));
    if (status != EDGE_OK) {
        return status;
    }

    self->mcconf = mc;
    self->appconf = app;
    self->is_dirty = false;
    return EDGE_OK;
}

edge_status_t motor_config_save(motor_config_t *self) {
    if (self == (void *)0 || self->storage == (void *)0 || self->storage->write == (void *)0) {
        return EDGE_EINVAL;
    }

    uint8_t *buffer = self->scratch;
    memset(buffer, 0xFF, sizeof(self->scratch));
    size_t out_len = 0;

    edge_status_t status = motor_config_serialize(&self->mcconf, &self->appconf, buffer,
                                                  sizeof(self->scratch), &out_len);
    if (status != EDGE_OK) {
        return status;
    }

    if (self->storage->erase != (void *)0) {
        status =
            self->storage->erase(self->storage->self, self->flash_offset, sizeof(self->scratch));
        if (status != EDGE_OK) {
            return status;
        }
    }

    status = self->storage->write(self->storage->self, self->flash_offset, buffer,
                                  sizeof(self->scratch));
    if (status == EDGE_OK) {
        self->is_dirty = false;
    }
    return status;
}

edge_module_t *motor_config_module(motor_config_t *self) {
    if (self == (void *)0) {
        return (void *)0;
    }
    return &self->module;
}

const mc_configuration_t *motor_config_get_mc(const motor_config_t *self) {
    if (self == (void *)0) {
        return (void *)0;
    }
    return &self->mcconf;
}

const app_configuration_t *motor_config_get_app(const motor_config_t *self) {
    if (self == (void *)0) {
        return (void *)0;
    }
    return &self->appconf;
}

edge_status_t motor_config_update_mc(motor_config_t *self, const mc_configuration_t *mcconf) {
    if (self == (void *)0 || mcconf == (void *)0) {
        return EDGE_EINVAL;
    }
    edge_status_t status = motor_config_validate(mcconf, &self->appconf);
    if (status != EDGE_OK) {
        return status;
    }
    self->mcconf = *mcconf;
    self->is_dirty = true;
    return EDGE_OK;
}

edge_status_t motor_config_update_app(motor_config_t *self, const app_configuration_t *appconf) {
    if (self == (void *)0 || appconf == (void *)0) {
        return EDGE_EINVAL;
    }
    edge_status_t status = motor_config_validate(&self->mcconf, appconf);
    if (status != EDGE_OK) {
        return status;
    }
    self->appconf = *appconf;
    self->is_dirty = true;
    return EDGE_OK;
}

edge_status_t motor_config_apply_mc_stream(motor_config_t *self, const uint8_t *buf, size_t len) {
    if (self == (void *)0 || buf == (void *)0) {
        return EDGE_EINVAL;
    }

    /*
     * Decoded into the staging copy first, which is the reference's shape: it copies the
     * live configuration and decodes into the copy, so a malformed stream from the peer
     * leaves the running configuration alone. Only a complete decode is published.
     */
    edge_status_t status = motor_config_deserialize_mc(&self->staging_mc, buf, len);
    if (status != EDGE_OK) {
        return status;
    }
    return motor_config_update_mc(self, &self->staging_mc);
}

edge_status_t motor_config_apply_app_stream(motor_config_t *self, const uint8_t *buf, size_t len) {
    if (self == (void *)0 || buf == (void *)0) {
        return EDGE_EINVAL;
    }

    edge_status_t status = motor_config_deserialize_app(&self->staging_app, buf, len);
    if (status != EDGE_OK) {
        return status;
    }
    return motor_config_update_app(self, &self->staging_app);
}
