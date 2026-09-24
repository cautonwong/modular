#include "dlt645/dlt645.h"
#include "flash/flash.h"
#include "gpio/gpio.h"
#include "relay/relay.h"
#include "storage.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void product_meter_host_make_storage(dlt645_storage_if_t *out, void *flash_state) {
    product_storage_wire(out, flash_state);
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
