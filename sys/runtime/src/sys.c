#include "runtime/sys.h"
#include "sys_internal.h"

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
    if (edge_sys_state(sys)->clock != NULL && edge_sys_state(sys)->clock->monotonic_ticks != NULL)
        return edge_sys_state(sys)->clock->monotonic_ticks(edge_sys_state(sys)->clock->self);
    return ++edge_sys_state(sys)->tick;
}

/* Const-safe current tick: never advances the fallback counter. */
static uint64_t peek_ticks(const edge_sys_t *sys) {
    if (edge_sys_state_const(sys)->clock != NULL &&
        edge_sys_state_const(sys)->clock->monotonic_ticks != NULL)
        return edge_sys_state_const(sys)->clock->monotonic_ticks(
            edge_sys_state_const(sys)->clock->self);
    return edge_sys_state_const(sys)->tick;
}

static bool tick_due(uint64_t now, uint64_t due) {
    return (int64_t)(now - due) >= 0;
}

static edge_status_t pending_pop(edge_sys_t *sys, edge_event_t *out) {
    if (edge_sys_state(sys)->pending == NULL || edge_sys_state(sys)->pending_capacity < 2u ||
        edge_sys_state(sys)->pending_tail == edge_sys_state(sys)->pending_head)
        return EDGE_ENOENT;
    *out = edge_sys_state(sys)->pending[edge_sys_state(sys)->pending_tail];
    edge_sys_state(sys)->pending_tail =
        (edge_sys_state(sys)->pending_tail + 1u) % edge_sys_state(sys)->pending_capacity;
    if (edge_sys_state(sys)->pending_count > 0u)
        --edge_sys_state(sys)->pending_count;
    return EDGE_OK;
}

edge_status_t edge_sys_configure(edge_sys_t *sys, const edge_sys_config_t *config) {
    if (sys == NULL || config == NULL)
        return EDGE_EINVAL;

    edge_status_t rc = edge_sys_init(sys, config->apps, config->app_count);
    if (rc < 0)
        return rc;

    if (config->events != NULL || config->subscriptions != NULL ||
        config->subscription_capacity > 0u) {
        rc = edge_sys_bind_event_queue(sys, config->events, config->subscriptions,
                                       config->subscription_capacity);
        if (rc < 0)
            return rc;
    }

    if (config->pending_storage != NULL && config->pending_capacity > 0u) {
        rc = edge_sys_bind_pending_queue(sys, config->pending_storage, config->pending_capacity);
        if (rc < 0)
            return rc;
    }

    if (config->required_ids != NULL || config->required_count > 0u) {
        rc = edge_sys_set_required(sys, config->required_ids, config->required_count);
        if (rc < 0)
            return rc;
    }

    if (config->clock != NULL) {
        rc = edge_sys_set_clock(sys, config->clock);
        if (rc < 0)
            return rc;
    }

    if (config->event_budget > 0u) {
        rc = edge_sys_set_event_budget(sys, config->event_budget);
        if (rc < 0)
            return rc;
    }

    if (config->idle_hook != NULL) {
        rc = edge_sys_set_idle(sys, config->idle_hook, config->idle_ctx);
        if (rc < 0)
            return rc;
    }

    return EDGE_OK;
}

edge_status_t edge_sys_init(edge_sys_t *sys, edge_module_t **apps, size_t count) {
    if (sys == NULL)
        return EDGE_EINVAL;
    const edge_status_t rc = validate_apps(apps, count);
    if (rc < 0)
        return rc;
    edge_sys_state(sys)->apps = apps;
    edge_sys_state(sys)->app_count = count;
    edge_sys_state(sys)->subscriptions = NULL;
    edge_sys_state(sys)->subscription_count = 0u;
    edge_sys_state(sys)->subscription_capacity = 0u;
    edge_sys_state(sys)->required_ids = NULL;
    edge_sys_state(sys)->required_count = 0u;
    edge_sys_state(sys)->events = NULL;
    edge_sys_state(sys)->clock = NULL;
    edge_sys_state(sys)->max_events_per_run = 8u;
    edge_sys_state(sys)->tick = 0u;
    edge_sys_state(sys)->stats = (edge_sys_stats_t){0};
    edge_sys_state(sys)->state = EDGE_SYS_CONSTRUCTED;
    edge_sys_state(sys)->pending = NULL;
    edge_sys_state(sys)->pending_capacity = 0u;
    edge_sys_state(sys)->pending_head = 0u;
    edge_sys_state(sys)->pending_tail = 0u;
    edge_sys_state(sys)->pending_count = 0u;
    edge_sys_state(sys)->idle_hook = NULL;
    edge_sys_state(sys)->idle_ctx = NULL;
    for (size_t i = 0u; i < count; ++i) {
        apps[i]->running = 0u;
        apps[i]->failed = 0u;
        apps[i]->suspended = 0u;
        apps[i]->next_due = 0u;
    }
    sort_apps(apps, count);
    return EDGE_OK;
}

