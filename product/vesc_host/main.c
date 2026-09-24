#include "adc_input/adc_input.h"
#include "bldc/sys.h"
#include "edge/clock.h"
#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"
#include "example/board.h"
#include "foc_core/foc_core.h"
#include "glue.h"
#include "motor_config/motor_config.h"
#include "motor_id/motor_id.h"
#include "ppm/ppm.h"
#include "throttle/throttle.h"
#include "timeout_guard/timeout_guard.h"
#include "vesc_can/vesc_can.h"
#include "vesc_comm/vesc_comm.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    vesc_host_glue_state_t glue_state;
    memset(&glue_state, 0, sizeof(glue_state));
    glue_state.v_bus = 24.0f;
    glue_state.ppm_pulse_us = 1500.0f;
    glue_state.adc_throttle_v = 1.0f;

    /* Initialize virtual motor */
    foc_virtual_motor_init(&glue_state.vmotor, 0.015f, 0.000020f, 0.005f, 7, 0.0001f);

    /* Construct Ports */
    motor_config_storage_port_t storage_port;
    vesc_host_make_storage_port(&storage_port, &glue_state);

    edge_stream_tx_port_t stream_tx_port;
    vesc_host_make_stream_tx_port(&stream_tx_port, &glue_state);

    foc_inverter_port_t inverter_port;
    vesc_host_make_inverter_port(&inverter_port, &glue_state);

    foc_current_port_t current_port;
    vesc_host_make_current_port(&current_port, &glue_state);

    foc_rotor_port_t rotor_port;
    vesc_host_make_rotor_port(&rotor_port, &glue_state);

    ppm_receiver_port_t ppm_port;
    vesc_host_make_ppm_port(&ppm_port, &glue_state);

    adc_input_port_t adc_port;
    vesc_host_make_adc_port(&adc_port, &glue_state);

    vesc_can_port_t can_port;
    vesc_host_make_can_port(&can_port, &glue_state);

    motor_id_measure_port_t id_m_port;
    vesc_host_make_motor_id_measure_port(&id_m_port, &glue_state);

    motor_id_control_port_t id_c_port;
    vesc_host_make_motor_id_control_port(&id_c_port, &glue_state);

    /* Construct Apps */
    motor_config_t motor_cfg;
    motor_config_construct(&motor_cfg, EDGE_MOD_MOTOR_CONFIG, 30u, &storage_port, 0x00u);
    if (motor_config_init(&motor_cfg) < 0) {
        return 10;
    }

    const mc_configuration_t *mc = motor_config_get_mc(&motor_cfg);
    foc_config_t foc_cfg = {
        .r_ohm = mc->foc_motor_r,
        .l_henry = mc->foc_motor_l,
        .lambda_wb = mc->foc_motor_flux_linkage,
        .pole_pairs = 7,
        .current_max_a = mc->current_max,
        .current_min_a = mc->current_min,
        .duty_max = 0.95f,
        .current_kp = mc->foc_current_kp,
        .current_ki = mc->foc_current_ki,
        .vbus_ov_threshold = mc->v_in_max,
        .vbus_uv_threshold = mc->v_in_min,
        .temp_fet_max_c = mc->temp_fet_max,
        .sensorless_mode = false,
        .observer_gamma = mc->foc_observer_gain,
    };

    foc_core_t foc;
    foc_core_construct(&foc, EDGE_MOD_FOC_CORE, 10u, &foc_cfg, &inverter_port, &current_port,
                       &rotor_port);
    if (foc_core_init(&foc) < 0) {
        return 11;
    }
    glue_state.foc = &foc;

    vesc_motor_provider_port_t motor_port;
    vesc_host_make_motor_provider_port(&motor_port, &foc);

    vesc_comm_t comm;
    vesc_comm_construct(&comm, EDGE_MOD_VESC_COMM, 20u, &stream_tx_port, &motor_port);
    if (vesc_comm_init(&comm) < 0) {
        return 12;
    }

    timeout_guard_t guard;
    timeout_guard_construct(&guard, EDGE_MOD_TIMEOUT_GUARD, 5u, NULL, 500u, 5.0f, 1000u);
    if (timeout_guard_init(&guard) != EDGE_OK) {
        return 13;
    }

    throttle_t throttle;
    throttle_construct(&throttle, EDGE_MOD_THROTTLE, 15u, NULL, NULL, NULL, 0.02f);
    if (throttle_init(&throttle) != EDGE_OK) {
        return 14;
    }

    ppm_app_t ppm;
    ppm_config_t ppm_cfg = {
        .mode = PPM_MODE_CURRENT,
        .pulse_min_us = 1000.0f,
        .pulse_max_us = 2000.0f,
        .pulse_center_us = 1500.0f,
        .pulse_deadband_us = 50.0f,
        .timeout_s = 0.2f,
        .safe_start = false,
    };
    ppm_construct(&ppm, EDGE_MOD_PPM, 25u, &ppm_cfg, &ppm_port);
    if (ppm_init(&ppm) != EDGE_OK) {
        return 15;
    }

    adc_input_app_t adc_app;
    adc_input_config_t adc_cfg = {
        .mode = ADC_MODE_CURRENT,
        .voltage_min = 0.2f,
        .voltage_max = 3.2f,
        .voltage_start = 0.8f,
        .voltage_end = 2.8f,
        .safe_start = false,
    };
    adc_input_construct(&adc_app, EDGE_MOD_ADC_INPUT, 25u, &adc_cfg, &adc_port);
    if (adc_input_init(&adc_app) != EDGE_OK) {
        return 16;
    }

    vesc_can_app_t can_app;
    vesc_can_config_t can_cfg = {
        .controller_id = 1,
        .baudrate = 500000,
        .status_rate_hz = 50.0f,
    };
    vesc_can_construct(&can_app, EDGE_MOD_VESC_CAN, 20u, &can_cfg, &can_port);
    if (vesc_can_init(&can_app) != EDGE_OK) {
        return 17;
    }

    motor_id_app_t motor_id;
    motor_id_config_t id_cfg = {
        .max_current = 10.0f,
        .samples_r = 50,
        .samples_l = 50,
    };
    motor_id_construct(&motor_id, EDGE_MOD_MOTOR_ID, 40u, &id_cfg, &id_m_port, &id_c_port);
    if (motor_id_init(&motor_id) != EDGE_OK) {
        return 18;
    }

    /* Assemble App List */
    edge_module_t *apps[9];
    apps[0] = foc_core_module(&foc);
    apps[1] = vesc_comm_module(&comm);
    apps[2] = motor_config_module(&motor_cfg);
    apps[3] = timeout_guard_module(&guard);
    apps[4] = throttle_module(&throttle);
    apps[5] = ppm_module(&ppm);
    apps[6] = adc_input_module(&adc_app);
    apps[7] = vesc_can_module(&can_app);
    apps[8] = motor_id_module(&motor_id);

    /* System & Event Infrastructure */
    edge_event_t event_storage[16];
    edge_event_queue_t event_queue;
    edge_event_queue_init(&event_queue, event_storage, 16);

    edge_sys_subscription_t subs[16];
    edge_sys_t sys;

    if (sys_bldc_init(&sys, apps, 9, &event_queue, subs, 16) != EDGE_OK) {
        return 1;
    }

    foc_core_set_current(&foc, 5.0f, 0.0f);

    /* Run 1000 fast-loop FOC and system cycles */
    for (int i = 0; i < 1000; i++) {
        foc_core_fast_loop(&foc, 0.000050f);
        foc_virtual_motor_step(&glue_state.vmotor, 12.0f, 0.0f, 0.0f, 0.000050f, 0.0f);
        edge_sys_step(&sys);
    }

    foc_telemetry_t telem;
    foc_core_get_telemetry(&foc, &telem);
    printf("VESC Host Simulation Finished. RPM: %.1f, Current Q: %.2f A, Errors: %u\n",
           (double)telem.speed_rpm, (double)telem.current_q, telem.faults);

    return 0;
}
