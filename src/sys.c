#include "edge/sys.h"

static void sort(edge_module_t **a, size_t n) {
    for (size_t i = 1; i < n; ++i) {
        edge_module_t *key = a[i];
        size_t j = i;
        while (j > 0 &&
               (a[j - 1]->priority > key->priority ||
                (a[j - 1]->priority == key->priority &&
                 a[j - 1]->module_id > key->module_id))) {
            a[j] = a[j - 1];
            --j;
        }
        a[j] = key;
    }
}

int edge_sys_init(edge_sys_t *sys, edge_module_t **apps, size_t count) {
    if (!sys || (!apps && count != 0u)) return EDGE_EINVAL;
    sys->apps = apps;
    sys->app_count = count;
    sys->tick = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!apps[i]) return EDGE_EINVAL;
    }
    sort(sys->apps, sys->app_count);
    for (size_t i = 0; i < count; ++i) {
        if (sys->apps[i]->init) {
            int rc = sys->apps[i]->init(sys->apps[i]);
            if (rc < 0) return rc;
        }
    }
    return EDGE_OK;
}

int edge_sys_validate_required(const edge_sys_t *sys, const uint32_t *required_ids, size_t required_count) {
    if (!sys || (!required_ids && required_count != 0u)) return EDGE_EINVAL;
    for (size_t r = 0; r < required_count; ++r) {
        int found = 0;
        for (size_t i = 0; i < sys->app_count; ++i) {
            if (sys->apps[i]->module_id == required_ids[r]) { found = 1; break; }
        }
        if (!found) return EDGE_EDEPEND;
    }
    return EDGE_OK;
}

int edge_sys_run_once(edge_sys_t *sys) {
    if (!sys) return EDGE_EINVAL;
    for (size_t i = 0; i < sys->app_count; ++i) {
        edge_module_t *app = sys->apps[i];
        if (app->poll) {
            int rc = app->poll(app);
            if (rc < 0) return rc;
        }
    }
    ++sys->tick;
    return EDGE_OK;
}

int edge_sys_power_off(edge_sys_t *sys) {
    if (!sys) return EDGE_EINVAL;
    for (size_t i = sys->app_count; i > 0u; --i) {
        edge_module_t *app = sys->apps[i - 1u];
        if (app->power_off) {
            int rc = app->power_off(app);
            if (rc < 0) return rc;
        }
    }
    return EDGE_OK;
}
