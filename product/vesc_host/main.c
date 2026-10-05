#include "adc_input/adc_input.h"
#include "balance/balance.h"
#include "bldc/sys.h"
#include "bldc_drive/bldc_drive.h"
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
    /* The two NTC readings the host simulation is given; the sampler filters them into the
     * values the FOC caches, which is where the reported motor temperature and the
     * temperature compensation both come from. */
    glue_state.fet_temp_raw_c = 30.0f;
    glue_state.motor_temp_raw_c = 25.0f;

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
    static uint16_t
        mcconf_var_table[VESC_HOST_MCCONF_VARS + VESC_HOST_APPCONF_VARS + VESC_HOST_BACKUP_VARS];
    for (size_t i = 0u; i < VESC_HOST_MCCONF_VARS; i++) {
        mcconf_var_table[i] = (uint16_t)(VESC_HOST_MCCONF_BASE + i);
    }
    /* The application configuration's next, and the backup block's after it: the list is what tells
     * the emulation which addresses exist, so all three ranges are in it. */
    for (size_t i = 0u; i < VESC_HOST_APPCONF_VARS; i++) {
        mcconf_var_table[VESC_HOST_MCCONF_VARS + i] = (uint16_t)(VESC_HOST_APPCONF_BASE + i);
    }
    for (size_t i = 0u; i < VESC_HOST_BACKUP_VARS; i++) {
        mcconf_var_table[VESC_HOST_MCCONF_VARS + VESC_HOST_APPCONF_VARS + i] =
            (uint16_t)(VESC_HOST_BACKUP_BASE + i);
    }
    static flash_emul_t flash_store;
    flash_emul_construct(&flash_store, &flash_sectors,
                         (flash_var_table_t){.virtual_addresses = mcconf_var_table,
                                             .count = VESC_HOST_MCCONF_VARS +
                                                      VESC_HOST_APPCONF_VARS +
                                                      VESC_HOST_BACKUP_VARS},
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
    glue_state.config = motor_cfg;
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
        /*
         * The HFI modes are sensorless ones: the observer runs underneath them and HFI corrects its
         * angle (mcpwm_foc.c:3579-3591), so reading the sensor mode as sensorless-or-not alone put
         * them on the rotor-sensor path, which this board has none of.
         */
        .sensorless_mode = (mc->foc_sensor_mode == FOC_SENSOR_MODE_SENSORLESS ||
                            mc->foc_sensor_mode == FOC_SENSOR_MODE_HFI ||
                            mc->foc_sensor_mode == FOC_SENSOR_MODE_HFI_START ||
                            (mc->foc_sensor_mode >= FOC_SENSOR_MODE_HFI_V2 &&
                             mc->foc_sensor_mode <= FOC_SENSOR_MODE_HFI_V5)),
        .observer_gamma = mc->foc_observer_gain,
        .observer_type = (foc_observer_type_t)mc->foc_observer_type,
        .sat_comp_mode = mc->foc_sat_comp_mode,
        .sat_comp = mc->foc_sat_comp,
        .ld_lq_diff = mc->foc_motor_ld_lq_diff,
        .temp_comp = mc->foc_temp_comp,
        .temp_comp_base_temp = mc->foc_temp_comp_base_temp,
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
        .pos_pid = {.kp = mc->p_pid_kp,
                    .ki = mc->p_pid_ki,
                    .kd = mc->p_pid_kd,
                    .kd_proc = mc->p_pid_kd_proc,
                    .kd_filter = mc->p_pid_kd_filter,
                    .gain_dec_angle = mc->p_pid_gain_dec_angle,
                    .ang_div = mc->p_pid_ang_div,
                    /* The reference scales the loop's output by both of these (foc_math.c:496-497),
                     * so what reaches the loop is their product. */
                    .current_max_a = mc->l_current_max * mc->l_current_max_scale,
                    .error_sign = mc->foc_encoder_inverted ? -1.0f : 1.0f},
        .l_abs_current_max = mc->l_abs_current_max,
        .limits = {.l_temp_fet_start = mc->l_temp_fet_start,
                   .l_temp_fet_end = mc->l_temp_fet_end,
                   .l_temp_motor_start = mc->l_temp_motor_start,
                   .l_temp_motor_end = mc->l_temp_motor_end,
                   .l_erpm_start = mc->l_erpm_start,
                   .l_max_erpm = mc->l_max_erpm,
                   .l_min_erpm = mc->l_min_erpm,
                   .foc_start_curr_dec = mc->foc_start_curr_dec,
                   .foc_start_curr_dec_rpm = mc->foc_start_curr_dec_rpm,
                   .l_duty_start = mc->l_duty_start,
                   .l_max_duty = mc->l_max_duty,
                   .cc_min_current = mc->cc_min_current,
                   .l_watt_max = mc->l_watt_max,
                   .l_watt_min = mc->l_watt_min,
                   .l_in_current_max = mc->l_in_current_max,
                   .l_in_current_min = mc->l_in_current_min,
                   .l_in_current_map_start = mc->l_in_current_map_start,
                   .l_battery_cut_start = mc->l_battery_cut_start,
                   .l_battery_cut_end = mc->l_battery_cut_end,
                   .l_battery_regen_cut_start = mc->l_battery_regen_cut_start,
                   .l_battery_regen_cut_end = mc->l_battery_regen_cut_end},
        .pll_kp = mc->foc_pll_kp,
        .pll_ki = mc->foc_pll_ki,
        /*
         * HFI. The mode comes from the same configuration: foc_sensor_mode is copied as the angle
         * source it names - the two enumerations list the same modes in the same order, which the
         * glue test pins - and the ambiguity and sampling-mode enumerations are one question each
         * here, because the ported six-vector path is the one FOC_AMB_MODE_SIX_VECTOR selects and
         * the lag compensation's period halves only in FOC_CONTROL_SAMPLE_MODE_V0_V7.
         */
        .sensor_mode = (foc_sensor_mode_t)mc->foc_sensor_mode,
        .hfi_amb_mode_six_vector = (mc->foc_hfi_amb_mode == FOC_AMB_MODE_SIX_VECTOR),
        .hfi_control_sample_mode_v0_v7 =
            (mc->foc_control_sample_mode == FOC_CONTROL_SAMPLE_MODE_V0_V7),
        .hfi_samples = mc->foc_hfi_samples,
        .hfi_voltage_start = mc->foc_hfi_voltage_start,
        .hfi_voltage_run = mc->foc_hfi_voltage_run,
        .hfi_voltage_max = mc->foc_hfi_voltage_max,
        .hfi_gain = mc->foc_hfi_gain,
        .hfi_max_err = mc->foc_hfi_max_err,
        .sl_erpm_hfi = mc->foc_sl_erpm_hfi,
        .hfi_start_samples = mc->foc_hfi_start_samples,
        .hfi_obs_ovr_sec = mc->foc_hfi_obs_ovr_sec,
        .f_zv = mc->foc_f_zv,
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

    /*
     * The backup block, reference conf_general_read_backup_data (conf_general.c:100-146): read back
     * word by word, restored through the aggregate's own validity rule - each field stands on its
     * own flag - and then stored again with the flags written for the whole block, which is what
     * the reference's boot path does once it has repaired what it could. A block that cannot be
     * read at all is treated as empty rather than as something to trust.
     *
     * The store itself is the port below, and the aggregate calls it from its power_off - the same
     * shutdown-only point the reference's own store is called from.
     */
    foc_storage_port_t backup_store;
    vesc_host_make_backup_store_port(&backup_store, &flash_store);
    foc_core_set_storage_port(&foc, &backup_store);

    uint8_t backup_block[FOC_BACKUP_BLOCK_BYTES];
    memset(backup_block, 0, sizeof(backup_block));
    for (size_t i = 0u; i < VESC_HOST_BACKUP_VARS; i++) {
        uint16_t word = 0u;
        if (flash_emul_read(&flash_store, (uint16_t)(VESC_HOST_BACKUP_BASE + i), &word) !=
            EDGE_OK) {
            memset(backup_block, 0, sizeof(backup_block));
            break;
        }
        backup_block[2u * i] = (uint8_t)(word >> 8);
        backup_block[2u * i + 1u] = (uint8_t)word;
    }

    (void)foc_core_backup_restore(&foc, backup_block, sizeof(backup_block));
    if (foc_core_backup_serialize(&foc, backup_block, sizeof(backup_block)) != 0u) {
        (void)backup_store.store_backup(backup_store.self, backup_block, sizeof(backup_block));
    }

    /* Motor identification drives the FOC aggregate, so its port is built from it. */
    motor_id_measure_port_t id_m_port;
    vesc_host_make_motor_id_measure_port(&id_m_port, &glue_state);

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
    const app_configuration_t *appconf = motor_config_get_app(motor_cfg);
    vesc_can_config_t can_cfg = {
        .controller_id = 1,
        .baudrate = 500000,
        /* The application configuration's own scheduling, which is what the reference's two status
         * threads read: a rate per sender in hertz, and the mask of which frames each sends. */
        .can_mode = (uint8_t)appconf->can_mode,
        .status_rate_1_hz = (float)appconf->can_status_rate_1,
        .status_rate_2_hz = (float)appconf->can_status_rate_2,
        .status_msgs_r1 = appconf->can_status_msgs_r1,
        .status_msgs_r2 = appconf->can_status_msgs_r2,
    };
    vesc_can_construct(&can_app, EDGE_MOD_VESC_CAN, 20u, &can_cfg, &can_port);
    if (vesc_can_init(&can_app) != EDGE_OK) {
        return 17;
    }

    vesc_host_ops_ctx_t ops_ctx = {.term = &term_app, .can = &can_app};

    /* The aggregate reads the bus's other controllers through this port, which is the product's own
     * answer because the product is what owns the bus. */
    foc_peer_port_t peer_port;
    vesc_host_make_peer_port(&peer_port, &can_app);
    foc_core_set_peer_port(&foc, &peer_port);
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
    glue_state.timeout = guard;
    if (timeout_guard_init(guard) != EDGE_OK) {
        return 13;
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
    motor_id_construct(&motor_id, EDGE_MOD_MOTOR_ID, 40u, &id_m_port);
    if (motor_id_init(&motor_id) != EDGE_OK) {
        return 18;
    }
    /* The flux-linkage command drives the procedure and the plant together, so the ops context
     * carries both once they exist. */
    ops_ctx.glue = &glue_state;
    ops_ctx.motor_id = &motor_id;
    /* The all-in-one detection keeps what it measured in the configuration, so it needs the module
     * the configuration lives in. */
    ops_ctx.config = motor_cfg;

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

    /*
     * The six-step drive, phase F. Its configuration is the motor configuration's own sensor mode,
     * hall table and hall_sl_erpm, which is where the reference's six-step layer reads them from
     * too (mcpwm.c:501 builds one of those tables, :2586 uses the other two); the speed its
     * sensor-mode decision needs arrives from the aggregate each cycle, the way that layer reads
     * its own motor state. What it does not have yet - the hall inputs, the phase outputs, the
     * commutation and the start-up modes - is named in its header and in the phase table.
     */
    const mc_configuration_t *bldc_mc = motor_config_get_mc(motor_cfg);
    bldc_drive_config_t bldc_cfg;
    memset(&bldc_cfg, 0, sizeof(bldc_cfg));
    bldc_cfg.sensor_mode = (uint8_t)bldc_mc->sensor_mode;
    bldc_cfg.hall_sl_erpm = bldc_mc->hall_sl_erpm;
    memcpy(bldc_cfg.hall_table, bldc_mc->hall_table, sizeof(bldc_cfg.hall_table));

    /* The sensorless start-up's own figures are the motor configuration's, which is where the
     * reference's six-step layer reads them from too. */
    bldc_cfg.rpm_dep = (bldc_rpm_dep_params_t){
        .sl_cycle_int_limit = bldc_mc->sl_cycle_int_limit,
        .sl_bemf_coupling_k = bldc_mc->sl_bemf_coupling_k,
        .sl_min_erpm = bldc_mc->sl_min_erpm,
        .sl_cycle_int_rpm_br = bldc_mc->sl_cycle_int_rpm_br,
        .sl_phase_advance_at_br = bldc_mc->sl_phase_advance_at_br,
        .sl_min_erpm_cycle_int_limit = bldc_mc->sl_min_erpm_cycle_int_limit,
        .m_bldc_f_sw_max = bldc_mc->m_bldc_f_sw_max,
    };
    bldc_cfg.comm_mode = (uint8_t)bldc_mc->comm_mode;
    /*
     * The board's divider correction, which the reference computes from its own VIN_R1 and VIN_R2
     * (conf_general.h:119). The host is a simulation wired to no divider, so its value is the
     * identity; a real board's arrives with that board's package, as its dead time does.
     */
    bldc_cfg.vdiv_corr = 1.0f;

    bldc_drive_t bldc_app;
    bldc_drive_construct(&bldc_app, EDGE_MOD_BLDC_DRIVE, 25u, &bldc_cfg);
    glue_state.bldc = &bldc_app;
    if (bldc_drive_init(&bldc_app) != EDGE_OK) {
        return 24;
    }

    /* Assemble App List */
    /*
     * The throttle app is not scheduled here. Its step applies deadband, curve and ramp to a raw
     * input, and this product has no raw input to give it: the host's simulated throttle reaches
     * the control path through the ppm and adc_input apps above, which is where the reference
     * applies the same curve (app_ppm.c / app_adc.c). Constructed with no input port it returned
     * EDGE_EINVAL from every poll - a failure that went unseen until the step's result stopped
     * being dropped, because edge_sys_step() was answering EDGE_ESTATE the whole time.
     */
    edge_module_t *apps[14];
    apps[0] = foc_core_module(&foc);
    apps[1] = vesc_comm_module(comm);
    apps[2] = motor_config_module(motor_cfg);
    apps[3] = timeout_guard_module(guard);
    apps[4] = ppm_module(&ppm);
    apps[5] = adc_input_module(&adc_app);
    apps[6] = vesc_can_module(&can_app);
    apps[7] = motor_id_module(&motor_id);
    apps[8] = nunchuk_module(&nunchuk_app);
    apps[9] = pas_module(&pas_app);
    apps[10] = balance_module(&balance_app);
    apps[11] = vesc_terminal_module(&term_app);
    apps[12] = vesc_bms_module(&bms_app);
    apps[13] = bldc_drive_module(&bldc_app);

    /* System & Event Infrastructure */
    edge_event_t event_storage[16];
    edge_event_queue_t event_queue;
    edge_event_queue_init(&event_queue, event_storage, 16);

    edge_sys_subscription_t subs[16];
    edge_sys_t sys;

    if (sys_bldc_init(&sys, apps, 14, &event_queue, subs, 16) != EDGE_OK) {
        return 1;
    }
    /*
     * Nothing polls until the system is started: edge_sys_step() answers EDGE_ESTATE while the
     * state is not EDGE_SYS_RUNNING. Until this call existed, every step below returned that
     * error and its result was dropped, so no module poll and no event dispatch ever ran - the
     * FOC loop turned the motor by itself, which is why the run looked healthy while the
     * scheduler's share of it was silently skipped.
     */
    if (edge_sys_start(&sys) != EDGE_OK) {
        return 6;
    }

    foc_core_set_current(&foc, 5.0f, 0.0f);

    /* Run 1000 fast-loop FOC and system cycles */
    for (int i = 0; i < 1000; i++) {
        /* The sampler runs ahead of the control cycle, as the reference's ADC interrupt
         * handler does; the FOC then reads the filtered values it cached. */
        vesc_host_sample_temperatures(&glue_state, &foc);
        foc_core_fast_loop(&foc, 0.000050f);
        /* Close the loop on the voltage vector the FOC just commanded, which is what
         * the reference firmware's virtual-motor hook does (virtual_motor_int_handler(
         * v_alpha, v_beta) from the FOC ISR). Feeding the plant a fixed voltage instead
         * leaves the actuator disconnected from the controller: the d-axis current then
         * diverges and the loop trips its own over-current guard. */
        foc_virtual_motor_step(&glue_state.vmotor, foc.v_alpha, foc.v_beta, 0.0f, 0.000050f, 0.0f);
        /*
         * A poll that fails marks its module failed and comes back here, so the run reports it
         * instead of dropping it the way the previous unchecked call did. The module that failed
         * is named: with fourteen modules polling, the error code alone does not say whose.
         */
        const edge_status_t step_rc = edge_sys_step(&sys);
        if (step_rc != EDGE_OK) {
            printf("FAIL: the scheduler rejected a step at cycle %d (rc 0x%x)\n", i,
                   (unsigned)step_rc);
            for (size_t m = 0u; m < 14u; m++) {
                if (apps[m]->failed) {
                    printf("      module 0x%x failed\n", (unsigned)apps[m]->module_id);
                }
            }
            return 32;
        }

        /*
         * A command asked the machine to restart. On a board that is a reset, with the backup block
         * stored first; here it ends the run, which is the simulation's own reset, and the store
         * happens below through the modules' power_off - the same path an ordinary shutdown takes.
         */
        if (glue_state.reboot_requested || glue_state.bootloader_requested) {
            break;
        }
    }

    if (glue_state.reboot_requested || glue_state.bootloader_requested) {
        if (glue_state.reboot_requested) {
            (void)edge_sys_power_off(&sys);
        }
        printf("VESC Host Simulation Ended: %s requested\n",
               glue_state.reboot_requested ? "reboot" : "bootloader jump");
        return 0;
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
