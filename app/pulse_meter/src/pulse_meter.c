#include "pulse_meter/pulse_meter.h"
#include "edge/event.h"
#include "edge/events.h"

#include <stddef.h>

static edge_status_t pulse_meter_poll(edge_module_t *module) {
    if (module == NULL || module->private_data == NULL)
        return EDGE_EINVAL;

    pulse_meter_t *meter = (pulse_meter_t *)module->private_data;

    /* Periodically sample battery voltage */
    if (meter->battery.read_voltage_mv != NULL) {
        uint32_t mv = 0u;
        const edge_status_t rc = meter->battery.read_voltage_mv(meter->battery.self, &mv);
        if (rc == EDGE_OK) {
            meter->last_battery_mv = mv;
            if (mv < meter->config.low_battery_mv && !meter->low_battery_warned) {
                meter->low_battery_warned = true;
            }
        }
    }

    return EDGE_OK;
}

static edge_status_t pulse_meter_on_event(edge_module_t *module, const edge_event_t *event) {
    if (module == NULL || module->private_data == NULL || event == NULL)
        return EDGE_EINVAL;

    pulse_meter_t *meter = (pulse_meter_t *)module->private_data;

    if (event->id == EDGE_EVT_PULSE_COUNT) {
        /* arg0 bit 0 = reverse flag */
        const bool reverse = (event->arg0 & 0x01u) != 0u;
        return pulse_meter_on_pulse(meter, event->timestamp, reverse);
    }

    if (event->id == EDGE_EVT_TAMPER_DETECTED) {
        return pulse_meter_on_tamper(meter, event->timestamp);
    }

    return EDGE_OK;
}

static edge_status_t pulse_meter_power_off(edge_module_t *module) {
    if (module == NULL || module->private_data == NULL)
        return EDGE_EINVAL;

    pulse_meter_t *meter = (pulse_meter_t *)module->private_data;
    if (meter->storage.write != NULL) {
        (void)meter->storage.write(meter->storage.self, (uint32_t)meter->total_pulses,
                                   meter->tamper_events);
    }
    return EDGE_OK;
}

void pulse_meter_construct(pulse_meter_t *self, uint32_t module_id, uint32_t priority,
                           const pulse_meter_config_t *config, const pulse_meter_storage_t *storage,
                           const pulse_meter_battery_t *battery) {
    if (self == NULL)
        return;

    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 5000u, /* Long periodic battery check interval (5s / 1hr in field) */
        .budget = 100u,
        .poll = pulse_meter_poll,
        .on_event = pulse_meter_on_event,
        .power_off = pulse_meter_power_off,
        .suspend = NULL,
        .resume = NULL,
        .private_data = self,
    };

    self->config = config ? *config : (pulse_meter_config_t){0};
    self->storage = storage ? *storage : (pulse_meter_storage_t){0};
    self->battery = battery ? *battery : (pulse_meter_battery_t){0};

    self->total_pulses = 0u;
    self->tamper_events = 0u;
    self->last_pulse_tick = 0u;
    self->last_battery_mv = 3300u;
    self->low_battery_warned = false;
    self->tamper_detected = false;
}

edge_status_t pulse_meter_init(pulse_meter_t *self) {
    if (self == NULL)
        return EDGE_EINVAL;

    /* Restore persisted pulse counter from storage if available */
    if (self->storage.read != NULL) {
        uint32_t saved_pulses = 0u;
        uint32_t saved_tamper = 0u;
        if (self->storage.read(self->storage.self, &saved_pulses, &saved_tamper) == EDGE_OK) {
            self->total_pulses = saved_pulses;
            self->tamper_events = saved_tamper;
        }
    }

    return EDGE_OK;
}

edge_status_t pulse_meter_deinit(pulse_meter_t *self) {
    if (self == NULL)
        return EDGE_EINVAL;
    return EDGE_OK;
}

edge_module_t *pulse_meter_module(pulse_meter_t *self) {
    if (self == NULL)
        return NULL;
    return &self->module;
}

edge_status_t pulse_meter_on_pulse(pulse_meter_t *meter, uint64_t now_tick, bool reverse) {
    if (meter == NULL)
        return EDGE_EINVAL;

    /* Debouncing: ignore pulses that arrive faster than minimum debounce interval */
    if (meter->last_pulse_tick != 0u && meter->config.debounce_ticks > 0u &&
        now_tick >= meter->last_pulse_tick &&
        (now_tick - meter->last_pulse_tick) < meter->config.debounce_ticks) {
        return EDGE_OK; /* Debounced and dropped */
    }

    meter->last_pulse_tick = now_tick;

    if (!reverse) {
        ++meter->total_pulses;
    } else {
        meter->total_pulses = (meter->total_pulses > 0u) ? meter->total_pulses - 1u : 0u;
        ++meter->tamper_events;
    }

    return EDGE_OK;
}

edge_status_t pulse_meter_on_tamper(pulse_meter_t *meter, uint64_t now_tick) {
    if (meter == NULL)
        return EDGE_EINVAL;

    (void)now_tick;
    meter->tamper_detected = true;
    ++meter->tamper_events;

    /* Persist tamper alert state immediately */
    if (meter->storage.write != NULL) {
        (void)meter->storage.write(meter->storage.self, (uint32_t)meter->total_pulses,
                                   meter->tamper_events);
    }

    return EDGE_OK;
}

edge_status_t pulse_meter_freeze_snapshot(pulse_meter_t *meter, uint64_t *out_volume) {
    if (meter == NULL)
        return EDGE_EINVAL;

    if (out_volume != NULL)
        *out_volume = meter->total_pulses;

    /* Persist current volume to non-volatile storage */
    if (meter->storage.write != NULL) {
        (void)meter->storage.write(meter->storage.self, (uint32_t)meter->total_pulses,
                                   meter->tamper_events);
    }

    return EDGE_OK;
}

uint64_t pulse_meter_total_pulses(const pulse_meter_t *meter) {
    if (meter == NULL)
        return 0u;
    return meter->total_pulses;
}
