#ifndef PRODUCT_VESC_HOST_GLUE_H
#define PRODUCT_VESC_HOST_GLUE_H

#include "adc_input/adc_input.h"
#include "balance/balance.h"
#include "foc_core/foc_core.h"
#include "foc_core/foc_math.h"
#include "motor_config/motor_config.h"
#include "motor_id/motor_id.h"
#include "nunchuk/nunchuk.h"
#include "pas/pas.h"
#include "ppm/ppm.h"
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
    uint8_t flash_mem[1024];
    uint8_t stream_tx_buf[512];
    size_t stream_tx_len;
    float ppm_pulse_us;
    float adc_throttle_v;
    float adc_brake_v;
    uint32_t last_can_id;
    uint8_t last_can_data[8];
    uint8_t last_can_len;
    char terminal_tx_buf[256];
} vesc_host_glue_state_t;

void vesc_host_make_storage_port(motor_config_storage_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_stream_tx_port(edge_stream_tx_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_motor_provider_port(vesc_motor_provider_port_t *out, foc_core_t *foc);
void vesc_host_make_inverter_port(foc_inverter_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_current_port(foc_current_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_rotor_port(foc_rotor_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_ppm_port(ppm_receiver_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_adc_port(adc_input_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_can_port(vesc_can_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_motor_id_measure_port(motor_id_measure_port_t *out,
                                          vesc_host_glue_state_t *state);
void vesc_host_make_motor_id_control_port(motor_id_control_port_t *out,
                                          vesc_host_glue_state_t *state);
void vesc_host_make_nunchuk_port(nunchuk_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_pas_port(pas_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_balance_port(balance_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_terminal_stream_port(terminal_stream_port_t *out,
                                         vesc_host_glue_state_t *state);
void vesc_host_make_terminal_system_port(terminal_system_port_t *out, foc_core_t *foc);

#ifdef __cplusplus
}
#endif

#endif /* PRODUCT_VESC_HOST_GLUE_H */
