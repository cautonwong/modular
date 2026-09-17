#include "pal_os/idle.h"

#include <stddef.h>

void edge_os_idle_hook(void *ctx) {
    edge_os_idle_t *idle = (edge_os_idle_t *)ctx;
    if (idle == NULL || idle->os == NULL)
        return;
    if (idle->os->yield != NULL)
        idle->os->yield(idle->os->self);
    if (idle->sleep_ms != 0u && idle->os->sleep_ms != NULL)
        idle->os->sleep_ms(idle->os->self, idle->sleep_ms);
}
