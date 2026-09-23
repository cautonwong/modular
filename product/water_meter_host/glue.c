#include "flash/flash.h"
#include "pulse_meter/pulse_meter.h"

#include <stddef.h>
#include <stdint.h>

// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t storage_read(void *self, uint32_t *pulses, uint32_t *tamper_count) {
    if (self == NULL || pulses == NULL || tamper_count == NULL)
        return EDGE_EINVAL;
    uint32_t data[2] = {0};
    const edge_status_t rc = flash_read(self, 0u, data, sizeof(data));
    if (rc == EDGE_OK) {
        *pulses = data[0];
        *tamper_count = data[1];
    }
    return rc;
}

static edge_status_t storage_write(void *self, uint32_t pulses, uint32_t tamper_count) {
    if (self == NULL)
        return EDGE_EINVAL;
    const uint32_t data[2] = {pulses, tamper_count};
    return flash_write(self, 0u, data, sizeof(data));
}

void product_water_meter_host_make_storage(pulse_meter_storage_t *out, void *flash_state) {
    if (out == NULL)
        return;
    *out = (pulse_meter_storage_t){
        .read = storage_read,
        .write = storage_write,
        .self = flash_state,
    };
}
