#include "example/sys.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static void sort_apps(edge_module_t **apps, size_t count) {
    for (size_t i = 1u; i < count; ++i) {
        edge_module_t *key = apps[i];
        size_t j = i;
        while (j > 0u && (apps[j - 1u]->priority > key->priority ||
                          (apps[j - 1u]->priority == key->priority &&
                           apps[j - 1u]->module_id > key->module_id))) {
            apps[j] = apps[j - 1u];
            --j;
        }
        apps[j] = key;
    }
}

static edge_status_t validate_apps(edge_module_t **apps, size_t count) {
    if (apps == NULL && count != 0u)
        return EDGE_EINVAL;
    for (size_t i = 0u; i < count; ++i) {
        if (apps[i] == NULL || apps[i]->module_id == 0u)
            return EDGE_EINVAL;
        for (size_t j = 0u; j < i; ++j) {
            if (apps[i]->module_id == apps[j]->module_id)
                return EDGE_EBUSY;
        }
    }
    return EDGE_OK;
}

static uint64_t now_ticks(edge_sys_t *sys) {
    if (sys->clock != NULL && sys->clock->monotonic_ticks != NULL)
        return sys->clock->monotonic_ticks(sys->clock->self);
    return sys->tick;
}

static bool tick_due(uint64_t now, uint64_t due) {
    return (int64_t)(now - due) >= 0;
}

edge_status_t edge_sys_init(edge_sys_t *sys, edge_module_t **apps, size_t count) {
    if (sys == NULL)
        return EDGE_EINVAL;
    const edge_status_t rc = validate_apps(apps, count);
    if (rc < 0)
        return rc;
    sys->apps = apps;
    sys->app_count = count;
    sys->subscriptions = NULL;
    sys->subscription_count = 0u;
    sys->subscription_capacity = 0u;
    sys->required_ids = NULL;
    sys->required_count = 0u;
    sys->events = NULL;
    sys->clock = NULL;
    sys->max_events_per_run = 8u;
    sys->tick = 0u;
    sys->stats = (edge_sys_stats_t){0};
    sys->state = EDGE_SYS_CONSTRUCTED;
    for (size_t i = 0u; i < count; ++i) {
        apps[i]->initialized = 0u;
        apps[i]->running = 0u;
        apps[i]->failed = 0u;
        apps[i]->next_due = 0u;
    }
    sort_apps(apps, count);
    return EDGE_OK;
}

edge_status_t edge_sys_bind_event_queue(edge_sys_t *sys, edge_event_queue_t *queue,
                                        edge_sys_subscription_t *subscriptions, size_t capacity) {
    if (sys == NULL || queue == NULL || (subscriptions == NULL && capacity != 0u))
        return EDGE_EINVAL;
    if (sys->state != EDGE_SYS_CONSTRUCTED)
        return EDGE_ESTATE;
    sys->events = queue;
    sys->subscriptions = subscriptions;
    sys->subscription_count = 0u;
    sys->subscription_capacity = capacity;
    return EDGE_OK;
}

edge_status_t edge_sys_set_clock(edge_sys_t *sys, const edge_clock_port_t *clock) {
    if (sys == NULL)
        return EDGE_EINVAL;
    if (sys->state != EDGE_SYS_CONSTRUCTED)
        return EDGE_ESTATE;
    sys->clock = clock;
    return EDGE_OK;
}

edge_status_t edge_sys_set_event_budget(edge_sys_t *sys, uint32_t max_events_per_run) {
    if (sys == NULL || max_events_per_run == 0u)
        return EDGE_EINVAL;
    if (sys->state != EDGE_SYS_CONSTRUCTED)
        return EDGE_ESTATE;
    sys->max_events_per_run = max_events_per_run;
    return EDGE_OK;
}

edge_status_t edge_sys_set_required(edge_sys_t *sys, const uint32_t *ids, size_t count) {
    if (sys == NULL || (ids == NULL && count != 0u))
        return EDGE_EINVAL;
    if (sys->state != EDGE_SYS_CONSTRUCTED)
        return EDGE_ESTATE;
    sys->required_ids = ids;
    sys->required_count = count;
    return EDGE_OK;
}

edge_status_t edge_sys_subscribe(edge_sys_t *sys, uint32_t event_id, edge_module_t *app) {
    if (sys == NULL || app == NULL || sys->subscriptions == NULL || event_id == 0u)
        return EDGE_EINVAL;
    if (sys->state != EDGE_SYS_CONSTRUCTED)
        return EDGE_ESTATE;
    if (sys->subscription_count >= sys->subscription_capacity)
        return EDGE_ENOSPC;
    for (size_t i = 0u; i < sys->subscription_count; ++i) {
        if (sys->subscriptions[i].event_id == event_id && sys->subscriptions[i].app == app)
            return EDGE_EBUSY;
    }
    sys->subscriptions[sys->subscription_count++] = (edge_sys_subscription_t){event_id, app};
    return EDGE_OK;
}

edge_status_t edge_sys_validate_required(const edge_sys_t *sys) {
    if (sys == NULL)
        return EDGE_EINVAL;
    for (size_t r = 0u; r < sys->required_count; ++r) {
        size_t matches = 0u;
        for (size_t i = 0u; i < sys->app_count; ++i) {
            if (sys->apps[i]->module_id == sys->required_ids[r])
                ++matches;
        }
        if (matches != 1u)
            return EDGE_EDEPEND;
    }
    return EDGE_OK;
}