edge_status_t edge_sys_bind_event_queue(edge_sys_t *sys, edge_event_queue_t *queue,
                                        edge_sys_subscription_t *subscriptions, size_t capacity) {
    if (sys == NULL || queue == NULL || (subscriptions == NULL && capacity != 0u))
        return EDGE_EINVAL;
    if (edge_sys_state(sys)->state != EDGE_SYS_CONSTRUCTED)
        return EDGE_ESTATE;
    edge_sys_state(sys)->events = queue;
    edge_sys_state(sys)->subscriptions = subscriptions;
    edge_sys_state(sys)->subscription_count = 0u;
    edge_sys_state(sys)->subscription_capacity = capacity;
    return EDGE_OK;
}

edge_status_t edge_sys_bind_pending_queue(edge_sys_t *sys, edge_event_t *storage,
                                          uint32_t capacity) {
    if (sys == NULL || storage == NULL || capacity < 2u)
        return EDGE_EINVAL;
    if (edge_sys_state(sys)->state != EDGE_SYS_CONSTRUCTED)
        return EDGE_ESTATE;
    edge_sys_state(sys)->pending = storage;
    edge_sys_state(sys)->pending_capacity = capacity;
    edge_sys_state(sys)->pending_head = 0u;
    edge_sys_state(sys)->pending_tail = 0u;
    edge_sys_state(sys)->pending_count = 0u;
    return EDGE_OK;
}

edge_status_t edge_sys_set_clock(edge_sys_t *sys, const edge_clock_port_t *clock) {
    if (sys == NULL)
        return EDGE_EINVAL;
    if (edge_sys_state(sys)->state != EDGE_SYS_CONSTRUCTED)
        return EDGE_ESTATE;
    edge_sys_state(sys)->clock = clock;
    return EDGE_OK;
}

edge_status_t edge_sys_set_event_budget(edge_sys_t *sys, uint32_t max_events_per_run) {
    if (sys == NULL || max_events_per_run == 0u)
        return EDGE_EINVAL;
    if (edge_sys_state(sys)->state != EDGE_SYS_CONSTRUCTED)
        return EDGE_ESTATE;
    edge_sys_state(sys)->max_events_per_run = max_events_per_run;
    return EDGE_OK;
}

edge_status_t edge_sys_set_required(edge_sys_t *sys, const uint32_t *ids, size_t count) {
    if (sys == NULL || (ids == NULL && count != 0u))
        return EDGE_EINVAL;
    if (edge_sys_state(sys)->state != EDGE_SYS_CONSTRUCTED)
        return EDGE_ESTATE;
    edge_sys_state(sys)->required_ids = ids;
    edge_sys_state(sys)->required_count = count;
    return EDGE_OK;
}

edge_status_t edge_sys_set_idle(edge_sys_t *sys, edge_sys_idle_fn hook, void *ctx) {
    if (sys == NULL)
        return EDGE_EINVAL;
    if (edge_sys_state(sys)->state != EDGE_SYS_CONSTRUCTED)
        return EDGE_ESTATE;
    edge_sys_state(sys)->idle_hook = hook;
    edge_sys_state(sys)->idle_ctx = ctx;
    return EDGE_OK;
}

