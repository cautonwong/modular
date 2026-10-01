#include "bldc/sys.h"
#include "edge/event.h"
#include "edge/modules.h"
#include "foc_core/foc_core.h"
#include "glue.h"
#include "motor_config/motor_config.h"
#include "soc_stm32f4/soc_stm32f4.h"
#include "timeout_guard/timeout_guard.h"
#include "vesc6/board.h"

#include <stdalign.h>
#include <stdint.h>
#include <string.h>

/*
 * The VESC6 / STM32F405-407 composition root (D1). The modules are assembled here, explicitly,
 * with every byte of their state provided by this function and no dynamic allocation anywhere.
 *
 * As of D2 the assembly reaches the part: TIM1 is set up for the three phases and ADC1, ADC2 and
 * ADC3 for the three phase currents, with the injected-conversion interrupt reading its ranks into
 * the snapshot the control loop's port reads. Two things are deliberately not wired, and both are
 * named where they are refused rather than filled in with a guess:
 *
 *   - the supply voltage's regular channel, which this checkout does not establish (the reference's
 *     ADC_Value indices are DMA buffer positions, not channel numbers), so read_vbus says so;
 *   - the board's gate-driver dead time, which board/vesc6 does not carry, so the phase outputs
 *     stay off - MOE is what puts voltage on a motor and a wrong dead time is a short.
 *
 * That is why the control loop is not stepped here: it would fault on the first supply-voltage
 * read. Everything else about the path is live - the timer is configured and running, the ADCs
 * convert, and the hook is installed.
 */
