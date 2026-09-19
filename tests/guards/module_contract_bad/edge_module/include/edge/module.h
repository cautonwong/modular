#ifndef EDGE_MODULE_H
#define EDGE_MODULE_H

/* Negative fixture: init/deinit must not be in the struct (D51). */

typedef int edge_status_t;
typedef struct edge_event edge_event_t;

typedef edge_status_t (*edge_module_init_fn)(void *self);
typedef edge_status_t (*edge_module_deinit_fn)(void *self);
typedef edge_status_t (*edge_module_poll_fn)(void *self);
typedef edge_status_t (*edge_module_event_fn)(void *self, const edge_event_t *event);
typedef edge_status_t (*edge_module_power_off_fn)(void *self);
typedef edge_status_t (*edge_module_suspend_fn)(void *self);
typedef edge_status_t (*edge_module_resume_fn)(void *self);

typedef struct edge_module {
    unsigned module_id;
    edge_module_init_fn init;
    edge_module_poll_fn poll;
    edge_module_event_fn on_event;
    edge_module_power_off_fn power_off;
    edge_module_deinit_fn deinit;
    edge_module_suspend_fn suspend;
    edge_module_resume_fn resume;
} edge_module_t;

#endif
