#ifndef PPM_H
#define PPM_H

#include <stdbool.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"
#include "edge/modules.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ppm_control_mode {
    PPM_MODE_OFF = 0,
    PPM_MODE_DUTY,
    PPM_MODE_CURRENT,
    PPM_MODE_CURRENT_NOREV,
    PPM_MODE_CURRENT_NOREV_BRAKE,
    PPM_MODE_RPM,
    PPM_MODE_POS
} ppm_control_mode_t;

typedef struct ppm_config {
    ppm_control_mode_t mode;
    float pulse_min_us;
    float pulse_max_us;
    float pulse_center_us;
    float pulse_deadband_us;
    float timeout_s;
    bool safe_start;
} ppm_config_t;

typedef struct ppm_receiver_port {
    void *self;
    edge_status_t (*read_pulse_us)(void *self, float *pulse_us);
    bool (*is_signal_present)(void *self);
} ppm_receiver_port_t;

typedef struct ppm_app {
    edge_module_t module;
    ppm_config_t config;
    ppm_receiver_port_t receiver_port;
    float last_pulse_us;
    float time_since_last_pulse_s;
    float output_norm; /* -1.0 to 1.0 */
    bool safe_start_unlocked;
    bool signal_lost;
} ppm_app_t;

void ppm_construct(ppm_app_t *app, uint32_t module_id, uint32_t priority,
                   const ppm_config_t *config, const ppm_receiver_port_t *receiver_port);
edge_status_t ppm_init(ppm_app_t *app);
edge_status_t ppm_update(ppm_app_t *app, float dt);
float ppm_get_output(const ppm_app_t *app);
bool ppm_is_safe(const ppm_app_t *app);
edge_module_t *ppm_module(ppm_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* PPM_H */