edge_status_t edge_sys_subscribe(edge_sys_t *sys, uint32_t event_id, edge_module_t *app) {
    if (sys == NULL || app == NULL || edge_sys_state(sys)->subscriptions == NULL || event_id == 0u)
        return EDGE_EINVAL;
    if (edge_sys_state(sys)->state != EDGE_SYS_CONSTRUCTED)
        return EDGE_ESTATE;
    if (edge_sys_state(sys)->subscription_count >= edge_sys_state(sys)->subscription_capacity)
        return EDGE_ENOSPC;
    for (size_t i = edge_sys_state(sys)->subscription_count; i-- > 0u;) {
        if (edge_sys_state(sys)->subscriptions[i].event_id == event_id &&
            edge_sys_state(sys)->subscriptions[i].app == app)
            return EDGE_EBUSY;
    }
    edge_sys_state(sys)->subscriptions[edge_sys_state(sys)->subscription_count++] =
        (edge_sys_subscription_t){event_id, app};
    return EDGE_OK;
}

edge_status_t edge_sys_unsubscribe(edge_sys_t *sys, uint32_t event_id, const edge_module_t *app) {
    if (sys == NULL || app == NULL || edge_sys_state(sys)->subscriptions == NULL)
        return EDGE_EINVAL;
    if (edge_sys_state(sys)->state != EDGE_SYS_CONSTRUCTED &&
        edge_sys_state(sys)->state != EDGE_SYS_RUNNING)
        return EDGE_ESTATE;
    for (size_t i = edge_sys_state(sys)->subscription_count; i-- > 0u;) {
        if (edge_sys_state(sys)->subscriptions[i].event_id == event_id &&
            edge_sys_state(sys)->subscriptions[i].app == app) {
            for (size_t j = i + 1u; j < edge_sys_state(sys)->subscription_count; ++j)
                edge_sys_state(sys)->subscriptions[j - 1u] = edge_sys_state(sys)->subscriptions[j];
            --edge_sys_state(sys)->subscription_count;
            return EDGE_OK;
        }
    }
    return EDGE_ENOENT;
}

edge_status_t edge_sys_publish(edge_sys_t *sys, const edge_event_t *event) {
    if (sys == NULL || event == NULL)
        return EDGE_EINVAL;
    if (edge_sys_state(sys)->state != EDGE_SYS_RUNNING)
        return EDGE_ESTATE;
    if (edge_sys_state(sys)->pending == NULL || edge_sys_state(sys)->pending_capacity < 2u)
        return EDGE_ENOTSUP;

    edge_event_t stamped = *event;
    if (edge_sys_state(sys)->clock != NULL && edge_sys_state(sys)->clock->monotonic_ticks != NULL)
        stamped.timestamp =
            edge_sys_state(sys)->clock->monotonic_ticks(edge_sys_state(sys)->clock->self);

    const uint32_t next =
        (edge_sys_state(sys)->pending_head + 1u) % edge_sys_state(sys)->pending_capacity;
    if (next == edge_sys_state(sys)->pending_tail) {
        ++edge_sys_state(sys)->stats.drops;
        return EDGE_EOVERFLOW;
    }
    edge_sys_state(sys)->pending[edge_sys_state(sys)->pending_head] = stamped;
    edge_sys_state(sys)->pending_head = next;
    if (edge_sys_state(sys)->pending_count < edge_sys_state(sys)->pending_capacity)
        ++edge_sys_state(sys)->pending_count;
    if (edge_sys_state(sys)->pending_count > edge_sys_state(sys)->stats.pending_high_water)
        edge_sys_state(sys)->stats.pending_high_water = edge_sys_state(sys)->pending_count;
    return EDGE_OK;
}

edge_status_t edge_sys_validate_required(const edge_sys_t *sys) {
    if (sys == NULL)
        return EDGE_EINVAL;
    for (size_t r = 0u; r < edge_sys_state_const(sys)->required_count; ++r) {
        size_t matches = 0u;
        for (size_t i = 0u; i < edge_sys_state_const(sys)->app_count; ++i) {
            if (edge_sys_state_const(sys)->apps[i]->module_id ==
                edge_sys_state_const(sys)->required_ids[r])
                ++matches;
        }
        if (matches != 1u)
            return EDGE_EDEPEND;
    }
    return EDGE_OK;
}

