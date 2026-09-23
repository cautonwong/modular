#ifndef APP_PULSE_METER_H
#define APP_PULSE_METER_H

#include "edge/errors.h"
#include "edge/module.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Low-Power Pulse Meter (Water meter / Wearable step & pulse counter domain).
 *
 * Characteristics:
 * - Ultra low duty-cycle: Event/GPIO interrupt driven, zero active polling in sleep.
 * - Anti-jitter / debouncing: Ignores rapid bouncing on mechanical reed switches.
 * - Magnetic / wire-cut tamper detection.
 * - Periodic battery monitoring and daily freeze snapshot.
 * - Retentive non-volatile storage contract (zero dynamic memory allocation).
 */

typedef struct pulse_meter_config {
    uint32_t pulses_per_unit; /* Pulses per volume unit (e.g. 100 per liter) */
    uint32_t debounce_ticks;  /* Minimum tick separation between valid pulses */
    uint32_t low_battery_mv;  /* Low battery warning threshold in millivolts */
} pulse_meter_config_t;

typedef struct pulse_meter_storage {
    edge_status_t (*read)(void *self, uint32_t *pulses, uint32_t *tamper_count);
    edge_status_t (*write)(void *self, uint32_t pulses, uint32_t tamper_count);
    void *self;
} pulse_meter_storage_t;

typedef struct pulse_meter_battery {
    edge_status_t (*read_voltage_mv)(void *self, uint32_t *voltage_mv);
    void *self;
} pulse_meter_battery_t;

typedef struct pulse_meter {
    edge_module_t module;
    pulse_meter_config_t config;
    pulse_meter_storage_t storage;
    pulse_meter_battery_t battery;
    uint64_t total_pulses;
    uint32_t tamper_events;
    uint64_t last_pulse_tick;
    uint32_t last_battery_mv;
    bool low_battery_warned;
    bool tamper_detected;
} pulse_meter_t;

void pulse_meter_construct(pulse_meter_t *self, uint32_t module_id, uint32_t priority,
                           const pulse_meter_config_t *config, const pulse_meter_storage_t *storage,
                           const pulse_meter_battery_t *battery);

/* D51: init/deinit are called by the composition root, not by the scheduler. */
edge_status_t pulse_meter_init(pulse_meter_t *self);
edge_status_t pulse_meter_deinit(pulse_meter_t *self);
edge_module_t *pulse_meter_module(pulse_meter_t *self);

/* Ingest pulse input with direction (false = forward, true = reverse flow) */
edge_status_t pulse_meter_on_pulse(pulse_meter_t *meter, uint64_t now_tick, bool reverse);

/* Ingest tamper event (e.g. magnetic interference detected) */
edge_status_t pulse_meter_on_tamper(pulse_meter_t *meter, uint64_t now_tick);

/* Generate daily/periodic volume freeze snapshot and persist to storage */
edge_status_t pulse_meter_freeze_snapshot(pulse_meter_t *meter, uint64_t *out_volume);

/* Read current accumulated total pulse count */
uint64_t pulse_meter_total_pulses(const pulse_meter_t *meter);

#ifdef __cplusplus
}
#endif

#endif
