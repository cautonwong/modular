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

/*
 * mcpwm.c:2302-2304, mcpwm_read_hall_phase: the reading looked up in the half of the table the
 * commanded direction selects - from index zero for one direction and from index eight for the
 * other, which is one sixteen-entry array there and the two halves the table builder makes here.
 * The direction is the reference's own sense of the word: it is set from the commanded duty's sign
 * (mcpwm.c:1024-1028), so a motor driven backwards reads the other half.
 */
int bldc_hall_phase_from_table(const int8_t forward[8], const int8_t reverse[8], uint8_t reading,
                               int direction);

/*
 * The hall-detection table (mcpwm.c:92, :1874-1877, :2242, :2257-2299): eight readings by seven
 * steps, counted while the motor is run sensorless, and read back as the table the configuration
 * carries - one step per reading, or minus one where the reading never named one.
 *
 * The counting gate is the reference's own: a sample is collected only while the phase difference
 * is below fifty counts, which is the first half of a commutation cycle, positive timing being
 * better than negative when the two are misaligned (mcpwm.c:1872-1875). That comparison is against
 * raw counts, so it belongs to whatever reads the phases rather than here; the caller says whether
 * the sample came from that half.
 */
typedef int bldc_hall_detect_counts_t[8][7];

/* mcpwm.c:2266-2268: a reading names a step only when more than fifteen samples say so. */
#define BLDC_HALL_DETECT_MIN_SAMPLES 15

void bldc_hall_detect_reset(bldc_hall_detect_counts_t counts);
void bldc_hall_detect_sample(bldc_hall_detect_counts_t counts, uint8_t reading, int comm_step,
                             bool in_first_half);

/*
 * mcpwm.c:2257-2299's three answers: zero for a table where every reading but two names a step and
 * six different steps are named in all, minus one for anything else, and minus three when the
 * sensor port is not a hall one at all - which the caller knows about its own hardware and says
 * here.
 */
int bldc_hall_detect_result(bldc_hall_detect_counts_t counts, bool hall_sensor_port, int8_t out[8]);

/*
 * mcpwm.c:1322-1341, the limits the sensorless start-up is commutated by. It is the reference's own
 * arithmetic with its own clamps: the running limit grows with the bus voltage over whichever is
 * larger of the speed and sl_min_erpm, is advanced towards the braking value as the speed rises,
 * and is held between one and the ceiling sl_bemf_coupling_k sets with sl_min_erpm_cycle_int_limit.
 * The two commutation-time figures are the switching frequency over six commutations per
 * revolution.
 *
 * The speed it is given is already the reference's own low-passed value; the filter is state, so it
 * belongs to whatever runs the thread. At a standstill comm_time_sum is infinite, as it is there:
 * that is a division by zero the reference does not guard.
 */
typedef struct bldc_rpm_dep_params {
    float sl_cycle_int_limit;          /* mcconf sl_cycle_int_limit */
    float sl_bemf_coupling_k;          /* mcconf sl_bemf_coupling_k */
    float sl_min_erpm;                 /* mcconf sl_min_erpm */
    float sl_cycle_int_rpm_br;         /* mcconf sl_cycle_int_rpm_br */
    float sl_phase_advance_at_br;      /* mcconf sl_phase_advance_at_br */
    float sl_min_erpm_cycle_int_limit; /* mcconf sl_min_erpm_cycle_int_limit */
    float m_bldc_f_sw_max;             /* mcconf m_bldc_f_sw_max */
} bldc_rpm_dep_params_t;

typedef struct bldc_rpm_dep {
    float cycle_int_limit;
    float cycle_int_limit_running;
    float cycle_int_limit_max;
    float comm_time_sum;
    float comm_time_sum_min_rpm;
} bldc_rpm_dep_t;

void bldc_rpm_dep_calc(const bldc_rpm_dep_params_t *params, float rpm_abs, float v_in,
                       bldc_rpm_dep_t *out);

