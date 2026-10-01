#ifndef MOTOR_ID_H
#define MOTOR_ID_H

#include <stdbool.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"
#include "edge/modules.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Motor parameter identification, after the reference's measurement procedures (motor/mcpwm_foc.c
 * from :1797). Those are blocking mills: they lock the interface, override the phase, ramp a
 * current up, sleep in milliseconds and read an accumulator the control loop fills. Here they are
 * state machines advanced by motor_id_step(), which keeps the caller's thread free and keeps the
 * timings the reference's - one millisecond of the procedure per accumulated millisecond - rather
 * than the caller's.
 *
 * Ported: the resistance measurement (mcpwm_foc_measure_resistance, :1797-1908). Not yet ported,
 * each for a named reason:
 *
 *   inductance  mcpwm_foc_measure_inductance switches the configuration into HFI mode and drives
 *               the HFI voltages (:1909-1935), so it waits on B2
 *   flux        the sensored variant, conf_general_measure_flux_linkage (:742), measures through
 *               the encoder and has no angle source on this board yet; the open-loop one is ported
 *   hall        the hall table procedure, likewise
 *
 * None of those three is approximated here. A measurement that returns a plausible number it did
 * not measure is worse than one that says it cannot measure it yet.
 */
typedef enum motor_id_state {
    MOTOR_ID_STATE_IDLE = 0,
    /* The reference's measure_resistance phases: ramp the current up, hold it, then sample. */
    MOTOR_ID_STATE_RAMP,
    MOTOR_ID_STATE_SETTLE,
    MOTOR_ID_STATE_SAMPLE,
    MOTOR_ID_STATE_COMPLETE,
    MOTOR_ID_STATE_FAILED,

    /*
     * conf_general_measure_flux_linkage_openloop's phases (conf_general.c:967-1316): a temporary
     * configuration, a current ramp at a standstill, a baseline of how much duty the motor draws
     * when it is not turning, then a spin-up watched until the duty reaches its target, the
     * average of what that took, and finally the same measurement with the phases off.
     */
    MOTOR_ID_STATE_FLUX_CONFIG,
    MOTOR_ID_STATE_FLUX_RAMP,
    MOTOR_ID_STATE_FLUX_BASELINE,
    MOTOR_ID_STATE_FLUX_SPINUP,
    MOTOR_ID_STATE_FLUX_SETTLE,
    MOTOR_ID_STATE_FLUX_UNDRIVEN,
    MOTOR_ID_STATE_FLUX_OBSERVER,
    MOTOR_ID_STATE_FLUX_STOP,
    MOTOR_ID_STATE_FLUX_BACK_EMF,

    /*
     * conf_general_measure_flux_linkage (conf_general.c:742-899), the sensored variant: install a
     * configuration that commutes from the rotor sensor, then four attempts to spin the motor up -
     * each of the last three releases it, loosens a start-up limit and drives again - and finally
     * two thousand milliseconds of averaging at the duty the caller asked for.
     */
    MOTOR_ID_STATE_SENSORED_CONFIG,
    MOTOR_ID_STATE_SENSORED_RELEASE,
    MOTOR_ID_STATE_SENSORED_SPINUP,
    MOTOR_ID_STATE_SENSORED_SAMPLE,

    /*
     * mcpwm_foc_measure_inductance (:1909-2070): the temporary HFI configuration, its two
     * one-millisecond waits, the wait for the first filled sample buffer (which gives up after a
     * hundred milliseconds and carries on anyway, :1943-1950), and then one pass per ten of the
     * requested samples - each of those zeroing the duty, checking the fault, waiting ten
     * milliseconds and reading the transform's bins (:1956-2030).
     */
    MOTOR_ID_STATE_IND_CONFIG,
    MOTOR_ID_STATE_IND_DUTY_ZERO,
    MOTOR_ID_STATE_IND_WAIT_READY,
    MOTOR_ID_STATE_IND_SAMPLE,
    MOTOR_ID_STATE_IND_SAMPLE_WAIT,
    MOTOR_ID_STATE_IND_SAMPLE_READ,
    /* mcpwm_foc_measure_res_ind's ten milliseconds between zeroing the current and measuring
     * inductance (:2350-2353). */
    MOTOR_ID_STATE_RES_IND_SETTLE
} motor_id_state_t;

