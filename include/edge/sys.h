#ifndef EDGE_SYS_H
#define EDGE_SYS_H

#include "module.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    edge_module_t **apps;
    size_t app_count;
    uint32_t tick;
} edge_sys_t;

int edge_sys_init(edge_sys_t *sys, edge_module_t **apps, size_t count);
int edge_sys_run_once(edge_sys_t *sys);
int edge_sys_power_off(edge_sys_t *sys);

#endif
