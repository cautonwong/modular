#ifndef PRODUCT_VESC_HOST_GLUE_H
#define PRODUCT_VESC_HOST_GLUE_H

#include "foc_core/foc_core.h"
#include "foc_core/foc_math.h"
#include "motor_config/motor_config.h"
#include "vesc_comm/vesc_comm.h"

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
} vesc_host_glue_state_t;

void vesc_host_make_storage_port(motor_config_storage_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_stream_tx_port(edge_stream_tx_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_motor_provider_port(vesc_motor_provider_port_t *out, foc_core_t *foc);
void vesc_host_make_inverter_port(foc_inverter_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_current_port(foc_current_port_t *out, vesc_host_glue_state_t *state);
void vesc_host_make_rotor_port(foc_rotor_port_t *out, vesc_host_glue_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* PRODUCT_VESC_HOST_GLUE_H */
