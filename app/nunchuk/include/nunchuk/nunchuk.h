#ifndef NUNCHUK_H
#define NUNCHUK_H

#include <stdbool.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"
#include "edge/modules.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nunchuk_control_mode {
    NUNCHUK_MODE_CURRENT = 0,
    NUNCHUK_MODE_CURRENT_NOREV,
    NUNCHUK_MODE_DUTY,
    NUNCHUK_MODE_CRUISE
} nunchuk_control_mode_t;

typedef struct nunchuk_port {
    void *self;
    edge_status_t (*read_data)(void *self, uint8_t *js_x, uint8_t *js_y, int16_t *acc_x,
                               int16_t *acc_y, int16_t *acc_z, bool *btn_c, bool *btn_z);
} nunchuk_port_t;

typedef struct nunchuk_config {
    nunchuk_control_mode_t mode;
    float deadband;
    float ramp_time_s;
    float timeout_s;
} nunchuk_config_t;

typedef struct nunchuk_app {
    edge_module_t module;
    nunchuk_config_t config;
    nunchuk_port_t port;
    float output_norm; /* -1.0 to 1.0 */
    bool btn_c_pressed;
    bool btn_z_pressed;
    bool cruise_active;
    float cruise_value;
    float time_since_update_s;
    bool disconnected;
} nunchuk_app_t;

void nunchuk_construct(nunchuk_app_t *app, uint32_t module_id, uint32_t priority,
                       const nunchuk_config_t *config, const nunchuk_port_t *port);
edge_status_t nunchuk_init(nunchuk_app_t *app);
edge_status_t nunchuk_update(nunchuk_app_t *app, float dt);
float nunchuk_get_output(const nunchuk_app_t *app);
bool nunchuk_is_connected(const nunchuk_app_t *app);
edge_module_t *nunchuk_module(nunchuk_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* NUNCHUK_H */
