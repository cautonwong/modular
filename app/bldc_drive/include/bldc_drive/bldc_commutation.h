#ifndef BLDC_DRIVE_COMMUTATION_H
#define BLDC_DRIVE_COMMUTATION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The six-step layer's arithmetic, separated from the hardware it runs on so it can be checked
 * against the reference's own code the way the FOC core's was. Everything here is pure: a step
 * number, a hall reading, a speed. What is not here yet is the drive itself - the phase outputs,
 * the integrators and the start-up modes - and it is named in the phase table
 * (docs/bldc-migration.md, phase F) rather than implied.
 */

/* Reference datatypes.h:53-55, the sensor modes the six-step layer is gated on. */
#define BLDC_SENSOR_MODE_SENSORLESS 0u
#define BLDC_SENSOR_MODE_SENSORED 1u
#define BLDC_SENSOR_MODE_HYBRID 2u

/*
 * mcpwm.c:501-514, mcpwm_init_hall_table: the hall table the configuration carries is stored for
 * the forward direction, and the reverse is its own mapping - 1 to 6, 2 to 5, 3 to 4 and back -
 * because running the motor the other way visits the same halls in the other order. The reference
 * keeps both halves in one sixteen-entry array, the reverse half from index eight; here they are
 * two arrays because nothing else needs the layout.
 *
 * `hall_to_phase` is eight entries, one per hall reading, and a reading with no step (0 or 7, which
 * the reference treats as no reading at all) stays whatever it is: below one, and therefore not a
 * step.
 */
void bldc_build_hall_tables(const int8_t hall_to_phase[8], int8_t forward[8], int8_t reverse[8]);

/*
 * mcpwm.c:2597-2603, inside commutate(): the step advances and wraps within one to six. The
 * reference does it with two while loops, and so does this - a modulo would be the same arithmetic
 * for every step it is ever given, and keeping the loops keeps the arithmetic identical for the
 * ones it is not.
 */
int bldc_comm_step_advance(int comm_step, int steps);

/*
 * mcpwm.c:2558-2566, update_rpm_tacho(): the difference between two steps, taken modulo six and
 * then normalized to plus or minus three - a step that appears to have gone forward five has gone
 * back one. The reference accumulates the time between commutations beside it and divides for the
 * speed; that needs a clock, so the caller owns it and this returns the delta and the step to
 * remember.
 */
int bldc_tacho_step_delta(int comm_step, int last_step);

/*
 * mcpwm.c:2586-2595, update_sensor_mode(): the motor is run sensorless when it is configured that
 * way, and when it is configured hybrid once its speed is above hall_sl_erpm. The reference
 * compares against the absolute value of its own measured rpm.
 */
bool bldc_sensorless_now(uint8_t sensor_mode, float rpm, float hall_sl_erpm);

/*
 * mcpwm.c:2302-2307, mcpwm_read_hall_phase: the three hall pins as the reading the commutation is
 * driven by, bit zero first. Nothing is filtered here - a reading of nought or seven is a reading,
 * and the reference assigns it as the step at the site below (mcpwm.c:1940). Its own table builder
 * keeps that same value rather than inventing one, which is why bldc_build_hall_tables passes such
 * an entry through.
 */
uint8_t bldc_hall_phase(bool hall1, bool hall2, bool hall3);

/*
 * mcpwm.c:1939-1952, the hall branch of the ADC ISR: the reading *is* the step - it is assigned,
 * not counted towards - and only a running motor has it applied to the phases. A reading that has
 * not changed still applies once if nothing has commutated since the motor started, which is the
 * reference's own catch-up for a motor that began its run on the step it is already standing on.
 *
 * What the caller does with an apply is the reference's own pair of calls: set_next_comm_step() and
 * commutate(0), which is where the phase outputs and the tachometer live.
 */
typedef struct bldc_hall_commutation {
    int comm_step;     /* the step after the reading, which the reference assigns either way */
    bool step_changed; /* and so the tachometer sees a commutation */
    bool apply;        /* whether the phases are to be set to it */
} bldc_hall_commutation_t;

void bldc_hall_commutation(int comm_step, int hall_phase, bool running, bool has_commutated,
                           bldc_hall_commutation_t *out);

#ifdef __cplusplus
}
#endif

#endif /* BLDC_DRIVE_COMMUTATION_H */
