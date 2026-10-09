#include "zmk_behavior_queue/behavior_queue.h"

static edge_status_t app_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t app_power_off(edge_module_t *module) {
    zmk_behavior_queue_app_t *app = (zmk_behavior_queue_app_t *)edge_module_data(module);
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    zmk_behavior_queue_clear(app);
    return EDGE_OK;
}

void zmk_behavior_queue_construct(zmk_behavior_queue_app_t *app, uint32_t module_id,
                                  uint8_t priority, const zmk_behavior_queue_sink_if_t *sink) {
    if (app == NULL) {
        return;
    }
    *app = (__typeof__(*app)){0};
    app->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 1u,
        .poll = app_poll,
        .power_off = app_power_off,
        .private_data = app,
    };

    if (sink != NULL) {
        app->sink = *sink;
    }
}

edge_status_t zmk_behavior_queue_init(zmk_behavior_queue_app_t *app) {
    if (app == NULL || app->sink.invoke_binding == NULL) {
        return EDGE_EINVAL;
    }
    zmk_behavior_queue_clear(app);
    return EDGE_OK;
}

void zmk_behavior_queue_clear(zmk_behavior_queue_app_t *app) {
    if (app == NULL) {
        return;
    }
    app->head = 0;
    app->tail = 0;
    app->count = 0;
    app->waiting = false;
    app->next_run_time_ms = 0;
}

bool zmk_behavior_queue_is_empty(const zmk_behavior_queue_app_t *app) {
    return (app == NULL || app->count == 0);
}

uint16_t zmk_behavior_queue_count(const zmk_behavior_queue_app_t *app) {
    return (app != NULL) ? app->count : 0;
}

edge_status_t zmk_behavior_queue_add(zmk_behavior_queue_app_t *app, uint16_t behavior_id,
                                     uint32_t param1, uint32_t param2, bool press,
                                     uint32_t position, uint32_t wait_ms, uint32_t timestamp_ms) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    if (app->count >= ZMK_BEHAVIOR_QUEUE_MAX_ITEMS) {
        return EDGE_ENOSPC;
    }

    zmk_behavior_queue_item_t *item = &app->items[app->tail];
    item->behavior_id = behavior_id;
    item->param1 = param1;
    item->param2 = param2;
    item->press = press;
    item->position = position;
    item->wait_ms = wait_ms;

    app->tail = (uint16_t)((app->tail + 1) % ZMK_BEHAVIOR_QUEUE_MAX_ITEMS);
    app->count++;

    if (!app->waiting) {
        return zmk_behavior_queue_process(app, timestamp_ms);
    }

    return EDGE_OK;
}

edge_status_t zmk_behavior_queue_process(zmk_behavior_queue_app_t *app, uint32_t current_time_ms) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }

    if (app->waiting && (int32_t)(current_time_ms - app->next_run_time_ms) < 0) {
        return EDGE_OK;
    }

    app->waiting = false;

    while (app->count > 0) {
        zmk_behavior_queue_item_t item = app->items[app->head];
        app->head = (uint16_t)((app->head + 1) % ZMK_BEHAVIOR_QUEUE_MAX_ITEMS);
        app->count--;

        if (app->sink.invoke_binding != NULL) {
            edge_status_t rc =
                app->sink.invoke_binding(app->sink.self, item.behavior_id, item.param1, item.param2,
                                         item.press, item.position, current_time_ms);
            if (rc != EDGE_OK) {
                return rc;
            }
        }

        if (item.wait_ms > 0) {
            app->waiting = true;
            app->next_run_time_ms = current_time_ms + item.wait_ms;
            break;
        }
    }

    return EDGE_OK;
}