/*
 * The two composed sequences, which the reference writes as nested calls and this port drives from
 * its completion step:
 *
 *   measure_inductance_current (:2086-2104) runs whole measurements at an increasing duty until the
 *   current they draw reaches the caller's goal, then one more at the last duty with the caller's
 *   sample count.
 *
 *   measure_res_ind (:2320-2360) measures resistance at an increasing current until the current
 *   exceeds 1/R, takes a final 200-sample resistance measurement at that current, stores it and
 *   then measures inductance at the same current.
 */
typedef enum motor_id_chain {
    MOTOR_ID_CHAIN_NONE = 0,
    MOTOR_ID_CHAIN_IND_CURRENT,
    MOTOR_ID_CHAIN_IND_FINAL,
    MOTOR_ID_CHAIN_RES_SCAN,
    MOTOR_ID_CHAIN_RES_FINAL
} motor_id_chain_t;

typedef struct motor_id_result {
    float r_ohm;
    /*
     * conf_general.c:1183-1191: the linkage measured while driving, the same measurement taken
     * afterwards with the phases off, and how many samples the second one had. The command layer
     * treats fewer than 61 of them as too few to trust, which is its rule rather than this
     * procedure's.
     */
    float flux_linkage_wb;
    float linkage_undriven_wb;
    float undriven_samples;
    /*
     * mcpwm_foc_measure_inductance (:2061-2069): the average of the two axis inductances and their
     * difference, both scaled by 1e6 into microhenrys and by the reference's 0.9 factor, and the
     * current it ran at. The factor is the reference's own note: the observer is more stable with
     * an underestimated inductance (:2055-2060).
     */
    float ind_uh;
    float ld_lq_diff_uh;
    float ind_current_a;
    bool valid;
} motor_id_result_t;

/*
 * What the measurement procedures need from the plant. Each callback is one thing the reference
 * reaches through mc_interface or the motor state directly, and the consumer defines how - the
 * product wires this to foc_core:
 *
 *   set_phase_override  m_phase_override + m_phase_now_override (:1803-1804)
 *   set_current         m_iq_set with m_id_set = 0 in CONTROL_MODE_CURRENT (:1805-1807)
 *   reset_samples       clearing motor->m_samples before sampling (:1841-1843)
 *   read_samples        the totals, and the count while waiting for them (:1846, :1869-1870)
 *   get_fault           mc_interface_get_fault(), the abort condition (:1821, :1851)
 *   stop                the cleanup, id/iq zeroed, override cleared, PWM stopped (:1874-1881)
 */
