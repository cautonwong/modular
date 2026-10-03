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

/*
 * What the drive needs from the world, in the two directions the reference reads and writes itself:
 * the three hall pins, which it reads in mcpwm_read_hall_phase (mcpwm.c:2302), and the bridge's
 * phases, which set_next_comm_step writes (:1104-1170). Both are optional, and a drive told to
 * commutate without them says so rather than driving blind - a product with no halls has no
 * six-step motor on it.
 */
typedef struct bldc_hall_port {
    void *self;
    /* The three pins as one reading, bit zero first, exactly as the reference reads them. */
    uint8_t (*read_hall)(void *self);
} bldc_hall_port_t;

typedef struct bldc_phase_port {
    void *self;
    /* Hold the bridge at the step: one phase high, one low, one left floating, which is what the
     * reference's own step table does. */
    edge_status_t (*apply_step)(void *self, int comm_step);
} bldc_phase_port_t;

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

    /*
     * The two ports, and the reference's own has_commutated global (mcpwm.c:2622), which is what
     * its catch-up branch reads when a reading has not changed since the run began.
     */
    const bldc_hall_port_t *hall;
    const bldc_phase_port_t *phase;
    bool has_commutated;

    /*
     * The commanded direction, which is the reference's own sense of the word: set from the sign of
     * the duty it is driving with (mcpwm.c:1024-1028), and it is what selects the table half a hall
     * reading is looked up in.
     */
    int direction;

    /*
     * The hall-detection samples (mcpwm.c:92), which its sensorless branch counts and
     * COMM_DETECT_MOTOR_PARAM reads back as the configuration's own table.
     */
    bldc_hall_detect_counts_t hall_detect_counts;
} bldc_drive_t;

void bldc_drive_construct(bldc_drive_t *self, uint32_t module_id, uint32_t priority,
                          const bldc_drive_config_t *config);
edge_status_t bldc_drive_init(bldc_drive_t *self);

/* The speed the sensor-mode decision is made on, in electrical rpm as the reference's own. */
void bldc_drive_set_rpm(bldc_drive_t *self, float rpm);

void bldc_drive_set_hall_port(bldc_drive_t *self, const bldc_hall_port_t *port);
void bldc_drive_set_phase_port(bldc_drive_t *self, const bldc_phase_port_t *port);

/* The commanded direction, one for forwards and nought for backwards, as mcpwm.c:1024-1028 sets it.
 */
void bldc_drive_set_direction(bldc_drive_t *self, int direction);
int bldc_drive_get_direction(const bldc_drive_t *self);

/* mcpwm.c:2242 and :2257: the detection table, reset before a run and read back after one. */
void bldc_drive_hall_detect_reset(bldc_drive_t *self);
void bldc_drive_hall_detect_sample(bldc_drive_t *self, bool in_first_half);
int bldc_drive_hall_detect_result(bldc_drive_t *self, bool hall_sensor_port, int8_t out[8]);
const bldc_hall_detect_counts_t *bldc_drive_hall_detect_counts(const bldc_drive_t *self);

/*
 * One hall-driven commutation, which is mcpwm.c:1939-1952's branch: the reading is taken, the
 * decision the pure function above makes is applied to the state, and a step the caller is to apply
 * goes to the bridge - all of it only while the motor is running, as there.
 *
 * EDGE_OK when it ran, EDGE_ENOTSUP when the drive has no hall port to read, and the phase port's
 * own status if applying the step failed.
 */
edge_status_t bldc_drive_commutate_hall(bldc_drive_t *self, bool running);

/* Whether a step has been applied to the bridge since the drive was initialised (:2622). */
bool bldc_drive_has_commutated(const bldc_drive_t *self);

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