int main(void) {
    board_vesc6_t board;
    if (board_vesc6_init(&board) != EDGE_OK) {
        return 10;
    }

    vesc6_glue_state_t glue;
    vesc6_glue_init(&glue, &board);

    /*
     * The peripherals, bound here because this is the composition root: the soc owns what the
     * registers mean, and the product owns which ones this firmware uses. Off the part those
     * addresses do not exist, so the host build binds nothing and the adapters are handed blocks of
     * RAM by their own unit tests instead - which is where their logic is checked.
     */
#if defined(EDGE_TARGET_ARM32)
    glue.tim = (soc_stm32f4_tim_regs_t *)(uintptr_t)SOC_STM32F4_TIM1_BASE;
    glue.adc_current[0] = (soc_stm32f4_adc_regs_t *)(uintptr_t)SOC_STM32F4_ADC1_BASE;
    glue.adc_current[1] = (soc_stm32f4_adc_regs_t *)(uintptr_t)SOC_STM32F4_ADC2_BASE;
    glue.adc_current[2] = (soc_stm32f4_adc_regs_t *)(uintptr_t)SOC_STM32F4_ADC3_BASE;
    glue.adc_vbus = NULL; /* no established channel; see the note above */
#endif

    motor_config_var_port_t vars;
    vesc6_make_var_port(&vars, &glue);

    /* The configuration's memory is the composition root's to provide, not the module's. */
    alignas(
        MOTOR_CONFIG_STORAGE_ALIGN) static unsigned char config_storage[MOTOR_CONFIG_STORAGE_SIZE];
    motor_config_t *config = (motor_config_t *)config_storage;
    motor_config_construct(config, EDGE_MOD_MOTOR_CONFIG, 30u, &vars);
    if (motor_config_init(config) != EDGE_OK) {
        return 11;
    }
    const mc_configuration_t *mc = motor_config_get_mc(config);

    /* The timer, at the frequency the configuration asks for, with the switching period the
     * reference's own use of these two fields implies (mcpwm_foc.c:5112 multiplies foc_dt_us by
     * 1e-6 times foc_f_zv for its modulation compensation). */
    const soc_stm32f4_tim_config_t tim_config = {
        .timer_clk_hz = SOC_STM32F4_TIM_CLK_HZ,
        .switching_freq_hz = (uint32_t)mc->foc_f_zv,
        .deadtime_ns = glue.deadtime_ns,
    };
    /*
     * Off the part there is no timer to configure. The host build takes the same path and skips
     * this block, which is why the condition is on the bound pointer rather than on the build.
     */
    if (glue.tim != NULL) {
        const uint32_t period = soc_stm32f4_tim_init(glue.tim, &tim_config);
        if (period == 0u) {
            return 12;
        }
        glue.period = period;

        /* The three phase currents: one channel per ADC, three injected ranks each, timer
         * triggered, with the interrupt on so the hook below collects them (hw_60_core.c:205-213).
         */
        const soc_stm32f4_adc_config_t adc_phase[3] = {
            {.channel = 10u,
             .samples = VESC6_CURRENT_RANKS,
             .sample_time = 1u,
             .jeoc_interrupt = true},
            {.channel = 11u,
             .samples = VESC6_CURRENT_RANKS,
             .sample_time = 1u,
             .jeoc_interrupt = true},
            {.channel = 12u,
             .samples = VESC6_CURRENT_RANKS,
             .sample_time = 1u,
             .jeoc_interrupt = true},
        };
        for (uint32_t phase = 0u; phase < 3u; ++phase) {
            if (soc_stm32f4_adc_init_injected(glue.adc_current[phase], &adc_phase[phase]) == 0u) {
                return 13;
            }
        }
        soc_stm32f4_adc_set_injected_handler(vesc6_adc_injected_hook, &glue);
    }

    foc_inverter_port_t inverter_port;
    vesc6_make_inverter_port(&inverter_port, &glue);
    foc_current_port_t current_port;
    vesc6_make_current_port(&current_port, &glue);

    const foc_config_t foc_config = {
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

    /* The rotor sensor is installed only when the configuration asks for a sensored mode: in a
     * sensorless one the observer owns the angle, and a port that could not answer would be a
     * claim the FOC never needs. */
    foc_rotor_port_t rotor_port;
    vesc6_make_rotor_port(&rotor_port, &glue);
    const foc_rotor_port_t *const rotor = foc_config.sensorless_mode ? NULL : &rotor_port;

    foc_core_t foc;
    foc_core_construct(&foc, EDGE_MOD_FOC_CORE, 10u, &foc_config, &inverter_port, &current_port,
                       rotor);
    if (foc_core_init(&foc) != EDGE_OK) {
        return 14;
    }

    /*
     * The backup block, reference conf_general_read_backup_data (conf_general.c:100-146): read back
     * through the aggregate's own validity rule, then stored again with the flags written for the
     * whole block, which is what the reference's boot path does once it has repaired what it could.
     * A first boot finds nothing - this store reports an empty slot - and starts from zero, which
     * is what a product with no stored block should do. The aggregate stores it back from its
     * power_off, the same shutdown-only point the reference's own store is called from.
     */
    foc_storage_port_t backup_store;
    vesc6_make_backup_store_port(&backup_store, &glue);
    foc_core_set_storage_port(&foc, &backup_store);

    uint8_t backup_block[FOC_BACKUP_BLOCK_BYTES];
    memset(backup_block, 0, sizeof(backup_block));
    if (vesc6_backup_read(&glue, backup_block, sizeof(backup_block)) == EDGE_OK) {
        (void)foc_core_backup_restore(&foc, backup_block, sizeof(backup_block));
    }
    if (foc_core_backup_serialize(&foc, backup_block, sizeof(backup_block)) != 0u) {
        (void)backup_store.store_backup(backup_store.self, backup_block, sizeof(backup_block));
    }

    alignas(
        TIMEOUT_GUARD_STORAGE_ALIGN) static unsigned char guard_storage[TIMEOUT_GUARD_STORAGE_SIZE];
    timeout_guard_t *guard = (timeout_guard_t *)guard_storage;
    timeout_guard_construct(guard, EDGE_MOD_TIMEOUT_GUARD, 5u, NULL, 500u, 5.0f, 1000u);
    if (timeout_guard_init(guard) != EDGE_OK) {
        return 15;
    }

    edge_module_t *apps[3];
    apps[0] = foc_core_module(&foc);
    apps[1] = motor_config_module(config);
    apps[2] = timeout_guard_module(guard);

    edge_event_t event_storage[16];
    edge_event_queue_t event_queue;
    if (edge_event_queue_init(&event_queue, event_storage, 16u) != EDGE_OK) {
        return 1;
    }

    edge_sys_subscription_t subscriptions[8];
    edge_sys_t sys;
    if (sys_bldc_init(&sys, apps, 3u, &event_queue, subscriptions, 8u) != EDGE_OK) {
        return 2;
    }
    /* Nothing polls until the system is started: edge_sys_step() answers EDGE_ESTATE while the
     * state is not EDGE_SYS_RUNNING. */
    if (edge_sys_start(&sys) != EDGE_OK) {
        return 3;
    }

    /*
     * The reference firmware's main loop is interrupt-driven and never returns. This one steps the
     * scheduler a bounded number of times and then powers down - the shape the example product
     * uses - so the same binary is runnable as a host smoke test as well as cross-built for the
     * part. The control loop is not commanded here (see the note at the top): the modules are
     * assembled, initialized and idle.
     */
    for (unsigned i = 0u; i < 1000u; ++i) {
        if (edge_sys_step(&sys) != EDGE_OK) {
            (void)edge_sys_power_off(&sys);
            return 20;
        }
    }

    if (edge_sys_power_off(&sys) != EDGE_OK) {
        return 21;
    }
    if (edge_sys_deinit(&sys) != EDGE_OK) {
        return 22;
    }
    (void)motor_config_deinit(config);

    return 0;
}
