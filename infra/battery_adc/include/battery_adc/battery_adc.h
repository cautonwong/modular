#ifndef INFRA_BATTERY_ADC_H
#define INFRA_BATTERY_ADC_H

#include "edge/module.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct battery_adc_state {
    uint16_t voltage_mv;
    uint8_t percent_remaining;
    bool is_charging;
    bool is_power_present;
    bool is_full;
    bool first_measurement;
} battery_adc_state_t;

void battery_adc_init(battery_adc_state_t *state);
uint16_t battery_adc_raw_to_mv(int16_t raw_adc);
uint8_t battery_adc_mv_to_percent(uint16_t voltage_mv, bool is_charging, bool is_full);
edge_status_t battery_adc_update(battery_adc_state_t *state, int16_t raw_adc, bool charging_pin_low,
                                 bool power_present_pin_low);

#ifdef __cplusplus
}
#endif

#endif