edge_status_t edge_sys_start(edge_sys_t *sys) {
    if (sys == NULL || edge_sys_state(sys)->state != EDGE_SYS_CONSTRUCTED)
        return EDGE_ESTATE;
    const edge_status_t rc = edge_sys_validate_required(sys);
    if (rc < 0)
        return rc;

    /* D51: module init is the composition root's job, so start only schedules.
     * A required module that the product failed to assemble is caught above. */
    const uint64_t now = now_ticks(sys);
    for (size_t i = 0u; i < edge_sys_state(sys)->app_count; ++i) {
        edge_module_t *app = edge_sys_state(sys)->apps[i];
        app->next_due = now + app->period;
        app->running = app->failed ? 0u : 1u;
    }
    edge_sys_state(sys)->state = EDGE_SYS_RUNNING;
    return EDGE_OK;
}

edge_status_t edge_sys_dispatch_events(edge_sys_t *sys) {
    if (sys == NULL || edge_sys_state(sys)->events == NULL)
        return EDGE_EINVAL;
    if (edge_sys_state(sys)->state != EDGE_SYS_RUNNING)
        return EDGE_ESTATE;

    edge_status_t first_error = EDGE_OK;
    uint32_t handled = 0u;
    while (handled < edge_sys_state(sys)->max_events_per_run) {
        edge_event_t event;
        edge_status_t pop_rc = edge_event_pop(edge_sys_state(sys)->events, &event);
        if (pop_rc == EDGE_ENOENT)
            pop_rc = pending_pop(sys, &event);
        if (pop_rc == EDGE_ENOENT)
            break;
        if (pop_rc < 0) {
            ++edge_sys_state(sys)->stats.errors;
            if (first_error == EDGE_OK)
                first_error = pop_rc;
            break;
        }
        ++handled;
        ++edge_sys_state(sys)->stats.events;
        /*
         * Reverse iteration, because an `on_event` callback may unsubscribe - which
         * removes an entry and shifts the array left. Going forward would then visit
         * the slot whose occupant moved into it and skip that subscription
         * entirely; going backwards only ever steps into slots a left shift cannot
         * have changed underneath us.
         */
        for (size_t i = edge_sys_state(sys)->subscription_count; i-- > 0u;) {
            edge_sys_subscription_t *sub = &edge_sys_state(sys)->subscriptions[i];
            if (sub->event_id == event.id && sub->app != NULL && sub->app->on_event != NULL &&
                !sub->app->failed && !sub->app->suspended) {
                const edge_status_t rc = sub->app->on_event(sub->app, &event);
                if (rc < 0) {
                    sub->app->failed = 1u;
                    ++edge_sys_state(sys)->stats.errors;
                    ++edge_sys_state(sys)->stats.isolated;
                    if (first_error == EDGE_OK)
                        first_error = rc;
                }
            }
        }
    }
    return first_error;
}

edge_status_t edge_sys_run_once(edge_sys_t *sys) {
    if (sys == NULL || edge_sys_state(sys)->state != EDGE_SYS_RUNNING)
        return EDGE_ESTATE;

    const uint32_t events_before = edge_sys_state(sys)->stats.events;
    const uint32_t polls_before = edge_sys_state(sys)->stats.polls;

    edge_status_t first_error = EDGE_OK;
    if (edge_sys_state(sys)->events != NULL) {
        const edge_status_t rc = edge_sys_dispatch_events(sys);
        if (rc < 0)
            first_error = rc;
    }

    const uint64_t now = now_ticks(sys);
    for (size_t i = 0u; i < edge_sys_state(sys)->app_count; ++i) {
        edge_module_t *app = edge_sys_state(sys)->apps[i];
        if (app->failed || app->suspended || app->poll == NULL || app->period == 0u)
            continue;
        if (!tick_due(now, app->next_due))
            continue;

        const uint64_t start = now_ticks(sys);
        const edge_status_t rc = app->poll(app);
        const uint64_t elapsed = now_ticks(sys) - start;
        ++edge_sys_state(sys)->stats.polls;
        app->next_due += app->period;
        if (app->budget != 0u && elapsed > app->budget) {
            ++edge_sys_state(sys)->stats.budget_hits;
            if (first_error == EDGE_OK)
                first_error = EDGE_EOVERFLOW;
        }
        if (rc < 0) {
            app->failed = 1u;
            ++edge_sys_state(sys)->stats.errors;
            ++edge_sys_state(sys)->stats.isolated;
            if (first_error == EDGE_OK)
                first_error = rc;
        }
    }

    /* D52: nothing to do this step -> hand the context to the idle hook. */
    if (edge_sys_state(sys)->state == EDGE_SYS_RUNNING &&
        edge_sys_state(sys)->stats.events == events_before &&
        edge_sys_state(sys)->stats.polls == polls_before)
        (void)edge_sys_idle(sys);

    return first_error;
}

