#include "battery_adc/battery_adc.h"
#include <stddef.h>

typedef struct lut_point {
    uint16_t mv;
    uint8_t percent;
} lut_point_t;

static const lut_point_t g_battery_curve[] = {
    {3500u, 0u}, {3616u, 3u}, {3723u, 22u}, {3776u, 48u}, {3979u, 79u}, {4180u, 100u},
};

#define BATTERY_CURVE_POINTS (sizeof(g_battery_curve) / sizeof(g_battery_curve[0]))

void battery_adc_init(battery_adc_state_t *state) {
    if (state == NULL) {
        return;
    }
    state->voltage_mv = 0u;
    state->percent_remaining = 0u;
    state->is_charging = false;
    state->is_power_present = false;
    state->is_full = false;
    state->first_measurement = true;
}

uint16_t battery_adc_raw_to_mv(int16_t raw_adc) {
    if (raw_adc <= 0) {
        return 0u;
    }
    /*
     * Hardware divider = /2, ADC gain = 1/4 -> total gain = 1/8.
     * Reference = 600mV, 10-bit ADC (1024 counts).
     * Voltage (mV) = raw * (8 * 600) / 1024 = raw * 4800 / 1024 = raw * 75 / 16.
     */
    const uint32_t val = (uint32_t)raw_adc * 75u;
    return (uint16_t)(val / 16u);
}

uint8_t battery_adc_mv_to_percent(uint16_t voltage_mv, bool is_charging, bool is_full) {
    if (is_full) {
        return 100u;
    }
    if (voltage_mv <= g_battery_curve[0].mv) {
        return g_battery_curve[0].percent;
    }
    if (voltage_mv >= g_battery_curve[BATTERY_CURVE_POINTS - 1u].mv) {
        return is_charging ? 99u : 100u;
    }

    uint8_t percent = 0u;
    for (size_t i = 0u; i < BATTERY_CURVE_POINTS - 1u; ++i) {
        if (voltage_mv >= g_battery_curve[i].mv && voltage_mv <= g_battery_curve[i + 1u].mv) {
            const uint32_t x0 = g_battery_curve[i].mv;
            const uint32_t x1 = g_battery_curve[i + 1u].mv;
            const uint32_t y0 = g_battery_curve[i].percent;
            const uint32_t y1 = g_battery_curve[i + 1u].percent;

            const uint32_t dx = x1 - x0;
            const uint32_t dy = y1 - y0;
            const uint32_t delta_x = (uint32_t)voltage_mv - x0;

            percent = (uint8_t)(y0 + (delta_x * dy) / dx);
            break;
        }
    }

    if (is_charging && percent > 99u) {
        percent = 99u;
    }
    return percent;
}

edge_status_t battery_adc_update(battery_adc_state_t *state, int16_t raw_adc, bool charging_pin_low,
                                 bool power_present_pin_low) {
    if (state == NULL) {
        return EDGE_EINVAL;
    }

    state->is_charging = charging_pin_low;
    state->is_power_present = power_present_pin_low;

    if (state->is_power_present && !state->is_charging) {
        state->is_full = true;
    } else if (!state->is_power_present) {
        state->is_full = false;
    }

    state->voltage_mv = battery_adc_raw_to_mv(raw_adc);
    const uint8_t new_percent =
        battery_adc_mv_to_percent(state->voltage_mv, state->is_charging, state->is_full);

    if ((state->is_power_present && new_percent > state->percent_remaining) ||
        (!state->is_power_present && new_percent < state->percent_remaining) ||
        state->first_measurement) {
        state->first_measurement = false;
        state->percent_remaining = new_percent;
    }

    return EDGE_OK;
}
