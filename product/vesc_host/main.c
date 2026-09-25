#include "adc_input/adc_input.h"
#include "balance/balance.h"
#include "bldc/sys.h"
#include "edge/clock.h"
#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"
#include "example/board.h"
#include "flash/flash.h"
#include "foc_core/foc_core.h"
#include "glue.h"
#include "motor_config/motor_config.h"
#include "motor_id/motor_id.h"
#include "nunchuk/nunchuk.h"
#include "pas/pas.h"
#include "ppm/ppm.h"
#include "throttle/throttle.h"
#include "timeout_guard/timeout_guard.h"
#include "vesc_can/vesc_can.h"
#include "vesc_comm/vesc_comm.h"
#include "vesc_terminal/vesc_terminal.h"
#include <stdalign.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    vesc_host_glue_state_t glue_state;
    memset(&glue_state, 0, sizeof(glue_state));
    /* Erased flash reads as ones, and the emulation looks for its page markers there; a
     * zeroed image would look like two valid pages. */
    memset(glue_state.flash_mem, 0xFF, sizeof(glue_state.flash_mem));
    glue_state.v_bus = 24.0f;
    glue_state.ppm_pulse_us = 1500.0f;
    glue_state.adc_throttle_v = 1.0f;

    /* Initialize virtual motor */
    foc_virtual_motor_init(&glue_state.vmotor, 0.015f, 0.000020f, 0.005f, 7, 0.0001f);

    /* Construct Ports */
    flash_sector_port_t flash_sectors;
    vesc_host_make_flash_sector_port(&flash_sectors, &glue_state);

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

    nunchuk_port_t nunchuk_port;
    vesc_host_make_nunchuk_port(&nunchuk_port, &glue_state);

    pas_port_t pas_port;
    vesc_host_make_pas_port(&pas_port, &glue_state);

    balance_port_t balance_port;
    vesc_host_make_balance_port(&balance_port, &glue_state);

    terminal_stream_port_t term_stream_port;
    vesc_host_make_terminal_stream_port(&term_stream_port, &glue_state);

    /*
     * The configuration's variable store, which is how the reference persists it: the EEPROM
     * emulation over the flash sectors above, with a variable table enumerating the reference's
     * virtual address base (conf_general.c:50, :72).
     */
    static uint16_t mcconf_var_table[VESC_HOST_MCCONF_VARS];
    for (size_t i = 0u; i < VESC_HOST_MCCONF_VARS; i++) {
        mcconf_var_table[i] = (uint16_t)(VESC_HOST_MCCONF_BASE + i);
    }
    static flash_emul_t flash_store;
    flash_emul_construct(
        &flash_store, &flash_sectors,
        (flash_var_table_t){.virtual_addresses = mcconf_var_table, .count = VESC_HOST_MCCONF_VARS},
        0u);
    if (flash_emul_init(&flash_store) != EDGE_OK) {
        return 11;
    }
    motor_config_var_port_t var_port;
    vesc_host_make_var_port(&var_port, &flash_store);

    /* Construct Apps */
    /* The configuration module's memory is the composition root's to provide. */
    static alignas(
        MOTOR_CONFIG_STORAGE_ALIGN) unsigned char motor_cfg_storage[MOTOR_CONFIG_STORAGE_SIZE];
    motor_config_t *motor_cfg = (motor_config_t *)motor_cfg_storage;
    motor_config_construct(motor_cfg, EDGE_MOD_MOTOR_CONFIG, 30u, &var_port);
    if (motor_config_init(motor_cfg) < 0) {
        return 10;
    }

    const mc_configuration_t *mc = motor_config_get_mc(motor_cfg);
    foc_config_t foc_cfg = {
        .r_ohm = mc->foc_motor_r,
        .l_henry = mc->foc_motor_l,
        .lambda_wb = mc->foc_motor_flux_linkage,
        .si_motor_poles = mc->si_motor_poles,
        .si_gear_ratio = mc->si_gear_ratio,
        .si_wheel_diameter = mc->si_wheel_diameter,
        .current_max_a = mc->l_current_max,
        .current_min_a = mc->l_current_min,
        .m_invert_direction = mc->m_invert_direction,
        .duty_max = mc->l_max_duty,
        .current_kp = mc->foc_current_kp,
        .current_ki = mc->foc_current_ki,
        .vbus_ov_threshold = mc->l_max_vin,
        .vbus_uv_threshold = mc->l_min_vin,
        .temp_fet_max_c = mc->l_temp_fet_end,
        /*
         * The reference chooses the angle source from the configuration's sensor mode
         * (mcpwm_foc.c switches on FOC_SENSOR_MODE_SENSORLESS), so this is derived rather
         * than fixed: hardcoding it made the configuration's sensor mode unreadable by the
         * control path.
         */
        .sensorless_mode = (mc->foc_sensor_mode == FOC_SENSOR_MODE_SENSORLESS),
        .observer_gamma = mc->foc_observer_gain,
        .observer_type = (foc_observer_type_t)mc->foc_observer_type,
        .sat_comp_mode = mc->foc_sat_comp_mode,
        .sat_comp = mc->foc_sat_comp,
        .ld_lq_diff = mc->foc_motor_ld_lq_diff,
        /* openloop_rpm is unused while index_found is passed true (see foc_core);
         * the port has no encoder index to lose, so there is nothing to clamp. */
        .speed_pid = {.kp = mc->s_pid_kp,
                      .ki = mc->s_pid_ki,
                      .kd = mc->s_pid_kd,
                      .kd_filter = mc->s_pid_kd_filter,
                      .ramp_erpms_s = mc->s_pid_ramp_erpms_s,
                      .min_erpm = mc->s_pid_min_erpm,
                      .openloop_rpm = 0.0f,
                      .l_min_erpm = mc->l_min_erpm,
                      .l_max_erpm = mc->l_max_erpm,
                      .lo_current_max = mc->l_current_max,
                      .current_max_scale = mc->l_current_max_scale,
                      .allow_braking = mc->s_pid_allow_braking,
                      .invert_direction = mc->m_invert_direction},
        .pll_kp = mc->foc_pll_kp,
        .pll_ki = mc->foc_pll_ki,
        .current_filter_const = mc->foc_current_filter_const,
        /* Field weakening and MTPA; the reference's own defaults are disabled FW and MTPA
         * off, so a configuration that never touches them behaves as before. */
        .fw_current_max = mc->foc_fw_current_max,
        .fw_duty_start = mc->foc_fw_duty_start,
        .fw_backoff = mc->foc_fw_backoff,
        .fw_ramp_time = mc->foc_fw_ramp_time,
        .fw_q_current_factor = mc->foc_fw_q_current_factor,
        .mtpa_mode = (uint8_t)mc->foc_mtpa_mode,
        .cc_min_current = mc->cc_min_current,
        .si_battery_type = (uint8_t)mc->si_battery_type,
        .si_battery_cells = mc->si_battery_cells,
        .si_battery_ah = mc->si_battery_ah,
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

    /*
     * Identity facts for COMM_FW_VERSION. The host has no MCU serial, so the uuid
     * is zeroed rather than invented, and the version is this repository's own
     * (VERSION, project() in CMakeLists) rather than the reference's 7.1: this
     * port implements a subset of the reference's command set, and reporting the
     * reference's number would claim API coverage it does not have.
     */
    static const uint8_t host_uuid[12] = {0};
    const vesc_identity_t comm_identity = {
        .hw_name = "EXAMPLE", /* the board this product actually binds; a vesc6-bound
                               * product should pass VESC6_HW_NAME from board/vesc6 */
        .fw_name = "vesc_host",
        .uuid = host_uuid,
        .fw_version_major = 0u,
        .fw_version_minor = 5u,
        .pairing_done = 0u,
        .fw_test_version = 0u,
        .hw_type = 0u,
        .custom_cfg_num = 0u,
        .phase_filters = 0u,
        .qmlui_hw = 0u,
        .qmlui_app = 0u,
        .nrf_flags = 0u,
        .controller_id = 1u,
        .hw_crc = 0u,
    };

    /*
     * The codec's memory is the composition root's to provide: a block of the
     * documented size and alignment, with nothing known about the fields inside.
     */
    static alignas(VESC_COMM_STORAGE_ALIGN) unsigned char comm_storage[VESC_COMM_STORAGE_SIZE];
    vesc_comm_t *comm = (vesc_comm_t *)comm_storage;
    vesc_app_status_port_t app_status_port;
    vesc_host_make_app_status_port(&app_status_port, &glue_state);

    vesc_config_provider_port_t config_port;
    vesc_host_make_config_port(&config_port, motor_cfg);

    /*
     * The terminal is built before the codec because COMM_TERMINAL_CMD runs through it; the
     * ops port carries that one callback, keeping "acts on the product" apart from
     * "configuration access".
     */
    terminal_system_port_t term_sys_port;
    vesc_host_make_terminal_system_port(&term_sys_port, &foc);

    vesc_terminal_app_t term_app;
    vesc_terminal_construct(&term_app, EDGE_MOD_VESC_TERMINAL, 50u, &term_stream_port,
                            &term_sys_port);
    if (vesc_terminal_init(&term_app) != EDGE_OK) {
        return 22;
    }

    /* COMM_FORWARD_CAN needs the CAN bus and COMM_TERMINAL_CMD the terminal, so both are
     * built before the codec and handed over through the ops context. */
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

    vesc_host_ops_ctx_t ops_ctx = {.term = &term_app, .can = &can_app};
    vesc_comm_ops_port_t ops_port;
    vesc_host_make_ops_port(&ops_port, &ops_ctx);

    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 20u, &stream_tx_port, &motor_port,
                        &app_status_port, &config_port, &ops_port, &comm_identity);
    if (vesc_comm_init(comm) < 0) {
        return 12;
    }

    /* The guard's memory is the composition root's to provide. */
    static alignas(
        TIMEOUT_GUARD_STORAGE_ALIGN) unsigned char guard_storage[TIMEOUT_GUARD_STORAGE_SIZE];
    timeout_guard_t *guard = (timeout_guard_t *)guard_storage;
    timeout_guard_construct(guard, EDGE_MOD_TIMEOUT_GUARD, 5u, NULL, 500u, 5.0f, 1000u);
    if (timeout_guard_init(guard) != EDGE_OK) {
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
    glue_state.ppm = &ppm;
    glue_state.adc = &adc_app;

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

    nunchuk_app_t nunchuk_app;
    nunchuk_config_t nunchuk_cfg = {.deadband = 0.05f, .timeout_s = 0.2f};
    nunchuk_construct(&nunchuk_app, EDGE_MOD_NUNCHUK, 25u, &nunchuk_cfg, &nunchuk_port);
    if (nunchuk_init(&nunchuk_app) != EDGE_OK) {
        return 19;
    }

    pas_app_t pas_app;
    pas_config_t pas_cfg = {.assist_ratio = 1.0f, .max_motor_current_a = 20.0f};
    pas_construct(&pas_app, EDGE_MOD_PAS, 25u, &pas_cfg, &pas_port);
    if (pas_init(&pas_app) != EDGE_OK) {
        return 20;
    }

    balance_app_t balance_app;
    balance_config_t balance_cfg = {.kp = 1.5f, .max_current_a = 30.0f};
    balance_construct(&balance_app, EDGE_MOD_BALANCE, 10u, &balance_cfg, &balance_port);
    if (balance_init(&balance_app) != EDGE_OK) {
        return 21;
    }

    bms_can_port_t bms_can_port;
    vesc_host_make_bms_can_port(&bms_can_port, &glue_state);

    vesc_bms_app_t bms_app;
    vesc_bms_construct(&bms_app, EDGE_MOD_VESC_BMS, 45u, NULL, &bms_can_port);
    if (vesc_bms_init(&bms_app) != EDGE_OK) {
        return 23;
    }

    /* Assemble App List */
    edge_module_t *apps[14];
    apps[0] = foc_core_module(&foc);
    apps[1] = vesc_comm_module(comm);
    apps[2] = motor_config_module(motor_cfg);
    apps[3] = timeout_guard_module(guard);
    apps[4] = throttle_module(&throttle);
    apps[5] = ppm_module(&ppm);
    apps[6] = adc_input_module(&adc_app);
    apps[7] = vesc_can_module(&can_app);
    apps[8] = motor_id_module(&motor_id);
    apps[9] = nunchuk_module(&nunchuk_app);
    apps[10] = pas_module(&pas_app);
    apps[11] = balance_module(&balance_app);
    apps[12] = vesc_terminal_module(&term_app);
    apps[13] = vesc_bms_module(&bms_app);

    /* System & Event Infrastructure */
    edge_event_t event_storage[16];
    edge_event_queue_t event_queue;
    edge_event_queue_init(&event_queue, event_storage, 16);

    edge_sys_subscription_t subs[16];
    edge_sys_t sys;

    if (sys_bldc_init(&sys, apps, 14, &event_queue, subs, 16) != EDGE_OK) {
        return 1;
    }

    foc_core_set_current(&foc, 5.0f, 0.0f);

    /* Run 1000 fast-loop FOC and system cycles */
    for (int i = 0; i < 1000; i++) {
        foc_core_fast_loop(&foc, 0.000050f);
        /* Close the loop on the voltage vector the FOC just commanded, which is what
         * the reference firmware's virtual-motor hook does (virtual_motor_int_handler(
         * v_alpha, v_beta) from the FOC ISR). Feeding the plant a fixed voltage instead
         * leaves the actuator disconnected from the controller: the d-axis current then
         * diverges and the loop trips its own over-current guard. */
        foc_virtual_motor_step(&glue_state.vmotor, foc.v_alpha, foc.v_beta, 0.0f, 0.000050f, 0.0f);
        edge_sys_step(&sys);
    }

    foc_telemetry_t telem;
    foc_core_get_telemetry(&foc, &telem);
    printf("VESC Host Simulation Finished. RPM: %.1f, Current Q: %.2f A, Errors: %u\n",
           (double)telem.speed_rpm, (double)telem.current_q, telem.faults);

    /*
     * The run asserts itself, so CI can use the exit code instead of grepping
     * stdout: a closed loop that ends in a fault, or that did not spin the motor,
     * exits non-zero. Before this the product printed its state and returned 0
     * whatever happened, which is how it spent a while reporting a diverged
     * d-axis current and an over-current fault unnoticed.
     */
    if (telem.faults != 0u) {
        printf("FAIL: the loop ended in fault 0x%x\n", telem.faults);
        return 30;
    }
    if (telem.speed_rpm < 100.0f) {
        printf("FAIL: closed loop did not spin the motor (%.1f rpm)\n", (double)telem.speed_rpm);
        return 31;
    }

    return 0;
}
