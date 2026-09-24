#include "bldc/sys.h"
#include "edge/clock.h"
#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"
#include "example/board.h"
#include "foc_core/foc_core.h"
#include "glue.h"
#include "motor_config/motor_config.h"
#include "vesc_comm/vesc_comm.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t monotonic_ticks(void *self) {
    return ++(*(uint64_t *)self);
}

int main(void) {
    uint64_t clock_tick = 0u;
    vesc_host_glue_state_t glue_state;
    memset(&glue_state, 0, sizeof(glue_state));
    glue_state.v_bus = 24.0f;

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

    /* Assemble App List */
    edge_module_t *apps[3];
    apps[0] = foc_core_module(&foc);
    apps[1] = vesc_comm_module(&comm);
    apps[2] = motor_config_module(&motor_cfg);

    /* System & Event Infrastructure */
    edge_event_t event_storage[16];
    edge_event_queue_t event_queue;
    edge_clock_port_t clock = {
        .monotonic_ticks = monotonic_ticks,
        .wall_time = NULL,
        .self = &clock_tick,
    };
    edge_event_sink_t event_sink;
    edge_sys_subscription_t subscriptions[8];
    edge_sys_t sys;

    if (edge_event_queue_init(&event_queue, event_storage, 16u) < 0) {
        return 1;
    }
    event_sink = (edge_event_sink_t){.queue = &event_queue, .clock = &clock, .guard = NULL};
    board_example_init(&event_sink);

    if (sys_bldc_init(&sys, apps, 3u, &event_queue, subscriptions, 8u) < 0) {
        return 2;
    }
    if (edge_sys_set_clock(&sys, &clock) < 0) {
        return 3;
    }
    if (edge_sys_start(&sys) < 0) {
        return 4;
    }

    /* Set target current and start fast loop */
    foc_core_set_current(&foc, 10.0f, 0.0f);

    /* Simulate 1000 background steps + fast loop stepping */
    for (unsigned i = 0u; i < 1000u; ++i) {
        /* 25 kHz fast loop stepping */
        foc_core_fast_loop(&foc, 1.0f / 25000.0f);

        float va = foc.duty_a * glue_state.v_bus;
        float vb = foc.duty_b * glue_state.v_bus;
        float vc = foc.duty_c * glue_state.v_bus;
        foc_virtual_motor_step(&glue_state.vmotor, va, vb, vc, 1.0f / 25000.0f, 0.0f);

        /* Background system poll */
        if (edge_sys_run_once(&sys) < 0) {
            return 5;
        }
    }

    foc_telemetry_t telem;
    foc_core_get_telemetry(&foc, &telem);
    printf("VESC host product run complete: speed=%.1f RPM, iq=%.2f A, Vbus=%.1f V\n",
           telem.speed_rpm, telem.current_q, telem.v_bus);

    return 0;
}