edge_status_t edge_sys_step(edge_sys_t *sys) {
    return edge_sys_run_once(sys);
}

edge_status_t edge_sys_run(edge_sys_t *sys) {
    if (sys == NULL)
        return EDGE_EINVAL;
    edge_status_t first_error = EDGE_OK;
    bool running = edge_sys_state(sys)->state == EDGE_SYS_RUNNING;
    while (running) {
        const edge_status_t rc = edge_sys_run_once(sys);
        if (rc < 0 && first_error == EDGE_OK)
            first_error = rc;
        running = edge_sys_state(sys)->state == EDGE_SYS_RUNNING;
    }
    return first_error;
}

edge_status_t edge_sys_idle(edge_sys_t *sys) {
    if (sys == NULL)
        return EDGE_EINVAL;
    ++edge_sys_state(sys)->stats.idle_calls;
    if (edge_sys_state(sys)->idle_hook != NULL)
        edge_sys_state(sys)->idle_hook(edge_sys_state(sys)->idle_ctx);
    return EDGE_OK;
}

edge_status_t edge_sys_suspend_all(edge_sys_t *sys) {
    if (sys == NULL || edge_sys_state(sys)->state != EDGE_SYS_RUNNING)
        return EDGE_ESTATE;
    edge_status_t first_error = EDGE_OK;
    for (size_t i = 0u; i < edge_sys_state(sys)->app_count; ++i) {
        edge_module_t *app = edge_sys_state(sys)->apps[i];
        if (app->failed || app->suspended)
            continue;
        if (app->suspend != NULL) {
            const edge_status_t rc = app->suspend(app);
            if (rc < 0) {
                app->failed = 1u;
                ++edge_sys_state(sys)->stats.errors;
                ++edge_sys_state(sys)->stats.isolated;
                if (first_error == EDGE_OK)
                    first_error = rc;
                continue;
            }
        }
        app->suspended = 1u;
    }
    return first_error;
}

edge_status_t edge_sys_resume_all(edge_sys_t *sys) {
    if (sys == NULL || edge_sys_state(sys)->state != EDGE_SYS_RUNNING)
        return EDGE_ESTATE;
    edge_status_t first_error = EDGE_OK;
    for (size_t i = edge_sys_state(sys)->app_count; i > 0u; --i) {
        edge_module_t *app = edge_sys_state(sys)->apps[i - 1u];
        if (app->failed || !app->suspended)
            continue;
        if (app->resume != NULL) {
            const edge_status_t rc = app->resume(app);
            if (rc < 0) {
                app->failed = 1u;
                ++edge_sys_state(sys)->stats.errors;
                ++edge_sys_state(sys)->stats.isolated;
                if (first_error == EDGE_OK)
                    first_error = rc;
                continue;
            }
        }
        app->suspended = 0u;
    }
    return first_error;
}

edge_status_t edge_sys_power_off(edge_sys_t *sys) {
    if (sys == NULL || (edge_sys_state(sys)->state != EDGE_SYS_RUNNING &&
                        edge_sys_state(sys)->state != EDGE_SYS_FAILED))
        return EDGE_ESTATE;
    edge_status_t first_error = EDGE_OK;
    for (size_t i = edge_sys_state(sys)->app_count; i > 0u; --i) {
        edge_module_t *app = edge_sys_state(sys)->apps[i - 1u];
        if (app->running && app->power_off != NULL) {
            const edge_status_t rc = app->power_off(app);
            if (rc < 0) {
                app->failed = 1u;
                ++edge_sys_state(sys)->stats.errors;
                if (first_error == EDGE_OK)
                    first_error = rc;
            }
        }
        app->running = 0u;
        app->suspended = 0u;
    }
    edge_sys_state(sys)->state = EDGE_SYS_STOPPED;
    return first_error;
}

