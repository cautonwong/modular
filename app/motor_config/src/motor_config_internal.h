#ifndef APP_MOTOR_CONFIG_INTERNAL_H
#define APP_MOTOR_CONFIG_INTERNAL_H

/*
 * Private definition of motor_config. The public header exposes an opaque type
 * plus a size and an alignment; these assertions keep that contract from going
 * stale when a field is added here.
 */

#include "edge/module.h"
#include "motor_config/motor_config.h"
#include <stdalign.h>
#include <stddef.h>

struct motor_config {
    edge_module_t module;

    /* Injected Storage Port */
    const motor_config_storage_port_t *storage;

    /* Active Configurations */
    mc_configuration_t mcconf;
    app_configuration_t appconf;

    /*
     * Staging copies for a stream arriving from the peer. The reference copies the live
     * configuration and decodes into the copy, so a malformed stream cannot leave half a
     * configuration behind; these are that copy, owned here rather than on the stack
     * because mc_configuration_t is 776 bytes.
     */
    mc_configuration_t staging_mc;
    app_configuration_t staging_app;

    /* Flash Offset */
    uint32_t flash_offset;
    bool is_dirty;
    /* Serialisation scratch for load/save; see the public header's note. */
    uint8_t scratch[MOTOR_CONFIG_BUFFER_SIZE];
};

_Static_assert(sizeof(struct motor_config) <= MOTOR_CONFIG_STORAGE_SIZE,
               "MOTOR_CONFIG_STORAGE_SIZE is stale: the caller would under-allocate");
_Static_assert(alignof(struct motor_config) <= MOTOR_CONFIG_STORAGE_ALIGN,
               "MOTOR_CONFIG_STORAGE_ALIGN is stale: the caller would under-align");

#endif /* APP_MOTOR_CONFIG_INTERNAL_H */
