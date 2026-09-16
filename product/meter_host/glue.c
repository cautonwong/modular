#include "dlt645/dlt645.h"
#include "flash/flash.h"
#include "gpio/gpio.h"
#include "relay/relay.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t storage_read(void *self, uint32_t key, void *buf, size_t len) {
    return flash_read(self, key, buf, len);
}

static edge_status_t storage_write(void *self, uint32_t key, const void *buf, size_t len) {
    return flash_write(self, key, buf, len);
}

void product_meter_host_make_storage(dlt645_storage_if_t *out, void *flash_state) {
    if (out == NULL)
        return;
    *out = (dlt645_storage_if_t){
        .read = storage_read,
        .write = storage_write,
        .self = flash_state,
    };
}

// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t relay_gpio_set(void *self, uint8_t channel, bool on) {
    return gpio_write(self, channel, on);
}

void product_meter_host_make_relay_out(relay_out_if_t *out, void *gpio_state) {
    if (out == NULL)
        return;
    *out = (relay_out_if_t){
        .set = relay_gpio_set,
        .self = gpio_state,
    };
}
