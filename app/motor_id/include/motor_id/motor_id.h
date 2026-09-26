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
 *   flux        conf_general_measure_flux_linkage spins the motor up in open loop and watches the
 *               current fall; a procedure of its own, not read yet
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
    MOTOR_ID_STATE_FAILED
} motor_id_state_t;

typedef struct motor_id_result {
    float r_ohm;
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
edge_status_t motor_id_measure_r_l(motor_id_app_t *app);
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