/*
 * mcpwm.c:1886-1901, the decision to let one measured phase difference into the cycle integrator.
 * Its three conditions are the reference's: the commutation cycle is in its first half, or nothing
 * has commutated since the run began, or the phase being measured is inside the band the supply
 * voltage and the duty leave around half of it - that band's own two limits are computed here from
 * the same expression, including the clamp to a quarter of the supply.
 *
 * The phase difference arrives already taken to zero when it is smaller than ten counts
 * (mcpwm.c:1880-1882): that gate is against raw readings, so it belongs to whatever reads the
 * phases.
 */
bool bldc_cycle_integrator_adds(float v_diff, float pwm_cycles_sum, float last_pwm_cycles_sum,
                                bool has_commutated, float ph_now_raw, float duty, float v_in);

/* Reference datatypes.h, the two commutation modes the six-step layer starts a motor in. */
#define BLDC_COMM_MODE_INTEGRATE 0u
#define BLDC_COMM_MODE_DELAY 1u

/*
 * The integrator's own state, which the reference keeps in globals of its sensorless branch and in
 * a static inside the decision: the integral of the phase difference, the two figures the DELAY
 * mode accumulates for the cycle-integrator measurement the detection reads back, and the cycle sum
 * that mode compares against its threshold.
 */
typedef struct bldc_comm_state {
    float cycle_integrator;
    float cycle_integrator_sum;
    float cycle_integrator_iterations;
    float cycle_sum;
} bldc_comm_state_t;

/*
 * One cycle of the sensorless branch (mcpwm.c:1886-1930), with its inputs gathered rather than
 * listed: what the phases measured, where the commutation cycle is, and the machine's own figures.
 */
typedef struct bldc_comm_input {
    float v_diff;              /* the measured phase difference, already taken to zero under ten */
    float pwm_cycles_sum;      /* the commutation cycle's own progress, in switching periods */
    float last_pwm_cycles_sum; /* the same figure as the cycle before it */
    float ph_now_raw;          /* the phase being measured, an ADC count */
    float duty;                /* the duty being driven with */
    float v_in;                /* the supply, in the same counts */
    float rpm_abs;             /* the speed, unsigned */
    float switching_frequency_now;
    float vdiv_corr; /* the board's divider correction, VDIV_CORR in conf_general.h:119 */
    uint8_t comm_mode;
    bool has_commutated;
} bldc_comm_input_t;

/*
 * The decision, in the reference's own order: what the integrator's gate let in is accumulated, and
 * then the mode decides whether that integral is enough to commutate - INTEGRATE against the speed
 * -dependent limits (mcpwm.c:1902-1913) or DELAY against a threshold derived from the commutation
 * time (1914-1929), where the delay also folds the integral into the two figures the detection
 * reads back.
 *
 * Returns whether to commutate, which the reference does by calling commutate(1) and is the
 * caller's to do here. The state it was given is updated exactly as the reference updates its own,
 * including the resets on a commutation and the ones a negative measurement forces in the DELAY
 * mode.
 */
bool bldc_comm_sensorless_step(const bldc_comm_input_t *in, const bldc_rpm_dep_params_t *params,
                               const bldc_rpm_dep_t *dep, bldc_comm_state_t *state);

/*
 * mcpwm_foc.c:2464-2474, the tail of mcpwm_foc_hall_detect: what a hall reading's angle is, from
 * the sums that procedure accumulated while it swept the electrical angle over the motor three
 * times each way. A reading that was seen more than thirty times names an angle, taken as the
 * arctangent of its sums and normalized into nought to three hundred and sixty degrees, then scaled
 * to the two hundred counts a hall table entry holds; one that was not seen enough names nothing,
 * two hundred and fifty-five. The result is the reference's own: a detection passes when exactly
 * two readings were short, which is what the two ends of a six-step rotation look like.
 *
 * Returns how many readings were short, which is the reference's own local and its caller's
 * verdict.
 */
int bldc_hall_angle_table(const float sin_hall[8], const float cos_hall[8],
                          const int hall_iterations[8], uint8_t table[8], bool *result);

#ifdef __cplusplus
}
#endif

#endif /* BLDC_DRIVE_COMMUTATION_H */
