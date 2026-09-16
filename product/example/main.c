#include "dlt645.h"
#include "edge/sys.h"
#include "flash.h"
#include <stddef.h>
#include <stdint.h>

static int storage_read(void *self, uint32_t key, void *buf, size_t len) {
    return flash_read(self, key, buf, len);
}

static int storage_write(void *self, uint32_t key, const void *buf, size_t len) {
    return flash_write(self, key, buf, len);
}

int main(void) {
    static dlt645_t dlt645;
    static const dlt645_storage_if storage = {
        .read = storage_read,
        .write = storage_write,
        .self = NULL,
    };
    dlt645_t *app = dlt645_new(&dlt645, &storage);
    if (!app) return 1;

    edge_module_t *apps[] = { &app->mod };
    edge_sys_t sys;
    if (edge_sys_init(&sys, apps, 1u) != EDGE_OK) return 2;

    for (;;) {
        if (edge_sys_run_once(&sys) != EDGE_OK) return 3;
    }
}
