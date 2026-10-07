#ifndef PAS_H
#define PAS_H

#include <stdbool.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"
#include "edge/modules.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * datatypes.h:685-695's values, in its own order, under names this application owns -
 * motor_config's vesc_enums.h emits the reference's own names, and a consumer that includes both
 * must not see two declarations of one enumerator. This is the same call the PPM and FOC
 * applications make (D30: an app may not depend on another, so it owns the name it answers to).
 */
typedef enum pas_mode {
    PAS_MODE_NONE = 0,
    PAS_MODE_CADENCE,
    PAS_MODE_TORQUE,
    PAS_MODE_TORQUE_WITH_CADENCE_TIMEOUT,
} pas_control_type_t;

typedef enum pas_sensor_kind {
    PAS_SENSOR_QUADRATURE = 0,
} pas_sensor_type_t;

/* datatypes.h:750-764, the reference's own fields and their types. */
typedef struct pas_config {
    pas_control_type_t ctrl_type;
    pas_sensor_type_t sensor_type;
    float current_scaling;
    float pedal_rpm_start;
    float pedal_rpm_end;
    bool invert_pedal_direction;
    uint8_t magnets;
    bool use_filter;
    float ramp_time_pos;
    float ramp_time_neg;
    uint32_t update_rate_hz;
} pas_config_t;

/*
 * applications/app_pas.c:136-138 reads the two pad levels, :156 reads the free-running clock for
 * its period, and :243 reads the torque sensor as a ratio rather than a torque: hw_get_PAS_torque
 * answers a fraction of that sensor's range, which the application then multiplies by its own
 * scaling.
 */
typedef struct pas_port {
    void *self;
    edge_status_t (*read_levels)(void *self, uint8_t *pas1, uint8_t *pas2);
    edge_status_t (*read_torque_ratio)(void *self, float *ratio);
    edge_status_t (*now_seconds)(void *self, float *seconds);
} pas_port_t;

/*
 * applications/app_pas.c:48-67's private state, plus the three values it hands out: the quadrature
 * decoder's own memory, the period filter, the idle timer, the ramp, and the safe start's counter.
 */
typedef struct pas_app {
    edge_module_t module;
    pas_config_t config;
    pas_port_t port;

    /* The decoder (app_pas.c:133-142). */
    uint8_t old_state;
    float old_timestamp;
    float inactivity_time;
    float period_filtered;
    int32_t correct_direction_counter;

    /* The two idle timers of the torque modes (app_pas.c:249-268) and the ramp (app_pas.c:275-287).
     */
    float ms_without_cadence_or_torque;
    float ms_without_cadence;
    float output_ramp;

    /* The safe start (app_pas.c:294-304): how long the output has been at zero, and the reading it
     * is compared with. The reference compares a float against an int, which is its own quirk. */
    float ms_without_power;
    int pulses_without_power_before;

    /* app_pas.c:51-59. */
    float sub_scaling;
    float output_current_rel;
    float pedal_rpm;
    float torque_ratio;
    float max_pulse_period;
    float min_pedal_period;
    float direction_conf;
    bool primary_output;
    /* app_pas.c:187-189: the product tells this when the motor has raised a fault, which is where
     * the reference restarts the safe start's clock. */
    bool fault;
    bool active;
} pas_app_t;

void pas_construct(pas_app_t *app, uint32_t module_id, uint32_t priority,
                   const pas_config_t *config, const pas_port_t *port);
edge_status_t pas_init(pas_app_t *app);

/* applications/app_pas.c:126-177, pas_event_handler: the quadrature decoder, called once a period.
 */
void pas_event_handler(pas_app_t *app);

/* applications/app_pas.c:178-317, the thread body's work, once per period of update_rate_hz. */
edge_status_t pas_update(pas_app_t *app, float dt);

float pas_get_current_target_rel(const pas_app_t *app);
float pas_get_pedal_rpm(const pas_app_t *app);
void pas_set_current_sub_scaling(pas_app_t *app, float current_sub_scaling);
void pas_set_primary_output(pas_app_t *app, bool primary_output);
void pas_set_fault(pas_app_t *app, bool fault);
bool pas_is_primary_output(const pas_app_t *app);
bool pas_is_active(const pas_app_t *app);
edge_module_t *pas_module(pas_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* PAS_H */
