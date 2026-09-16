#include "edge/sys.h"

static void sort_apps(edge_module_t **apps, size_t count)
{
    for (size_t i = 1u; i < count; ++i) {
        edge_module_t *key = apps[i];
        size_t j = i;
        while (j > 0u &&
               (apps[j - 1u]->priority > key->priority ||
                (apps[j - 1u]->priority == key->priority &&
                 apps[j - 1u]->module_id > key->module_id))) {
            apps[j] = apps[j - 1u];
            --j;
        }
        apps[j] = key;
    }
}

int edge_sys_init(edge_sys_t *sys, edge_module_t **apps, size_t count)
{
    if (!sys || (!apps && count != 0u)) {
        return EDGE_EINVAL;
    }
    sys->apps = apps;
    sys->app_count = count;
    sys->subscriptions = NULL;
    sys->subscription_count = 0u;
    sys->subscription_capacity = 0u;
    sys->events = NULL;
    sys->tick = 0u;

    for (size_t i = 0u; i < count; ++i) {
        if (!apps[i]) {
            return EDGE_EINVAL;
        }
    }

    sort_apps(sys->apps, sys->app_count);
    for (size_t i = 0u; i < count; ++i) {
        if (sys->apps[i]->init) {
            const int rc = sys->apps[i]->init(sys->apps[i]);
            if (rc < 0) {
                return rc;
            }
        }
    }
    return EDGE_OK;
}

int edge_sys_bind_event_queue(edge_sys_t *sys, edge_event_queue_t *queue,
                              edge_sys_subscription_t *subscriptions,
                              size_t capacity)
{
    if (!sys || !queue || (!subscriptions && capacity != 0u)) {
        return EDGE_EINVAL;
    }
    sys->events = queue;
    sys->subscriptions = subscriptions;
    sys->subscription_count = 0u;
    sys->subscription_capacity = capacity;
    return EDGE_OK;
}

int edge_sys_subscribe(edge_sys_t *sys, uint32_t event_id, edge_module_t *app)
{
    if (!sys || !app || !sys->subscriptions) {
        return EDGE_EINVAL;
    }
    if (sys->subscription_count >= sys->subscription_capacity) {
        return EDGE_ENOSPC;
    }
    for (size_t i = 0u; i < sys->subscription_count; ++i) {
        if (sys->subscriptions[i].event_id == event_id &&
            sys->subscriptions[i].app == app) {
            return EDGE_EBUSY;
        }
    }
    sys->subscriptions[sys->subscription_count++] =
        (edge_sys_subscription_t){ event_id, app };
    return EDGE_OK;
}

int edge_sys_dispatch_events(edge_sys_t *sys)
{
    if (!sys || !sys->events) {
        return EDGE_EINVAL;
    }

    edge_event_t event;
    for (;;) {
        const int pop_rc = edge_event_pop(sys->events, &event);
        if (pop_rc == EDGE_ENOENT) {
            return EDGE_OK;
        }
        if (pop_rc < 0) {
            return pop_rc;
        }

        for (size_t i = 0u; i < sys->subscription_count; ++i) {
            const edge_sys_subscription_t *sub = &sys->subscriptions[i];
            if (sub->event_id != event.id || !sub->app || !sub->app->on_event) {
                continue;
            }
            const int rc = sub->app->on_event(sub->app, &event);
            if (rc < 0) {
                return rc;
            }
        }
    }
}

int edge_sys_validate_required(const edge_sys_t *sys,
                               const uint32_t *required_ids,
                               size_t required_count)
{
    if (!sys || (!required_ids && required_count != 0u)) {
        return EDGE_EINVAL;
    }
    for (size_t r = 0u; r < required_count; ++r) {
        int found = 0;
        for (size_t i = 0u; i < sys->app_count; ++i) {
            if (sys->apps[i]->module_id == required_ids[r]) {
                found = 1;
                break;
            }
        }
        if (!found) {
            return EDGE_EDEPEND;
        }
    }
    return EDGE_OK;
}

int edge_sys_run_once(edge_sys_t *sys)
{
    if (!sys) {
        return EDGE_EINVAL;
    }
    if (sys->events) {
        const int event_rc = edge_sys_dispatch_events(sys);
        if (event_rc < 0) {
            return event_rc;
        }
    }
    for (size_t i = 0u; i < sys->app_count; ++i) {
        edge_module_t *app = sys->apps[i];
        if (app->poll) {
            const int rc = app->poll(app);
            if (rc < 0) {
                return rc;
            }
        }
    }
    ++sys->tick;
    return EDGE_OK;
}

int edge_sys_power_off(edge_sys_t *sys)
{
    if (!sys) {
        return EDGE_EINVAL;
    }
    for (size_t i = sys->app_count; i > 0u; --i) {
        edge_module_t *app = sys->apps[i - 1u];
        if (app->power_off) {
            const int rc = app->power_off(app);
            if (rc < 0) {
                return rc;
            }
        }
    }
    return EDGE_OK;
}
