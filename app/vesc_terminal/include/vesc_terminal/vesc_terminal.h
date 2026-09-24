#ifndef VESC_TERMINAL_H
#define VESC_TERMINAL_H

#include <stdbool.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"
#include "edge/modules.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct terminal_stream_port {
    void *self;
    edge_status_t (*write_string)(void *self, const char *str);
} terminal_stream_port_t;

typedef struct terminal_system_port {
    void *self;
    edge_status_t (*get_stats)(void *self, float *rpm, float *iq, float *v_bus, float *temp_fet,
                               uint32_t *faults);
} terminal_system_port_t;

typedef struct vesc_terminal_app {
    edge_module_t module;
    terminal_stream_port_t stream_port;
    terminal_system_port_t sys_port;
} vesc_terminal_app_t;

void vesc_terminal_construct(vesc_terminal_app_t *app, uint32_t module_id, uint32_t priority,
                             const terminal_stream_port_t *stream_port,
                             const terminal_system_port_t *sys_port);
edge_status_t vesc_terminal_init(vesc_terminal_app_t *app);
edge_status_t vesc_terminal_execute(vesc_terminal_app_t *app, const char *cmd);
edge_module_t *vesc_terminal_module(vesc_terminal_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* VESC_TERMINAL_H */
