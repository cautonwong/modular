#ifndef PRODUCT_VESC_HOST_GLUE_H
#define PRODUCT_VESC_HOST_GLUE_H

#include "adc_input/adc_input.h"
#include "balance/balance.h"
#include "flash/flash.h"
#include "foc_core/foc_core.h"
#include "foc_core/foc_math.h"
#include "motor_config/motor_config.h"
#include "motor_id/motor_id.h"
#include "nunchuk/nunchuk.h"
#include "pas/pas.h"
#include "ppm/ppm.h"
#include "vesc_bms/vesc_bms.h"
#include "vesc_can/vesc_can.h"
#include "vesc_comm/vesc_comm.h"
#include "vesc_terminal/vesc_terminal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vesc_host_glue_state {
    foc_virtual_motor_t vmotor;
    foc_core_t *foc;
    float v_bus;
    /* The EEPROM emulation's two sectors, one FLASH_EMUL_PAGE_SIZE each: the configuration's
     * variable table is stored in this image, so the emulation's page size sets its size. */
    uint8_t flash_mem[2u * FLASH_EMUL_PAGE_SIZE];
    uint8_t stream_tx_buf[512];
    size_t stream_tx_len;
    float ppm_pulse_us;
    float adc_throttle_v;
    float adc_brake_v;

    /*
     * The two NTC readings the host simulation is given, and the filtered values the adapter
     * makes of them (vesc_host_sample_temperatures). Inputs the composition root sets, like
     * v_bus and the throttle voltages.
     */
    float fet_temp_raw_c;
    float motor_temp_raw_c;
    float fet_temp_c;
    float motor_temp_c;
    uint32_t last_can_id;
    uint8_t last_can_data[8];
    uint8_t last_can_len;
    char terminal_tx_buf[256];

    /* The decoded-input adapters read through the apps, not through the raw glue
     * fields; the composition root points these at the constructed apps. */
    const ppm_app_t *ppm;
    const adc_input_app_t *adc;
} vesc_host_glue_state_t;

void vesc_host_make_flash_sector_port(flash_sector_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_stream_tx_port(edge_stream_tx_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_motor_provider_port(vesc_motor_provider_port_t *out, foc_core_t *foc);

/*
 * COMM_GET/SET_MCCONF and COMM_GET/SET_APPCONF, served from the configuration aggregate.
 * The streams are the reference's own byte layouts; the aggregate owns the staging copy
 * that keeps a malformed stream from half-writing the running configuration.
 */
void vesc_host_make_config_port(vesc_config_provider_port_t *out, motor_config_t *cfg);

/*
 * The configuration's variable store, backed by the EEPROM emulation (infra/flash). Keys are the
 * logical indices motor_config uses; the reference's virtual address base EEPROM_BASE_MCCONF
 * (conf_general.c:50) is applied here, so the app never sees an address space. The emulation
 * itself is the product's: it needs a variable table (the reference builds it by enumerating
 * base + i, conf_general.c:72) and a sector backend.
 */
/* conf_general.c:50, and the size of the variable table the emulation walks: the reference
 * enumerates base + i for every two bytes of the configuration (conf_general.c:72). */
#define VESC_HOST_MCCONF_BASE 1000u
#define VESC_HOST_MCCONF_VARS (sizeof(mc_configuration_t) / 2u)

/* The motor NTC's filter, hwconf/hw.h:639. Boards that need a faster response override it
 * (the m600 sets 0.001), so it is a board parameter and belongs here rather than in the app. */
#define VESC_HOST_MOTOR_TEMP_LPF 0.01

void vesc_host_make_var_port(motor_config_var_port_t *out, flash_emul_t *emul);

/*
 * COMM_TERMINAL_CMD and COMM_FORWARD_CAN run through the product: the terminal, and the
 * CAN bus. Two callbacks need two targets, so the port carries this small context, which
 * the product fills in.
 */
typedef struct vesc_host_ops_ctx {
    vesc_terminal_app_t *term;
    vesc_can_app_t *can;
} vesc_host_ops_ctx_t;
void vesc_host_make_ops_port(vesc_comm_ops_port_t *out, vesc_host_ops_ctx_t *ctx);
void vesc_host_make_inverter_port(foc_inverter_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_current_port(foc_current_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_rotor_port(foc_rotor_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_ppm_port(ppm_receiver_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_adc_port(adc_input_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_app_status_port(vesc_app_status_port_t *out, vesc_host_glue_state_t *state);

/*
 * One sampling pass over the two NTCs, as the reference's ADC interrupt handler does it
 * (mc_interface.c:2266 for the FET, :2325-2331 for the motor): the motor reading is replaced
 * when it is not a plausible number, then each is low-passed - 0.1 for the FET, the board's
 * MOTOR_TEMP_LPF (0.01) for the motor - and the filtered values are handed to the FOC, which
 * caches them as the reference's motor state does. Those constants are board defines in the
 * reference (hwconf/hw.h), which is why the sampler lives in the product and not in the app.
 */
void vesc_host_sample_temperatures(vesc_host_glue_state_t *state, foc_core_t *foc);
void vesc_host_make_can_port(vesc_can_port_t *out, vesc_host_glue_state_t *state);
/*
 * Motor identification's port, wired to the FOC aggregate: the procedures drive the motor through
 * it and read the sample accumulator the control loop fills.
 */
void vesc_host_make_motor_id_measure_port(motor_id_measure_port_t *out, foc_core_t *foc);
void vesc_host_make_nunchuk_port(nunchuk_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_pas_port(pas_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_balance_port(balance_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_terminal_stream_port(terminal_stream_port_t *out,
                                         vesc_host_glue_state_t *state);
void vesc_host_make_terminal_system_port(terminal_system_port_t *out, foc_core_t *foc);
void vesc_host_make_bms_can_port(bms_can_port_t *out, vesc_host_glue_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* PRODUCT_VESC_HOST_GLUE_H */
