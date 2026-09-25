#ifndef APP_MOTOR_CONFIG_H
#define APP_MOTOR_CONFIG_H

#include "edge/errors.h"
#include "edge/module.h"
/*
 * mc_configuration_t, bms_config and the enums come from the generator: they are the
 * reference's own datatypes.h, emitted by tools/gen_mcconf_from_reference.py.
 */
#include "motor_config/config_structs.h"
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The reference's streams are 488 and 290 bytes. This is the buffer they need, with room to
 * spare; it is all a byte buffer is used for now, since the stored configuration is a
 * variable table rather than an image. */
#define MOTOR_CONFIG_BUFFER_SIZE 1024u

/*
 * Consumer-Defined Storage Port (Rules: void *self; callbacks take void *self)
 */
typedef struct motor_config motor_config_t;

/*
 * The variable store the reference actually persists a configuration in: EE_WriteVariable /
 * EE_ReadVariable, one uint16 variable per two bytes of mc_configuration, at virtual address
 * EEPROM_BASE_MCCONF + i (conf_general.c:436-520). Keys here are the logical index i; the base is
 * the consumer's business, which keeps the reference's address layout out of this module.
 */
typedef struct motor_config_var_port {
    edge_status_t (*read)(void *self, uint16_t index, uint16_t *value);
    edge_status_t (*write)(void *self, uint16_t index, uint16_t value);
    void *self;
} motor_config_var_port_t;

/*
 * The reference's mc_interface_calc_crc (motor/mc_interface.c:3067): the struct's own crc member
 * is zeroed, crc16 runs over the whole sizeof(mc_configuration) - padding included - and the field
 * is restored. Because padding is part of the input, the value only means anything against a
 * struct whose padding is deterministic, so this takes a mutable pointer rather than working on a
 * copy: use the aggregate's own memset configuration, never a stack temporary.
 */
uint16_t motor_config_config_crc(mc_configuration_t *mcconf);

/*
 * Opaque, caller-provided memory. The definition and its size/alignment
 * assertions live in src/motor_config_internal.h:
 *
 *   static alignas(MOTOR_CONFIG_STORAGE_ALIGN)
 *       unsigned char storage[MOTOR_CONFIG_STORAGE_SIZE];
 *   motor_config_t *cfg = (motor_config_t *)storage;
 *
 * The configuration itself is reached through motor_config_get_mc() / _get_app(),
 * so the caller never needs the layout to use the module.
 */
/*
 * The caller's storage contract: sizeof(struct motor_config) is 2552 with the generated
 * 177-member mc_configuration_t (776 of those bytes). Measured under this module's own
 * -std=c11, which lays the same aggregate out 256 bytes larger than -std=gnu11 does.
 * The assert in src/motor_config_internal.h turns a stale value into a build error rather
 * than an under-allocating caller.
 */
#define MOTOR_CONFIG_STORAGE_SIZE 2552u
#define MOTOR_CONFIG_STORAGE_ALIGN alignof(max_align_t)

void motor_config_set_defaults(mc_configuration_t *mcconf, app_configuration_t *appconf);

edge_status_t motor_config_validate(const mc_configuration_t *mcconf,
                                    const app_configuration_t *appconf);

/*
 * The reference's own mc_configuration stream - confgenerator_serialize_mcconf(): 488
 * bytes including the signature, with no version, length or CRC of its own. This is what
 * COMM_GET_MCCONF hands out, what COMM_SET_MCCONF decodes, and what the tests compare byte
 * for byte against the reference's own serialiser.
 */
size_t motor_config_stream_len(void);
edge_status_t motor_config_serialize_mc(const mc_configuration_t *mcconf, uint8_t *buffer,
                                        size_t buf_size, size_t *out_len);
edge_status_t motor_config_deserialize_mc(mc_configuration_t *mcconf, const uint8_t *buffer,
                                          size_t len);

/* The same, for the reference's app_configuration stream (confgenerator_serialize_appconf:
 * 290 bytes including the signature). */
size_t motor_config_app_stream_len(void);
edge_status_t motor_config_serialize_app(const app_configuration_t *appconf, uint8_t *buffer,
                                         size_t buf_size, size_t *out_len);
edge_status_t motor_config_deserialize_app(app_configuration_t *appconf, const uint8_t *buffer,
                                           size_t len);

void motor_config_construct(motor_config_t *self, uint32_t module_id, uint32_t priority,
                            const motor_config_var_port_t *vars);

edge_status_t motor_config_init(motor_config_t *self);
edge_status_t motor_config_deinit(motor_config_t *self);
edge_status_t motor_config_load(motor_config_t *self);
edge_status_t motor_config_save(motor_config_t *self);

edge_module_t *motor_config_module(motor_config_t *self);
const mc_configuration_t *motor_config_get_mc(const motor_config_t *self);
const app_configuration_t *motor_config_get_app(const motor_config_t *self);
edge_status_t motor_config_update_mc(motor_config_t *self, const mc_configuration_t *mcconf);
edge_status_t motor_config_update_app(motor_config_t *self, const app_configuration_t *appconf);

/*
 * Apply a stream from the peer, in the reference's own byte format. Decoded into a
 * staging copy first, so a malformed stream cannot leave half a configuration behind -
 * this is what COMM_SET_MCCONF / COMM_SET_APPCONF call.
 */
edge_status_t motor_config_apply_mc_stream(motor_config_t *self, const uint8_t *buf, size_t len);
edge_status_t motor_config_apply_app_stream(motor_config_t *self, const uint8_t *buf, size_t len);

/* COMM_SET_APPCONF_NO_STORE: applied to the running system, kept out of flash. */
edge_status_t motor_config_apply_app_stream_nostore(motor_config_t *self, const uint8_t *buf,
                                                    size_t len);

/*
 * COMM_GET_MCCONF_DEFAULT / COMM_GET_APPCONF_DEFAULT. The mc variant keeps the nine
 * calibration offsets from the live configuration, as the reference does, so a peer cannot
 * wipe a motor's measured calibration by asking for the defaults.
 */
edge_status_t motor_config_serialize_mc_defaults(motor_config_t *self, uint8_t *out,
                                                 size_t buf_size, size_t *out_len);
edge_status_t motor_config_serialize_app_defaults(uint8_t *out, size_t buf_size, size_t *out_len);

/* Whether the configuration has changes waiting to be written to flash. A NO_STORE apply
 * deliberately leaves this false. */
bool motor_config_is_dirty(const motor_config_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_MOTOR_CONFIG_H */
