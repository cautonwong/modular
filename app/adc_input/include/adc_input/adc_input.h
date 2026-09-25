#ifndef ADC_INPUT_H
#define ADC_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"
#include "edge/modules.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum adc_control_mode {
    ADC_MODE_OFF = 0,
    ADC_MODE_DUTY,
    ADC_MODE_CURRENT,
    ADC_MODE_CURRENT_NOREV,
    ADC_MODE_CURRENT_NOREV_BRAKE,
    ADC_MODE_RPM
} adc_control_mode_t;

typedef struct adc_input_config {
    adc_control_mode_t mode;
    float voltage_min;
    float voltage_max;
    float voltage_start;
    float voltage_end;
    float voltage_center;
    float deadband;
    bool use_brake_input;
    float brake_start;
    float brake_end;
    bool safe_start;
} adc_input_config_t;

typedef struct adc_input_port {
    void *self;
    edge_status_t (*read_throttle_v)(void *self, float *voltage);
    edge_status_t (*read_brake_v)(void *self, float *voltage);
    bool (*read_button)(void *self, uint8_t button_idx);
} adc_input_port_t;

typedef struct adc_input_app {
    edge_module_t module;
    adc_input_config_t config;
    adc_input_port_t port;
    float throttle_norm; /* -1.0 to 1.0 (or 0.0 to 1.0) */
    float brake_norm;    /* 0.0 to 1.0 */
    float throttle_v;    /* last measured throttle input voltage */
    float brake_v;       /* last measured brake input voltage */
    bool fault_wire_disconnected;
    bool safe_start_unlocked;
} adc_input_app_t;

void adc_input_construct(adc_input_app_t *app, uint32_t module_id, uint32_t priority,
                         const adc_input_config_t *config, const adc_input_port_t *port);
edge_status_t adc_input_init(adc_input_app_t *app);
edge_status_t adc_input_update(adc_input_app_t *app);
float adc_input_get_throttle(const adc_input_app_t *app);
float adc_input_get_brake(const adc_input_app_t *app);
/* Last measured input voltages. The reference reports these alongside the decoded
 * levels in COMM_GET_DECODED_ADC, so they have to be kept, not just consumed. */
float adc_input_get_throttle_v(const adc_input_app_t *app);
float adc_input_get_brake_v(const adc_input_app_t *app);
bool adc_input_has_fault(const adc_input_app_t *app);
edge_module_t *adc_input_module(adc_input_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* ADC_INPUT_H */
