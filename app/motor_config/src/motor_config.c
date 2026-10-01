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
                            const motor_config_var_port_t *vars) {
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

    self->vars = vars;
    self->is_dirty = false;

    motor_config_set_defaults(&self->mcconf, &self->appconf);
}

edge_status_t motor_config_init(motor_config_t *self) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }

    /* Try loading the saved configuration out of the variable store. */
    if (self->vars != (void *)0 && self->vars->read != (void *)0) {
        if (motor_config_load(self) != EDGE_OK) {
            /* Nothing stored yet, or the stored image failed its CRC: restore the defaults
             * and persist them, which is the reference's first-boot behaviour. */
            motor_config_set_defaults(&self->mcconf, &self->appconf);
            (void)motor_config_save(self);
        }

        /*
         * The application configuration, which the reference reads from its own base at boot
         * (conf_general.c:335-374) and falls back to the defaults from when the read or its CRC
         * says so. This port's defaults are reached through the default stream rather than a second
         * definition of them, and they are applied *without* storing - because the reference's own
         * read side writes them into the structure in RAM and leaves the store to whoever sets a
         * configuration next, which is also what keeps the two paths apart that this module's tests
         * hold it to.
         */
        if (motor_config_load_app(self) != EDGE_OK) {
            uint8_t app_defaults[512];
            size_t app_len = 0u;
            if (motor_config_serialize_app_defaults(app_defaults, sizeof(app_defaults), &app_len) ==
                EDGE_OK) {
                (void)motor_config_apply_app_stream_nostore(self, app_defaults, app_len);
            }
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
    if (self == (void *)0 || self->vars == (void *)0 || self->vars->read == (void *)0) {
        return EDGE_EINVAL;
    }

    /*
     * Read into the staging copy so a failed read cannot half-overwrite the running
     * configuration, then check the struct's own CRC exactly as
     * conf_general_read_mc_configuration does: a missing variable or a mismatch falls back to
     * the defaults.
     */
    uint8_t *bytes = (uint8_t *)&self->staging_mc;
    const size_t count = sizeof(self->staging_mc) / 2u;
    for (size_t i = 0u; i < count; i++) {
        uint16_t value = 0u;
        edge_status_t status = self->vars->read(self->vars->self, (uint16_t)i, &value);
        if (status != EDGE_OK) {
            motor_config_set_defaults(&self->mcconf, &self->appconf);
            return status;
        }
        bytes[2u * i] = (uint8_t)(value >> 8);
        bytes[2u * i + 1u] = (uint8_t)(value & 0xFFu);
    }

    if (self->staging_mc.crc != motor_config_config_crc(&self->staging_mc)) {
        motor_config_set_defaults(&self->mcconf, &self->appconf);
        return EDGE_EINVAL;
    }

    self->mcconf = self->staging_mc;
    self->is_dirty = false;
    return EDGE_OK;
}

/*
 * The application configuration's words follow the motor one's in the same store, which is the
 * reference's own arrangement: one base above the other (conf_general.c:50-51).
 */
#define MOTOR_CONFIG_APP_BASE (sizeof(mc_configuration_t) / 2u)

edge_status_t motor_config_load_app(motor_config_t *self) {
    if (self == (void *)0 || self->vars == (void *)0 || self->vars->read == (void *)0) {
        return EDGE_EINVAL;
    }

    /*
     * Staged, so a failed read cannot half-overwrite the running application configuration. The
     * defaults are the caller's to apply rather than this function's, because this module's own
     * default setter sets both configurations and would take the motor one with it.
     */
    uint8_t *bytes = (uint8_t *)&self->staging_app;
    const size_t count = sizeof(self->staging_app) / 2u;
    for (size_t i = 0u; i < count; i++) {
        uint16_t value = 0u;
        const edge_status_t status =
            self->vars->read(self->vars->self, (uint16_t)(MOTOR_CONFIG_APP_BASE + i), &value);
        if (status != EDGE_OK) {
            return status;
        }
        bytes[2u * i] = (uint8_t)(value >> 8);
        bytes[2u * i + 1u] = (uint8_t)(value & 0xFFu);
    }

    if (self->staging_app.crc != motor_config_app_crc(&self->staging_app)) {
        return EDGE_EINVAL;
    }

    self->appconf = self->staging_app;
    self->is_dirty = false;
    return EDGE_OK;
}

edge_status_t motor_config_save_app(motor_config_t *self) {
    if (self == (void *)0 || self->vars == (void *)0 || self->vars->write == (void *)0) {
        return EDGE_EINVAL;
    }

    /* The CRC goes into the struct's own field first, as the reference computes it before writing
     * (conf_general.c:395), so the stored image carries the CRC it was built with. */
    self->appconf.crc = motor_config_app_crc(&self->appconf);

    const uint8_t *bytes = (const uint8_t *)&self->appconf;
    const size_t count = sizeof(self->appconf) / 2u;
    for (size_t i = 0u; i < count; i++) {
        const uint16_t value =
            (uint16_t)(((uint16_t)bytes[2u * i] << 8) | (uint16_t)bytes[2u * i + 1u]);
        const edge_status_t status =
            self->vars->write(self->vars->self, (uint16_t)(MOTOR_CONFIG_APP_BASE + i), value);
        if (status != EDGE_OK) {
            return status;
        }
    }
    return EDGE_OK;
}

edge_status_t motor_config_save(motor_config_t *self) {
    if (self == (void *)0 || self->vars == (void *)0 || self->vars->write == (void *)0) {
        return EDGE_EINVAL;
    }

    /* The reference computes the CRC into the struct's own field first, then writes the whole
     * struct out as variables, so the stored image includes the CRC it was built with. */
    self->mcconf.crc = motor_config_config_crc(&self->mcconf);

    const uint8_t *bytes = (const uint8_t *)&self->mcconf;
    const size_t count = sizeof(self->mcconf) / 2u;
    for (size_t i = 0u; i < count; i++) {
        const uint16_t value =
            (uint16_t)(((uint16_t)bytes[2u * i] << 8) | (uint16_t)bytes[2u * i + 1u]);
        edge_status_t status = self->vars->write(self->vars->self, (uint16_t)i, value);
        if (status != EDGE_OK) {
            return status;
        }
    }

    self->is_dirty = false;
    return EDGE_OK;
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

edge_status_t motor_config_apply_app_stream_nostore(motor_config_t *self, const uint8_t *buf,
                                                    size_t len) {
    if (self == (void *)0 || buf == (void *)0) {
        return EDGE_EINVAL;
    }

    /*
     * The reference's COMM_SET_APPCONF_NO_STORE: apply it to the running system but keep it
     * out of flash. Here that means not marking the configuration dirty, since the module
     * only writes when it is - everything else is the normal path.
     */
    edge_status_t status = motor_config_deserialize_app(&self->staging_app, buf, len);
    if (status != EDGE_OK) {
        return status;
    }
    status = motor_config_validate(&self->mcconf, &self->staging_app);
    if (status != EDGE_OK) {
        return status;
    }
    self->appconf = self->staging_app;
    return EDGE_OK;
}

edge_status_t motor_config_serialize_mc_defaults(motor_config_t *self, uint8_t *out,
                                                 size_t buf_size, size_t *out_len) {
    if (self == (void *)0 || out == (void *)0 || out_len == (void *)0) {
        return EDGE_EINVAL;
    }

    /*
     * The reference's COMM_GET_MCCONF_DEFAULT: the reference's own defaults, except the nine
     * calibration offsets, which it copies from the live configuration so a peer cannot
     * throw away a motor's measured calibration by asking for the defaults.
     */
    motor_config_set_defaults(&self->staging_mc, &self->staging_app);
    for (size_t i = 0u; i < 3u; i++) {
        self->staging_mc.foc_offsets_current[i] = self->mcconf.foc_offsets_current[i];
        self->staging_mc.foc_offsets_voltage[i] = self->mcconf.foc_offsets_voltage[i];
        self->staging_mc.foc_offsets_voltage_undriven[i] =
            self->mcconf.foc_offsets_voltage_undriven[i];
    }
    return motor_config_serialize_mc(&self->staging_mc, out, buf_size, out_len);
}

edge_status_t motor_config_serialize_app_defaults(uint8_t *out, size_t buf_size, size_t *out_len) {
    if (out == (void *)0 || out_len == (void *)0) {
        return EDGE_EINVAL;
    }

    /* The reference's COMM_GET_APPCONF_DEFAULT: the defaults, nothing carried over. */
    mc_configuration_t mc;
    app_configuration_t app;
    motor_config_set_defaults(&mc, &app);
    return motor_config_serialize_app(&app, out, buf_size, out_len);
}

bool motor_config_is_dirty(const motor_config_t *self) {
    /*
     * Observable state, so COMM_SET_APPCONF_NO_STORE's "applied, not stored" is a testable
     * contract rather than a claim about a private field.
     */
    return (self != (void *)0) && self->is_dirty;
}

/*
 * Reference util/crc.c crc16(): CRC-16/CCITT-FALSE with a zero initial value, computed bitwise
 * rather than with the reference's 256-entry table. vesc_can carries a copy of this for the same
 * layering reason (an app may not depend on infra, and D30 decided against a shared util); the
 * tests cross-check this one against the codec's table-driven vesc_crc16 so the copies agree.
 */
static uint16_t config_crc16(const uint8_t *buf, size_t len) {
    uint16_t cksum = 0u;
    for (size_t i = 0u; i < len; i++) {
        cksum ^= (uint16_t)((uint16_t)buf[i] << 8);
        for (int b = 0; b < 8; b++) {
            cksum = (cksum & 0x8000u) ? (uint16_t)((uint16_t)(cksum << 1) ^ 0x1021u)
                                      : (uint16_t)(cksum << 1);
        }
    }
    return cksum;
}

uint16_t motor_config_app_crc(app_configuration_t *appconf) {
    if (appconf == (void *)0) {
        return 0u;
    }

    /*
     * The same rule as the motor configuration's below: the CRC covers the struct with its own
     * field zeroed, which is what the reference's app_calc_crc does (conf_general.c:373).
     */
    const uint16_t saved = appconf->crc;
    appconf->crc = 0u;
    const uint16_t crc = config_crc16((const uint8_t *)appconf, sizeof(*appconf));
    appconf->crc = saved;
    return crc;
}

uint16_t motor_config_config_crc(mc_configuration_t *mcconf) {
    if (mcconf == (void *)0) {
        return 0u;
    }

    /* In place, as the reference does: a struct copy's padding need not be copied, and the
     * padding is part of the CRC's input. */
    const uint16_t saved = mcconf->crc;
    mcconf->crc = 0u;
    const uint16_t crc = config_crc16((const uint8_t *)mcconf, sizeof(*mcconf));
    mcconf->crc = saved;
    return crc;
}
