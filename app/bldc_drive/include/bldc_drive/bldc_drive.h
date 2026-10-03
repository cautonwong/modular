#ifndef BLDC_DRIVE_H
#define BLDC_DRIVE_H

#include <stdbool.h>
#include <stdint.h>

#include "bldc_drive/bldc_commutation.h"
#include "edge/errors.h"
#include "edge/module.h"
#include "edge/modules.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The six-step drive, phase F. What is here so far is the part that can be checked without
 * hardware: the configuration's hall table turned into the two directions' tables at init, and the
 * sensor mode the drive is running in, recomputed every cycle from the speed the product hands in -
 * both the reference's own arithmetic (motor/mcpwm.c:501 and :2586), both against its output.
 *
 * What is not here is named rather than implied: the hall input port, the phase outputs
 * (set_next_comm_step), the commutation itself with its start-up modes, the cycle integrator the
 * current control needs and the hall-table detection that COMM_DETECT_MOTOR_PARAM runs. They arrive
 * in the slices that follow, and the phase table in docs/bldc-migration.md carries them.
 */
typedef struct bldc_drive_config {
    uint8_t sensor_mode; /* mcconf sensor_mode: SENSORLESS / SENSORED / HYBRID */
    float hall_sl_erpm;  /* mcconf hall_sl_erpm: where hybrid hands over to sensorless */
    int8_t hall_table[8];
} bldc_drive_config_t;

typedef struct bldc_drive {
    edge_module_t module;
    bldc_drive_config_t config;

    /* The configuration's table in both directions (mcpwm.c:501). */
    int8_t hall_forward[8];
    int8_t hall_reverse[8];

    /* The commutation state the reference keeps in globals of its own: the step, the one the
     * tachometer last saw, and the mode decision. */
    int comm_step;
    int last_step;
    float rpm;

    /*
     * mcpwm.c:2546's sensorless_now. The reference computes it in update_sensor_mode() from the
     * configuration and its own measured speed; here the product hands the speed in, because the
     * aggregate that measures it is the product's to read.
     */
    bool sensorless_now;
} bldc_drive_t;

void bldc_drive_construct(bldc_drive_t *self, uint32_t module_id, uint32_t priority,
                          const bldc_drive_config_t *config);
edge_status_t bldc_drive_init(bldc_drive_t *self);

/* The speed the sensor-mode decision is made on, in electrical rpm as the reference's own. */
void bldc_drive_set_rpm(bldc_drive_t *self, float rpm);

/* The step the drive is commutating at, one to six, and how it last moved. */
void bldc_drive_advance_step(bldc_drive_t *self, int steps);
int bldc_drive_get_comm_step(const bldc_drive_t *self);
int bldc_drive_get_tacho_delta(bldc_drive_t *self);

bool bldc_drive_is_sensorless(const bldc_drive_t *self);
const int8_t *bldc_drive_hall_forward(const bldc_drive_t *self);
const int8_t *bldc_drive_hall_reverse(const bldc_drive_t *self);

edge_module_t *bldc_drive_module(bldc_drive_t *self);

#ifdef __cplusplus
}
#endif

#endif /* BLDC_DRIVE_H */