edge_status_t edge_sys_deinit(edge_sys_t *sys) {
    if (sys == NULL || edge_sys_state(sys)->state == EDGE_SYS_RUNNING)
        return EDGE_ESTATE;
    /* D51: tearing down module state is the composition root's job (reverse
     * order); sys only stops scheduling. */
    for (size_t i = 0u; i < edge_sys_state(sys)->app_count; ++i) {
        edge_sys_state(sys)->apps[i]->running = 0u;
        edge_sys_state(sys)->apps[i]->suspended = 0u;
    }
    edge_sys_state(sys)->state = EDGE_SYS_STOPPED;
    return EDGE_OK;
}

edge_status_t edge_sys_stats_get(const edge_sys_t *sys, edge_sys_stats_t *out) {
    if (sys == NULL || out == NULL)
        return EDGE_EINVAL;
    *out = edge_sys_state_const(sys)->stats;
    if (edge_sys_state_const(sys)->events != NULL)
        out->drops += edge_event_dropped(edge_sys_state_const(sys)->events);
    return EDGE_OK;
}

edge_status_t edge_sys_stats_reset(edge_sys_t *sys) {
    if (sys == NULL)
        return EDGE_EINVAL;
    edge_sys_state(sys)->stats = (edge_sys_stats_t){0};
    return EDGE_OK;
}

bool edge_sys_healthy(const edge_sys_t *sys) {
    if (sys == NULL)
        return false;
    for (size_t i = 0u; i < edge_sys_state_const(sys)->app_count; ++i) {
        if (edge_sys_state_const(sys)->apps[i]->failed != 0u)
            return false;
    }
    return edge_sys_state_const(sys)->state == EDGE_SYS_RUNNING;
}

bool edge_sys_pending(const edge_sys_t *sys) {
    if (sys == NULL)
        return false;
    if (edge_sys_state_const(sys)->events != NULL &&
        edge_event_count(edge_sys_state_const(sys)->events) > 0u)
        return true;
    if (edge_sys_state_const(sys)->pending_count > 0u)
        return true;
    const uint64_t now = peek_ticks(sys);
    for (size_t i = 0u; i < edge_sys_state_const(sys)->app_count; ++i) {
        const edge_module_t *app = edge_sys_state_const(sys)->apps[i];
        if (app->failed != 0u || app->suspended != 0u || app->poll == NULL || app->period == 0u)
            continue;
        if (tick_due(now, app->next_due))
            return true;
    }
    return false;
}

edge_status_t edge_sys_next_due(const edge_sys_t *sys, uint64_t now, uint64_t *next_due) {
    if (sys == NULL || next_due == NULL)
        return EDGE_EINVAL;
    if (edge_sys_state_const(sys)->state != EDGE_SYS_RUNNING) {
        *next_due = now;
        return EDGE_ESTATE;
    }
    if ((edge_sys_state_const(sys)->events != NULL &&
         edge_event_count(edge_sys_state_const(sys)->events) > 0u) ||
        edge_sys_state_const(sys)->pending_count > 0u) {
        *next_due = now;
        return EDGE_OK;
    }
    uint64_t earliest = UINT64_MAX;
    for (size_t i = 0u; i < edge_sys_state_const(sys)->app_count; ++i) {
        const edge_module_t *app = edge_sys_state_const(sys)->apps[i];
        if (app->failed != 0u || app->suspended != 0u || app->poll == NULL || app->period == 0u)
            continue;
        if (tick_due(now, app->next_due)) {
            *next_due = now;
            return EDGE_OK;
        }
        if (app->next_due < earliest)
            earliest = app->next_due;
    }
    *next_due = earliest;
    return EDGE_OK;
}
