#include "pal_os/power.h"

#include <stddef.h>

edge_status_t edge_pm_init(edge_pm_state_t *pm, uint64_t stop_threshold_ticks,
                           uint64_t standby_threshold_ticks) {
    if (pm == NULL)
        return EDGE_EINVAL;
    for (size_t i = 0u; i < (size_t)EDGE_PM_MODE_COUNT; ++i)
        pm->lock_counts[i] = 0u;
    pm->stop_threshold_ticks = stop_threshold_ticks;
    pm->standby_threshold_ticks = standby_threshold_ticks;
    return EDGE_OK;
}

edge_status_t edge_pm_lock(edge_pm_state_t *pm, edge_pm_mode_t mode) {
    if (pm == NULL || (int)mode < 0 || mode >= EDGE_PM_MODE_COUNT)
        return EDGE_EINVAL;
    if (pm->lock_counts[mode] == UINT32_MAX)
        return EDGE_EOVERFLOW;
    ++pm->lock_counts[mode];
    return EDGE_OK;
}

edge_status_t edge_pm_unlock(edge_pm_state_t *pm, edge_pm_mode_t mode) {
    if (pm == NULL || (int)mode < 0 || mode >= EDGE_PM_MODE_COUNT)
        return EDGE_EINVAL;
    if (pm->lock_counts[mode] == 0u)
        return EDGE_ESTATE;
    --pm->lock_counts[mode];
    return EDGE_OK;
}

edge_pm_mode_t edge_pm_target_mode(const edge_pm_state_t *pm, uint64_t idle_ticks) {
    if (pm == NULL || idle_ticks == 0u)
        return EDGE_PM_ACTIVE;

    /* Check active constraints from highest constraint (ACTIVE) downwards */
    if (pm->lock_counts[EDGE_PM_ACTIVE] > 0u)
        return EDGE_PM_ACTIVE;
    if (pm->lock_counts[EDGE_PM_IDLE] > 0u)
        return EDGE_PM_IDLE;

    /* If STOP is locked, system cannot enter STANDBY */
    const bool stop_locked = pm->lock_counts[EDGE_PM_STOP] > 0u;

    if (!stop_locked && pm->standby_threshold_ticks > 0u &&
        idle_ticks >= pm->standby_threshold_ticks)
        return EDGE_PM_STANDBY;

    if (pm->stop_threshold_ticks > 0u && idle_ticks >= pm->stop_threshold_ticks)
        return EDGE_PM_STOP;

    return EDGE_PM_IDLE;
}

edge_status_t edge_pm_execute(const edge_pm_state_t *pm, uint64_t idle_ticks,
                              const edge_pal_port_t *pal, edge_board_pm_enter_fn board_pm,
                              void *board_ctx, edge_os_pending_fn pending, void *pending_ctx) {
    if (pal == NULL)
        return EDGE_EINVAL;

    const edge_pm_mode_t target = edge_pm_target_mode(pm, idle_ticks);
    if (target == EDGE_PM_ACTIVE)
        return EDGE_OK;

    if (pal->critical_enter != NULL)
        pal->critical_enter(pal->self);

    if (pending == NULL || !pending(pending_ctx)) {
        if (board_pm != NULL) {
            (void)board_pm(target, idle_ticks, board_ctx);
        } else if (pal->idle != NULL) {
            pal->idle(pal->self);
        }
    }

    if (pal->critical_exit != NULL)
        pal->critical_exit(pal->self);

    return EDGE_OK;
}