typedef struct motor_id_measure_port {
    void *self;
    edge_status_t (*set_phase_override)(void *self, float angle_rad, bool enable);
    edge_status_t (*set_current)(void *self, float iq);
    edge_status_t (*reset_samples)(void *self);
    edge_status_t (*read_samples)(void *self, float *i_sum, float *v_sum, uint32_t *count);
    uint32_t (*get_fault)(void *self);
    edge_status_t (*stop)(void *self);

    /*
     * Flux linkage (conf_general.c:967-1316). The temporary configuration is a save-and-restore
     * pair rather than a struct: the reference edits the live configuration in place and puts the
     * old one back, and this port's product does the same to the aggregate's own configuration
     * fields - which is also why an app never has to see another app's types.
     *
     *   enter_measurement_config  the temporary configuration (:1007-1016): sensorless, the current
     *                             gains from the supplied resistance and inductance, no decoupling
     *   leave_measurement_config  the restore every exit path does (:1204, and the fault paths)
     *   set_openloop_current      mcpwm_foc_set_openloop_current (:878), the drive this uses
     *   read_vdq / read_idq       the averages are taken over these (:1155-1162)
     *   read_duty                 |duty_cycle_now| (:1119, :1175)
     *   read_speed_rad_s          the electrical speed both formulae divide by (:1181, :1222)
     */
    edge_status_t (*enter_measurement_config)(void *self, float current_kp, float current_ki);
    edge_status_t (*leave_measurement_config)(void *self);
    edge_status_t (*set_openloop_current)(void *self, float current_a, float rpm);
    edge_status_t (*read_vdq)(void *self, float *v_d, float *v_q);
    edge_status_t (*read_idq)(void *self, float *i_d, float *i_q);
    edge_status_t (*read_duty)(void *self, float *duty_now);
    edge_status_t (*read_speed_rad_s)(void *self, float *rad_s);
    /*
     * The sensored variant (conf_general.c:742-899) needs four more things and one more answer:
     *
     *   read_vbus             GET_INPUT_VOLTAGE() multiplies the bus voltage by the duty (:877)
     *   release_motor         mc_interface_release_motor before each retry (:796-826)
     *   is_running            the wait_for_motor_release(1.0) that follows it
     *   set_startup_limits    the per-attempt sl_min_erpm / sl_cycle_int_limit / comm_mode values
     *
     * and entering the measurement has to say whether the configuration should commute from the
     * rotor sensor or sensorless. The sensored variant is a procedure of its own, so it has an
     * entry of its own rather than a flag on the other one's.
     */
    edge_status_t (*read_vbus)(void *self, float *v_bus);
    edge_status_t (*read_rpm)(void *self, float *rpm);
    edge_status_t (*release_motor)(void *self);
    edge_status_t (*is_running)(void *self, bool *running);
    edge_status_t (*set_startup_limits)(void *self, float sl_min_erpm, float sl_cycle_int_limit,
                                        bool delay_comm_mode);
    edge_status_t (*enter_sensored_measurement_config)(void *self);

    /*
     * Inductance (mcpwm_foc_measure_inductance, :1909-2070). The temporary configuration is the
     * same save-and-restore pair the flux procedure's is, and what the procedure reads are the HFI
     * transform's own outputs rather than raw plant quantities:
     *
     *   enter_inductance_config  the temporary configuration (:1918-1935): the HFI sensor and
     *                            ambiguity modes, the three voltages computed from the caller's
     * duty against the bus voltage, the speed and sampling-mode overrides and the
     * switching-frequency clamp leave_inductance_config  the restore every exit path does
     * (:2038-2052) set_duty                 mcpwm_foc_set_duty(0.0), how the reference holds the
     * motor still so that the injection is the only voltage on it (:1940, :1960) is_hfi_ready the
     * `while (!m_hfi.ready)` wait (:1943-1950) read_hfi_bins            bin 0 of the sample buffer
     * - the mean of the inverse inductance - bin 2 of it - the saliency's second harmonic - and bin
     * 0 of the current-step buffer, the mean measured step (:2004-2007)
     */
    edge_status_t (*enter_inductance_config)(void *self, float duty);
    edge_status_t (*leave_inductance_config)(void *self);
    edge_status_t (*set_duty)(void *self, float duty);
    edge_status_t (*is_hfi_ready)(void *self, bool *ready);
    edge_status_t (*read_hfi_bins)(void *self, float *offset, float *real_bin2, float *imag_bin2,
                                   float *current_mean);
    /*
     * mcpwm_foc_measure_res_ind's own temporary configuration (:2322-2326 and its exit at :2357):
     * the current loop's gains become very small ones for the scan, which is what lets the applied
     * voltage be read as the resistance's own drop rather than as a controller's output. The
     * reference edits the live configuration and restores it; the product does the same to the
     * aggregate's, which is why this is a pair rather than a value.
     */
    edge_status_t (*enter_res_ind_gains)(void *self);
    edge_status_t (*leave_res_ind_gains)(void *self);
} motor_id_measure_port_t;

typedef struct motor_id_app {
    edge_module_t module;
    motor_id_state_t state;
    motor_id_measure_port_t measure_port;
    motor_id_result_t result;

    /* The procedure's arguments, and the reference's stop_after flag: its composed R-then-L
     * sequence leaves the motor running between the two passes, so only the last one stops it. */
    float target_current_a;
    uint32_t target_samples;
    bool stop_after;

    /* The consumer's fault indication, zero for none: the procedures read it through get_fault()
     * and only ever compare it against none, so the product hands over the FOC's fault bits
     * where the reference would hand over its fault_code enum. */
    uint32_t fault_code;

    /* The millisecond clock the procedure runs on, plus the counters the phases need: the settle
     * wait, the sample-wait timeout, and the current that is being ramped. */
    float ms_accum;
    uint32_t ms;
    float ramp_current_a;

    /* The flux-linkage procedure's own state: its arguments, the configuration it computes from
     * them, and the accumulators its phases fill. */
    float flux_current_a;
    float flux_duty_target;
    float flux_erpm_per_sec;
    float flux_res_ohm;
    float flux_ind_h;
    float flux_duty_still;
    float flux_duty_max;
    float flux_rpm_now;
    uint32_t flux_cnt_ms;
    float flux_vd_sum;
    float flux_vq_sum;
    float flux_id_sum;
    float flux_iq_sum;
    float flux_samples;
    float flux_linkage_sum;
    float flux_linkage_samples;
    /* The reference writes -1, -2 or -3 into the linkage on its three failed exits (:1125,:1133,
     * :1140) and zero otherwise; this keeps that number rather than folding it into valid. */
    float flux_fail_reason;

    /* The sensored variant's own state: its arguments, which attempt it is on, and the three sums
     * its averaging phase fills. */
    float sensored_current_a;
    float sensored_duty;
    float sensored_min_erpm;
    float sensored_res_ohm;
    uint32_t sensored_pass;
    bool sensored_switch_done;
    float sensored_avg_voltage;
    float sensored_avg_rpm;
    float sensored_avg_current;
    float sensored_samples;

    /*
     * The inductance procedure's own state (:1909-2070): its arguments, the sums its sampling
     * passes fill, and the two waits' counters.
     */
    float ind_duty;
    float ind_current_a;
    uint32_t ind_samples;
    uint32_t ind_iterations;
    uint32_t ind_waited_ms;
    float ind_l_sum;
    float ind_diff_sum;
    float ind_i_sum;

    /*
     * The composed sequences' progress: which chain is running, the current goal the inductance
     * scan is aiming for, the duty it has reached, and the resistance chain's own scan state.
     */
    motor_id_chain_t chain;
    uint32_t chain_samples;
    float ind_goal_current_a;
    float ind_scan_duty;
    float res_ind_current_a;
    float res_ind_r_tmp;
    float res_ind_current_max_a;
    float res_ind_last_current_a;
    /* Whether the composed sequence's own current-loop gains are still in place, so that the single
     * exit point puts them back once and only when it changed them. */
    bool res_ind_gains_active;
} motor_id_app_t;

