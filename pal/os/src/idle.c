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

void edge_os_idle_wait(const edge_pal_port_t *pal, edge_os_pending_fn pending, void *ctx) {
    if (pal == NULL)
        return;
    if (pal->critical_enter != NULL)
        pal->critical_enter(pal->self);
    if ((pending == NULL || !pending(ctx)) && pal->idle != NULL)
        pal->idle(pal->self);
    if (pal->critical_exit != NULL)
        pal->critical_exit(pal->self);
}
