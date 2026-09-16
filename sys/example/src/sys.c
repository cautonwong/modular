#include "example/sys.h"

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

static edge_status_t validate_apps(edge_module_t **apps, size_t count)
{
    if (apps == NULL && count != 0u) return EDGE_EINVAL;
    for (size_t i = 0u; i < count; ++i) {
        if (apps[i] == NULL || apps[i]->module_id == 0u) return EDGE_EINVAL;
        for (size_t j = 0u; j < i; ++j) {
            if (apps[i]->module_id == apps[j]->module_id) return EDGE_EBUSY;
        }
    }
    return EDGE_OK;
}

edge_status_t edge_sys_init(edge_sys_t *sys, edge_module_t **apps, size_t count)
{
    if (sys == NULL) return EDGE_EINVAL;
    const edge_status_t rc = validate_apps(apps, count);
    if (rc < 0) return rc;
    sys->apps = apps;
    sys->app_count = count;
    sys->subscriptions = NULL;
    sys->subscription_count = 0u;
    sys->subscription_capacity = 0u;
    sys->required_ids = NULL;
    sys->required_count = 0u;
    sys->events = NULL;
    sys->tick = 0u;
    sys->state = EDGE_SYS_CONSTRUCTED;
    for (size_t i = 0u; i < count; ++i) {
        apps[i]->initialized = 0u;
        apps[i]->running = 0u;
        apps[i]->failed = 0u;
    }
    sort_apps(apps, count);
    return EDGE_OK;
}

edge_status_t edge_sys_bind_event_queue(edge_sys_t *sys, edge_event_queue_t *queue,
                                        edge_sys_subscription_t *subscriptions,
                                        size_t capacity)
{
    if (sys == NULL || queue == NULL || (subscriptions == NULL && capacity != 0u)) return EDGE_EINVAL;
    if (sys->state != EDGE_SYS_CONSTRUCTED) return EDGE_ESTATE;
    sys->events = queue;
    sys->subscriptions = subscriptions;
    sys->subscription_count = 0u;
    sys->subscription_capacity = capacity;
    return EDGE_OK;
}

edge_status_t edge_sys_set_required(edge_sys_t *sys, const uint32_t *ids, size_t count)
{
    if (sys == NULL || (ids == NULL && count != 0u)) return EDGE_EINVAL;
    if (sys->state != EDGE_SYS_CONSTRUCTED) return EDGE_ESTATE;
    sys->required_ids = ids;
    sys->required_count = count;
    return EDGE_OK;
}

edge_status_t edge_sys_subscribe(edge_sys_t *sys, uint32_t event_id, edge_module_t *app)
{
    if (sys == NULL || app == NULL || sys->subscriptions == NULL || event_id == 0u) return EDGE_EINVAL;
    if (sys->state != EDGE_SYS_CONSTRUCTED) return EDGE_ESTATE;
    if (sys->subscription_count >= sys->subscription_capacity) return EDGE_ENOSPC;
    for (size_t i = 0u; i < sys->subscription_count; ++i) {
        if (sys->subscriptions[i].event_id == event_id && sys->subscriptions[i].app == app) return EDGE_EBUSY;
    }
    sys->subscriptions[sys->subscription_count++] = (edge_sys_subscription_t){event_id, app};
    return EDGE_OK;
}

edge_status_t edge_sys_validate_required(const edge_sys_t *sys)
{
    if (sys == NULL) return EDGE_EINVAL;
    for (size_t r = 0u; r < sys->required_count; ++r) {
        size_t matches = 0u;
        for (size_t i = 0u; i < sys->app_count; ++i) {
            if (sys->apps[i]->module_id == sys->required_ids[r]) ++matches;
        }
        if (matches != 1u) return EDGE_EDEPEND;
    }
    return EDGE_OK;
}