void motor_id_construct(motor_id_app_t *app, uint32_t module_id, uint32_t priority,
                        const motor_id_measure_port_t *measure_port);
edge_status_t motor_id_init(motor_id_app_t *app);

/*
 * The reference's mcpwm_foc_measure_resistance arguments: the current to hold, how many control
 * cycles to average over, and whether to stop the motor at the end. The result lands in
 * motor_id_get_result() when the state reaches MOTOR_ID_STATE_COMPLETE, and the procedure can
 * still fail on a fault, which motor_id_get_fault() reports.
 */
edge_status_t motor_id_measure_resistance(motor_id_app_t *app, float current_a, uint32_t samples,
                                          bool stop_after);

/* Not ported yet; the note above names what each one waits on. */
/*
 * conf_general_measure_flux_linkage_openloop (conf_general.c:967-1316). The arguments are the
 * reference's: the current to drive with, the duty to spin up to, how fast to ramp the speed, and
 * the resistance and inductance to compute the current gains from - zero for either means "take it
 * from the configuration", which this procedure is given rather than finding itself.
 *
 * The result lands in motor_id_get_result() when the state reaches MOTOR_ID_STATE_COMPLETE; a
 * failed exit leaves the negative reason in it and reports not valid.
 */
edge_status_t motor_id_measure_flux_linkage_openloop(motor_id_app_t *app, float current_a,
                                                     float duty, float erpm_per_sec, float res_ohm,
                                                     float ind_h, float config_res_ohm,
                                                     float config_ind_h, float config_duty_max);

/*
 * conf_general_measure_flux_linkage (conf_general.c:742-899): the same measurement with the motor
 * commutated from its rotor sensor instead of open loop. It returns the linkage through the same
 * result struct - the driven linkage is the only number it produces - and reports success through
 * the state, so a run that could not spin the motor up ends in MOTOR_ID_STATE_FAILED with nothing
 * valid in the result.
 */
edge_status_t motor_id_measure_flux_linkage_sensored(motor_id_app_t *app, float current_a,
                                                     float duty, float min_erpm, float res_ohm,
                                                     float config_res_ohm);

edge_status_t motor_id_measure_inductance(motor_id_app_t *app, float duty, uint32_t samples);
edge_status_t motor_id_measure_inductance_current(motor_id_app_t *app, float curr_goal,
                                                  uint32_t samples);

/*
 * The composed pair, mcpwm_foc_measure_res_ind (:2320-2360): a resistance measurement at an
 * increasing current until the current exceeds 1/R, a final one at that current, and then an
 * inductance measurement there. Ported only once its inductance half was.
 */
edge_status_t motor_id_measure_r_l(motor_id_app_t *app, float current_max_a);
edge_status_t motor_id_measure_flux_linkage(motor_id_app_t *app);
edge_status_t motor_id_detect_hall(motor_id_app_t *app);

edge_status_t motor_id_step(motor_id_app_t *app, float dt);
const motor_id_result_t *motor_id_get_result(const motor_id_app_t *app);
uint32_t motor_id_get_fault(const motor_id_app_t *app);
edge_module_t *motor_id_module(motor_id_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_ID_H */
