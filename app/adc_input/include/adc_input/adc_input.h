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
    /* applications/app_adc.c:200-204: whether the filter's value is used rather than the reading.
     */
    bool use_filter;
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
    /* applications/app_adc.c:70's flag: the verdict of the last reading's own range check. */
    bool range_ok;
    /* applications/app_adc.c:66, the detaching mode, and the two values it substitutes. */
    int adc_detached;
    float throttle_override;
    float brake_override;
    /* applications/app_adc.c:198: the filter's own state, one value approximated over
     * FILTER_SAMPLES. */
    float throttle_filter;
    float brake_filter;
    /* applications/app_adc.c:67 and :63: the buttons, and whether the serial port's pins are a
     * pair. */
    bool buttons_detached;
    bool use_rx_tx_as_buttons;
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

/*
 * applications/app_adc.c:207, app_adc_range_ok: whether the last voltage read is inside the range
 * the configuration gives, both ends included - the reference's own comparison, and the flag its
 * own getter hands out.
 */
bool adc_input_range_ok(const adc_input_app_t *app);

/*
 * applications/app_adc.c:126, app_adc_detach_adc, and the two overrides it makes use of: detaching
 * mode 1 substitutes both channels, 2 the throttle alone, 3 the brake alone, and 0 neither, and an
 * override is truncated into [0, 3.3] volts the way the reference truncates it.
 */
void adc_input_detach(adc_input_app_t *app, int mode);
int adc_input_get_detach(const adc_input_app_t *app);
void adc_input_override_throttle(adc_input_app_t *app, float val);
void adc_input_override_brake(adc_input_app_t *app, float val);

/*
 * applications/app_adc.c:148, app_adc_detach_buttons, and the start flag beside it: while the
 * buttons are detached the serial port's pins are never the buttons, whatever the caller asked for.
 * Whether the pins are actually taken is the product's, since that is a pin mode and not
 * arithmetic.
 */
void adc_input_detach_buttons(adc_input_app_t *app, bool state);
bool adc_input_buttons_detached(const adc_input_app_t *app);
void adc_input_set_rx_tx_as_buttons(adc_input_app_t *app, bool use_rx_tx);
bool adc_input_rx_tx_as_buttons(const adc_input_app_t *app);
edge_module_t *adc_input_module(adc_input_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* ADC_INPUT_H */