edge_status_t edge_sys_start(edge_sys_t *sys)
{
    if (sys == NULL || sys->state != EDGE_SYS_CONSTRUCTED) return EDGE_ESTATE;
    edge_status_t rc = edge_sys_validate_required(sys);
    if (rc < 0) return rc;

    size_t started = 0u;
    for (; started < sys->app_count; ++started) {
        edge_module_t *app = sys->apps[started];
        if (app->init != NULL) {
            rc = app->init(app);
            if (rc < 0) {
                app->failed = 1u;
                if (app->initialized && app->deinit != NULL) (void)app->deinit(app);
                while (started > 0u) {
                    --started;
                    app = sys->apps[started];
                    if (app->initialized && app->deinit != NULL) (void)app->deinit(app);
                    app->initialized = 0u;
                    app->running = 0u;
                }
                sys->state = EDGE_SYS_FAILED;
                return rc;
            }
        }
        app->initialized = 1u;
    }
    for (size_t i = 0u; i < sys->app_count; ++i) sys->apps[i]->running = 1u;
    sys->state = EDGE_SYS_RUNNING;
    return EDGE_OK;
}

edge_status_t edge_sys_dispatch_events(edge_sys_t *sys)
{
    if (sys == NULL || sys->events == NULL) return EDGE_EINVAL;
    if (sys->state != EDGE_SYS_RUNNING) return EDGE_ESTATE;
    edge_event_t event;
    for (;;) {
        const edge_status_t pop_rc = edge_event_pop(sys->events, &event);
        if (pop_rc == EDGE_ENOENT) return EDGE_OK;
        if (pop_rc < 0) return pop_rc;
        for (size_t i = 0u; i < sys->subscription_count; ++i) {
            edge_sys_subscription_t *sub = &sys->subscriptions[i];
            if (sub->event_id == event.id && sub->app != NULL && sub->app->on_event != NULL && !sub->app->failed) {
                const edge_status_t rc = sub->app->on_event(sub->app, &event);
                if (rc < 0) {
                    sub->app->failed = 1u;
                    return rc;
                }
            }
        }
    }
}

edge_status_t edge_sys_run_once(edge_sys_t *sys)
{
    if (sys == NULL || sys->state != EDGE_SYS_RUNNING) return EDGE_ESTATE;
    if (sys->events != NULL) {
        const edge_status_t rc = edge_sys_dispatch_events(sys);
        if (rc < 0) return rc;
    }
    for (size_t i = 0u; i < sys->app_count; ++i) {
        edge_module_t *app = sys->apps[i];
        if (!app->failed && app->poll != NULL) {
            const edge_status_t rc = app->poll(app);
            if (rc < 0) {
                app->failed = 1u;
                return rc;
            }
        }
    }
    ++sys->tick;
    return EDGE_OK;
}

edge_status_t edge_sys_power_off(edge_sys_t *sys)
{
    if (sys == NULL || (sys->state != EDGE_SYS_RUNNING && sys->state != EDGE_SYS_FAILED)) return EDGE_ESTATE;
    for (size_t i = sys->app_count; i > 0u; --i) {
        edge_module_t *app = sys->apps[i - 1u];
        if (app->running && app->power_off != NULL) {
            const edge_status_t rc = app->power_off(app);
            if (rc < 0) { app->failed = 1u; return rc; }
        }
        app->running = 0u;
    }
    sys->state = EDGE_SYS_STOPPED;
    return EDGE_OK;
}

edge_status_t edge_sys_deinit(edge_sys_t *sys)
{
    if (sys == NULL || sys->state == EDGE_SYS_RUNNING) return EDGE_ESTATE;
    for (size_t i = sys->app_count; i > 0u; --i) {
        edge_module_t *app = sys->apps[i - 1u];
        if (app->initialized && app->deinit != NULL) {
            const edge_status_t rc = app->deinit(app);
            if (rc < 0) return rc;
        }
        app->initialized = 0u;
        app->running = 0u;
    }
    sys->state = EDGE_SYS_STOPPED;
    return EDGE_OK;
}