edge_status_t edge_sys_start(edge_sys_t *sys) {
    if (sys == NULL || sys->state != EDGE_SYS_CONSTRUCTED)
        return EDGE_ESTATE;
    edge_status_t rc = edge_sys_validate_required(sys);
    if (rc < 0)
        return rc;

    size_t started = 0u;
    for (; started < sys->app_count; ++started) {
        edge_module_t *app = sys->apps[started];
        if (app->init != NULL) {
            rc = app->init(app);
            if (rc < 0) {
                app->failed = 1u;
                ++sys->stats.errors;
                ++sys->stats.isolated;
                if (app->initialized && app->deinit != NULL)
                    (void)app->deinit(app);
                while (started > 0u) {
                    --started;
                    app = sys->apps[started];
                    if (app->initialized && app->deinit != NULL)
                        (void)app->deinit(app);
                    app->initialized = 0u;
                    app->running = 0u;
                }
                sys->state = EDGE_SYS_FAILED;
                return rc;
            }
        }
        app->initialized = 1u;
        app->next_due = now_ticks(sys) + app->period;
    }
    for (size_t i = 0u; i < sys->app_count; ++i)
        sys->apps[i]->running = 1u;
    sys->state = EDGE_SYS_RUNNING;
    return EDGE_OK;
}

edge_status_t edge_sys_dispatch_events(edge_sys_t *sys) {
    if (sys == NULL || sys->events == NULL)
        return EDGE_EINVAL;
    if (sys->state != EDGE_SYS_RUNNING)
        return EDGE_ESTATE;

    edge_status_t first_error = EDGE_OK;
    uint32_t handled = 0u;
    while (handled < sys->max_events_per_run) {
        edge_event_t event;
        const edge_status_t pop_rc = edge_event_pop(sys->events, &event);
        if (pop_rc == EDGE_ENOENT)
            break;
        if (pop_rc < 0) {
            ++sys->stats.errors;
            if (first_error == EDGE_OK)
                first_error = pop_rc;
            break;
        }
        ++handled;
        ++sys->stats.events;
        for (size_t i = 0u; i < sys->subscription_count; ++i) {
            edge_sys_subscription_t *sub = &sys->subscriptions[i];
            if (sub->event_id == event.id && sub->app != NULL && sub->app->on_event != NULL &&
                !sub->app->failed) {
                const edge_status_t rc = sub->app->on_event(sub->app, &event);
                if (rc < 0) {
                    sub->app->failed = 1u;
                    ++sys->stats.errors;
                    ++sys->stats.isolated;
                    if (first_error == EDGE_OK)
                        first_error = rc;
                }
            }
        }
    }
    return first_error;
}

edge_status_t edge_sys_run_once(edge_sys_t *sys) {
    if (sys == NULL || sys->state != EDGE_SYS_RUNNING)
        return EDGE_ESTATE;

    edge_status_t first_error = EDGE_OK;
    if (sys->events != NULL) {
        const edge_status_t rc = edge_sys_dispatch_events(sys);
        if (rc < 0)
            first_error = rc;
    }

    const uint64_t now = now_ticks(sys);
    for (size_t i = 0u; i < sys->app_count; ++i) {
        edge_module_t *app = sys->apps[i];
        if (app->failed || app->poll == NULL || app->period == 0u)
            continue;
        if (!tick_due(now, app->next_due))
            continue;

        const uint64_t start = now_ticks(sys);
        const edge_status_t rc = app->poll(app);
        const uint64_t elapsed = now_ticks(sys) - start;
        ++sys->stats.polls;
        app->next_due += app->period;
        if (app->budget != 0u && elapsed > app->budget) {
            ++sys->stats.budget_hits;
            if (first_error == EDGE_OK)
                first_error = EDGE_EOVERFLOW;
        }
        if (rc < 0) {
            app->failed = 1u;
            ++sys->stats.errors;
            ++sys->stats.isolated;
            if (first_error == EDGE_OK)
                first_error = rc;
        }
    }

    ++sys->tick;
    return first_error;
}

edge_status_t edge_sys_power_off(edge_sys_t *sys) {
    if (sys == NULL || (sys->state != EDGE_SYS_RUNNING && sys->state != EDGE_SYS_FAILED))
        return EDGE_ESTATE;
    edge_status_t first_error = EDGE_OK;
    for (size_t i = sys->app_count; i > 0u; --i) {
        edge_module_t *app = sys->apps[i - 1u];
        if (app->running && app->power_off != NULL) {
            const edge_status_t rc = app->power_off(app);
            if (rc < 0) {
                app->failed = 1u;
                ++sys->stats.errors;
                if (first_error == EDGE_OK)
                    first_error = rc;
            }
        }
        app->running = 0u;
    }
    sys->state = EDGE_SYS_STOPPED;
    return first_error;
}

edge_status_t edge_sys_deinit(edge_sys_t *sys) {
    if (sys == NULL || sys->state == EDGE_SYS_RUNNING)
        return EDGE_ESTATE;
    edge_status_t first_error = EDGE_OK;
    for (size_t i = sys->app_count; i > 0u; --i) {
        edge_module_t *app = sys->apps[i - 1u];
        if (app->initialized && app->deinit != NULL) {
            const edge_status_t rc = app->deinit(app);
            if (rc < 0 && first_error == EDGE_OK)
                first_error = rc;
        }
        app->initialized = 0u;
        app->running = 0u;
    }
    sys->state = EDGE_SYS_STOPPED;
    return first_error;
}

edge_status_t edge_sys_stats_get(const edge_sys_t *sys, edge_sys_stats_t *out) {
    if (sys == NULL || out == NULL)
        return EDGE_EINVAL;
    *out = sys->stats;
    if (sys->events != NULL)
        out->drops = edge_event_dropped(sys->events);
    return EDGE_OK;
}
